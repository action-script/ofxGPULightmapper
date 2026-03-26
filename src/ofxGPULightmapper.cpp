#include "ofxGPULightmapper.h"

#define TRIANGLEPACKER_IMPLEMENTATION
//#define TP_DEBUG_OUTPUT
#include "trianglepacker/trianglepacker.h"

bool ofxGPULightmapper::setup(std::function<void()> scene, unsigned int numPasses) {
    this->scene = scene;
    this->numPasses = numPasses * 2;
    return setup();
}

bool ofxGPULightmapper::setup() {
    // clear existing state before (re)initializing
    depthFBO.clear();
    lastBiasedMatrix.clear();
    lastLightPos.clear();
    random_cache.clear();

    for (unsigned int i = 0; i < numPasses; i++) {
        depthFBO.emplace_back(new ofFbo);
        allocateFBO(*depthFBO[i], FBO_TYPE::DEPTH);
        lastBiasedMatrix.emplace_back();
        lastLightPos.emplace_back();
    }

    this->passIndex = 0;

    // determine how many shadow maps we can bind simultaneously
    GLint maxTexUnits;
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &maxTexUnits);
    batchSize = std::min((int)numPasses, (int)maxTexUnits - 1);

    // compile depth shaders
    bool success;
    success = depthShader.setupShaderFromSource(GL_VERTEX_SHADER,
		R"(
		#version 330
		uniform mat4 modelViewProjectionMatrix;
		in vec4 position;
		void main() {
			gl_Position = modelViewProjectionMatrix * position;
		}
		)"
    );
	success &= depthShader.setupShaderFromSource(GL_FRAGMENT_SHADER,
		R"(
		#version 330
		out float fragDepth;
		void main() {
			fragDepth = gl_FragCoord.z;
		}
		)"
    );
	success &= depthShader.linkProgram();


    // compile lightmap shader
    // vertex outputs world position; fragment projects into each shadow map per batch
    std::string lm_vertexshader = R"(
        #version 330
        uniform mat4 modelViewProjectionMatrix;
        uniform mat4 modelMatrix;
        uniform vec3 light; // first light in batch, used for contact shadow offset
        uniform int usingPackedTriangles;
        uniform float contact_shadow_factor;
        in vec4 position;
        in vec4 normal;
        in vec2 texcoord;
    )";
    // trick to ensure same location through shaders
    // custom triangle packed UVs
    lm_vertexshader += "layout (location = "+std::to_string(LM_TEXCOORDS_LOCATION)+") in vec2 t_texcoord;\n";
    lm_vertexshader += R"(
        out vec4 v_worldPos;
        out vec3 v_normal;
        out vec2 v_texcoord;
        void main() {
            // fix contact shadows by moving geometry against light
            vec3 pos = position.xyz + normalize(cross(normal.xyz, cross(normal.xyz, light))) * contact_shadow_factor;
            v_worldPos = modelMatrix * vec4(pos, 1);
            v_normal = normal.xyz;
            v_texcoord = mix(texcoord, t_texcoord, usingPackedTriangles);
            gl_Position = vec4(v_texcoord * 2.0 - 1.0, 0.0, 1.0);
        }
    )";
    success &= lightmapShader.setupShaderFromSource(GL_VERTEX_SHADER, lm_vertexshader);
    // geometry dilation as conservative rasterization
    success &= lightmapShader.setupShaderFromSource(GL_GEOMETRY_SHADER_EXT,
        R"(
        #version 150
        #define M_PI 3.1415926535897932384626433832795
        layout (triangles) in;
        layout (triangle_strip, max_vertices = 9) out;
        uniform float texSize;
        uniform float dilation;
        in vec4 v_worldPos[3];
        in vec3 v_normal[3];
        in vec2 v_texcoord[3];
        out vec4 f_worldPos;
        out vec3 f_normal;
        out vec2 f_texcoord;
        float atan2(in float y, in float x) {
            bool s = (abs(x) > abs(y));
            return mix(M_PI/2.0 - atan(x,y), atan(y,x), s);
        }
        void emit(vec4 position, int i) {
            gl_Position = position;
            f_worldPos = v_worldPos[i];
            f_normal = v_normal[i];
            f_texcoord = v_texcoord[i];
            EmitVertex();
        }
        void main() {
            float hPixel = (1.0/texSize)*dilation;
            vec4 vertices[3];
            for (int i = 0; i < 3; i++) {
                int i0 = i, i1 = (i+1)%3, i2 = (i+2)%3;
                vec4 lp0 = gl_in[i0].gl_Position;
                vec4 lp1 = gl_in[i1].gl_Position;
                vec4 lp2 = gl_in[i2].gl_Position;
                vec2 v0 = normalize(lp0.xy - lp1.xy);
                vec2 v1 = normalize(lp2.xy - lp1.xy);
                vec2 mixed = -normalize((v0+v1)/2.f);
                float angle = atan2(v0.y, v0.x) - atan2(mixed.y, mixed.x);
                float vlength = abs(hPixel / sin(angle));
                vec2 offs = mixed * vec2(vlength);
                vertices[i1] = vec4(lp1.xy + offs, 0, 1);
            }

            /*
             *    0                     \
             *    ^                      |  original geometry
             *   / \                     |  uv coords = original geometry
             * 1/___\2___4____6         /
             *  \   |   /|   /|\        \
             *   \  |  / |  / | \        |  dilation vertices
             *    \ | /  | /  |  \       |  uv coords = original geometry
             *     \|/___|/___|___\     /
             *      3    5    7    8
             *
             *   [0-4] [1-6] [3(2)-8]
             */

            emit(gl_in[0].gl_Position, 0);  // 0
            emit(gl_in[1].gl_Position, 1);  // 1
            emit(gl_in[2].gl_Position, 2);  // 2

            emit(vertices[2], 2);           // 3
            emit(gl_in[0].gl_Position, 0);  // 4
            emit(vertices[0], 0);           // 5
            emit(gl_in[1].gl_Position, 1);  // 6
            emit(vertices[1], 1);           // 7
            emit(vertices[2], 2);           // 8
        }
        )"
    );
    // Fragment shader: sample all shadow maps in batch and average.
    // Loop is unrolled with literal indices — GLSL 3.30 doesn't guarantee dynamic sampler indexing.
    // Alpha blend weight = batchSize/(accumulatedPasses+batchSize) for weighted running average.
    std::string frag = "#version 330\n"
        "#define BATCH_SIZE " + std::to_string(batchSize) + "\n" + R"(
        uniform sampler2DShadow shadowMaps[BATCH_SIZE];
        uniform mat4 shadowViewProjectionMatrices[BATCH_SIZE];
        uniform int batchSize;
        uniform float accumulatedPasses;
        uniform float shadow_bias;
        uniform float shadowMapTexelSize; // 1.0 / shadow map resolution
        in vec4 f_worldPos;
        in vec3 f_normal;
        in vec2 f_texcoord;
        out vec4 outputColor;
    )";
    // PCF helper per shadow map — 3x3 kernel for smooth edges.
    // Each texture() on sampler2DShadow does hardware bilinear comparison,
    // so 9 taps effectively give 4x4 filtering.
    for (int i = 0; i < batchSize; i++) {
        std::string si = std::to_string(i);
        frag += "        float sampleShadow" + si + "(vec3 co) {\n"
                "            float s = 0.0;\n"
                "            for (int y = -1; y <= 1; y++)\n"
                "                for (int x = -1; x <= 1; x++)\n"
                "                    s += texture(shadowMaps[" + si + "], vec3(co.xy + vec2(x, y) * shadowMapTexelSize, co.z - shadow_bias));\n"
                "            return s / 9.0;\n"
                "        }\n";
    }
    frag += R"(
        void main() {
            float shadowSum = 0.0;
    )";
    for (int i = 0; i < batchSize; i++) {
        std::string si = std::to_string(i);
        frag += "            if (" + si + " < batchSize) {\n"
                "                vec4 sp = shadowViewProjectionMatrices[" + si + "] * f_worldPos;\n"
                "                vec3 co = sp.xyz / sp.w;\n"
                "                co.y = 1.0 - co.y;\n"
                "                float s = 1.0;\n"
                "                if (co.x > 0.0 && co.x < 1.0 && co.y > 0.0 && co.y < 1.0)\n"
                "                    s = sampleShadow" + si + "(co);\n"
                "                shadowSum += s;\n"
                "            }\n";
    }
    frag += R"(
            outputColor = vec4(vec3(shadowSum / float(batchSize)), float(batchSize) / (accumulatedPasses + float(batchSize)));
        }
    )";
    success &= lightmapShader.setupShaderFromSource(GL_FRAGMENT_SHADER, frag);
    if(ofIsGLProgrammableRenderer()) lightmapShader.bindDefaults();
	success &= lightmapShader.linkProgram();

    return success;
}

void ofxGPULightmapper::beginShadowMap(ofNode& light, float fustrumSize, float nearClip, float farClip) {
    // calculate light projection 
    float left      = -fustrumSize / 2.;
    float right     =  fustrumSize / 2.;
    float top       =  fustrumSize / 2.;
    float bottom    = -fustrumSize / 2.;
    auto ortho  = glm::ortho(left, right, bottom, top, nearClip, farClip);
    auto view   = glm::inverse(light.getGlobalTransformMatrix());
    auto viewProjection = ortho * view;

    this->lastBiasedMatrix[passIndex] = this->clipToUvMatrix * viewProjection;
    this->lastLightPos[passIndex] = light.getPosition();

    // begin depth render FBO and shader
    depthShader.begin();
    depthFBO[passIndex]->begin(OF_FBOMODE_NODEFAULTS);

    ofEnableDepthTest();
    // slope-scaled depth bias: adjusts per-polygon based on angle to light
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(shadow_bias_slope, shadow_bias_units);

    ofPushView();

    ofSetMatrixMode(OF_MATRIX_PROJECTION);
    ofLoadMatrix(ortho);

    ofSetMatrixMode(OF_MATRIX_MODELVIEW);
    ofLoadViewMatrix(view);

    ofViewport(ofRectangle(1,0,depthFBO[passIndex]->getWidth(),depthFBO[passIndex]->getHeight()));
    ofClear(0);
}

void ofxGPULightmapper::endShadowMap() {
    glDisable(GL_POLYGON_OFFSET_FILL);
    depthFBO[passIndex]->end();
    depthShader.end();
    ofPopView(); // pop at the end to prevent trigger update matrix stack
}

void ofxGPULightmapper::beginBake(ofFbo& fbo, int sampleCount, bool usingPackedTriangles) {
    // legacy single-pass entry point — wraps the first pass as a single-item batch
    beginBakeBatch(fbo, passIndex, 1, (float)sampleCount, usingPackedTriangles);
}

void ofxGPULightmapper::beginBakeBatch(ofFbo& fbo, int startPass, int count, float accumulatedPasses, bool usingPackedTriangles) {
    ofDisableDepthTest();
    lightmapShader.begin();
    ofEnableBlendMode(OF_BLENDMODE_ALPHA);
    fbo.begin();

    // bind depth textures and set uniform arrays via direct GL for reliable sampler array support
    for (int i = 0; i < count; i++) {
        depthFBO[startPass + i]->getDepthTexture().bind(i);
    }
    GLint samplerLoc = lightmapShader.getUniformLocation("shadowMaps");
    if (samplerLoc != -1) {
        std::vector<int> units(count);
        for (int i = 0; i < count; i++) units[i] = i;
        glUniform1iv(samplerLoc, count, units.data());
    }
    GLint matLoc = lightmapShader.getUniformLocation("shadowViewProjectionMatrices");
    if (matLoc != -1) {
        glUniformMatrix4fv(matLoc, count, GL_FALSE, glm::value_ptr(lastBiasedMatrix[startPass]));
    }

    lightmapShader.setUniform1i("batchSize", count);
    lightmapShader.setUniform1f("accumulatedPasses", accumulatedPasses);
    lightmapShader.setUniform3f("light", lastLightPos[startPass]);
    lightmapShader.setUniform1i("usingPackedTriangles", usingPackedTriangles);
    lightmapShader.setUniform1f("texSize", fbo.getWidth());
    lightmapShader.setUniform1f("dilation", this->geometry_dilation);
    lightmapShader.setUniform1f("contact_shadow_factor", this->contact_shadow_factor);
    lightmapShader.setUniform1f("shadow_bias", this->shadow_bias);
    lightmapShader.setUniform1f("shadowMapTexelSize", 1.0f / depthFboSettings.width);
}

void ofxGPULightmapper::endBake(ofFbo& fbo) {
    fbo.end();
    ofDisableBlendMode();
    lightmapShader.end();
    // unbind depth textures from all units to prevent feedback loops —
    // next frame's shadow map rendering writes to these same FBOs
    for (int i = 0; i < batchSize; i++) {
        depthFBO[i]->getDepthTexture().unbind(i);
    }
}

void ofxGPULightmapper::allocateFBO(ofFbo& fbo, glm::vec2 size) {
    lightFboSettings.width  = size.x;
    lightFboSettings.height = size.y;
    allocateFBO(fbo, FBO_TYPE::LIGHT);
}

void ofxGPULightmapper::allocateFBO(ofFbo& fbo, FBO_TYPE type) {
    switch (type) {
        case FBO_TYPE::DEPTH:
            fbo.allocate(depthFboSettings);
            fbo.getDepthTexture().setRGToRGBASwizzles(true);
            // allow depth texture to compare in glsl
            fbo.getDepthTexture().bind();
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LESS);
            fbo.getDepthTexture().unbind();
            break;
        case FBO_TYPE::LIGHT: {
            fbo.allocate(this->lightFboSettings);
            // glClearTexImage writes directly to texture memory without binding the FBO,
            // avoiding framebuffer state changes. Called once at allocation, not per frame.
            GLuint texId = fbo.getTexture().getTextureData().textureID;
            const GLfloat zeros[4] = {0, 0, 0, 0};
            glClearTexImage(texId, 0, GL_RGBA, GL_FLOAT, zeros);
            break;
        }
    }
}

void ofxGPULightmapper::updateCachedShadowMap(ofNode & light, int sampleCount, glm::vec3 origin, float softness, 
    float fustrumSize, float nearClip, float farClip) {
    for (int i = 0; i < numPasses; i++) {
        ofNode nlight;
        auto lad = light.getLookAtDir();
        auto pos = light.getPosition();
        float radius = glm::distance(lad, pos);

        int index = sampleCount * numPasses + i;
        glm::vec3 lightDir;
        if (index >= random_cache.size()) {
            lightDir = glm::sphericalRand(radius);
            if (i % 2 == 0)
                lightDir = light.getPosition() + lightDir*(softness * (1+ofRandomf())/2.0);

            if (lightDir.y < 0) lightDir.y = -lightDir.y;
            random_cache.push_back(lightDir);
        } else
            lightDir = random_cache[index];

        nlight.setPosition(lightDir + origin);
        nlight.lookAt(origin);

        this->passIndex = i;
        beginShadowMap(nlight, fustrumSize, nearClip, farClip);
        scene();
        endShadowMap();
    }
    this->passIndex = 0;
}

void ofxGPULightmapper::updateShadowMap(ofNode & light, glm::vec3 origin, float softness, 
    float fustrumSize, float nearClip, float farClip) {
    for (int i = 0; i < numPasses; i++) {
        ofNode nlight;
        auto lad = light.getLookAtDir();
        auto pos = light.getPosition();
        float radius = glm::distance(lad, pos);
        glm::vec3 lightDir = glm::sphericalRand(radius);
        if (i % 2 == 0)
            lightDir = light.getPosition() + lightDir*(softness * (1+ofRandomf())/2.0);

        if (lightDir.y < 0) lightDir.y = -lightDir.y;
        nlight.setPosition(lightDir + origin);
        nlight.lookAt(origin);

        this->passIndex = i;
        beginShadowMap(nlight, fustrumSize, nearClip, farClip);
        scene();
        endShadowMap();
    }
    this->passIndex = 0;
}

void ofxGPULightmapper::bake(ofMesh& mesh, ofFbo& fbo, ofNode& node, int sampleCount) {
    int numBatches = (numPasses + batchSize - 1) / batchSize;
    for (int b = 0; b < numBatches; b++) {
        int start = b * batchSize;
        int count = std::min(batchSize, (int)numPasses - start);
        float accumulatedPasses = float(sampleCount * (int)numPasses + start);
        beginBakeBatch(fbo, start, count, accumulatedPasses);
        node.transformGL();
        mesh.draw();
        node.restoreTransformGL();
        endBake(fbo);
    }
    this->passIndex = 0;
}

void ofxGPULightmapper::bake(ofVboMesh& mesh, ofFbo& fbo, ofNode& node, int sampleCount) {
    bool usingPackedTriangles = mesh.getVbo().hasAttribute(this->LM_TEXCOORDS_LOCATION);
    int numBatches = (numPasses + batchSize - 1) / batchSize;
    for (int b = 0; b < numBatches; b++) {
        int start = b * batchSize;
        int count = std::min(batchSize, (int)numPasses - start);
        float accumulatedPasses = float(sampleCount * (int)numPasses + start);
        beginBakeBatch(fbo, start, count, accumulatedPasses, usingPackedTriangles);
        node.transformGL();
        mesh.drawInstanced(OF_MESH_FILL, 1);
        node.restoreTransformGL();
        endBake(fbo);
    }
    this->passIndex = 0;
}

// pack geometry into UV triangles. Generates coords for a LighMap texture.
// Only works if has individual edges. this why getUniqueFaces()
// If not, the mesh would have shared edges pointing to individual geometry on texture coordinate.
bool ofxGPULightmapper::lightmapPack(ofVboMesh& mesh, glm::vec2 size) {
    // Re-assign mesh data as independent indices. Not shared vertex anmore.
    mesh.setFromTriangles(mesh.getUniqueFaces());

    int vertexCount = mesh.getNumVertices();
    float scale = 0.0f;

    std::vector<glm::vec3> triangles;
    const float* positions;
    int uvCount;
    if (mesh.hasIndices()) {
        // read vertices as consecutive triangles
        auto vertices = mesh.getVertices();
        for (auto& i : mesh.getIndices()) {
            triangles.push_back(vertices[i]);
        }

        positions = (const float*)triangles.data();
        uvCount = triangles.size();

    } else {
        positions = (const float*)mesh.getVerticesPointer();
        uvCount = vertexCount;
    }

    // allocate UVs
    std::vector<glm::vec2> UVs;
    UVs.resize(uvCount);

    bool success = tpPackIntoRect(
        positions, uvCount,
        size.x, size.y, 2, 3, // 2,2 broder, spacing
        glm::value_ptr(UVs[0]), &scale
    );

    if (success) {
        GLint attLoc1 = lightmapShader.getAttributeLocation("t_texcoord"); // located at LM_TEXCOORDS_LOCATION
        // set custom Vertex Color Data
        mesh.getVbo().setAttributeData(attLoc1, glm::value_ptr(UVs[0]), 2, uvCount*2, GL_STATIC_DRAW, sizeof(glm::vec2));
    }

    return success;
}
