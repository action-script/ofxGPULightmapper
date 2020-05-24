#include "ofxGPULightmapper.h"

bool ofxGPULightmapper::setup(function<void()> scene) {
    this->scene = scene;
    return setup();
}

bool ofxGPULightmapper::setup() {
    // allocate depth FBO
    ofFbo depthFBO_0, depthFBO_1;
    allocatFBO(depthFBO_0, FBO_DEPTH);
    allocatFBO(depthFBO_1, FBO_DEPTH);
    depthFBO.push_back(depthFBO_0);
    depthFBO.push_back(depthFBO_1);

    // set up passes vector
    glm::mat4 bm;
    lastBiasedMatrix.push_back(bm);
    lastBiasedMatrix.push_back(bm);
    glm::vec3 lpos;
    lastLightPos.push_back(lpos);
    lastLightPos.push_back(lpos);
    this->fboIndex = 0;

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


    // compile light packer shader
    success &= lightmapShader.setupShaderFromSource(GL_VERTEX_SHADER,
        R"(
        #version 330
        uniform mat4 modelViewProjectionMatrix;
        uniform mat4 shadowViewProjectionMatrix;
        uniform mat4 modelMatrix;
        uniform vec3 light; // shadow light position
        in vec4 position;
        in vec4 normal;
        in vec2 texcoord;
        out vec4 v_shadowPos;
        out vec3 v_normal;
        out vec2 v_texcoord;
        void main() {
            // fix contact shadows by moving geometry against light
            vec3 pos = position.xyz + normalize(cross(normal.xyz, cross(normal.xyz, light))) * 0.02;
            v_shadowPos = shadowViewProjectionMatrix * (modelMatrix * vec4(pos, 1));
            v_normal = normal.xyz;
            v_texcoord = texcoord;
            gl_Position = vec4(texcoord * 2.0 - 1.0, 0.0, 1.0);
        }
        )"
    );
    // geometry dilation as conservative rasterization
    success &= lightmapShader.setupShaderFromSource(GL_GEOMETRY_SHADER_EXT,
        R"(
        #version 150
        #define M_PI 3.1415926535897932384626433832795
        layout (triangles) in;
        layout (triangle_strip, max_vertices = 3) out;
        uniform float texSize;
        uniform float dilation;
        in vec4 v_shadowPos[3];
        in vec3 v_normal[3];
        in vec2 v_texcoord[3];
        out vec4 f_shadowPos;
        out vec3 f_normal;
        out vec2 f_texcoord;
        float atan2(in float y, in float x) {
            bool s = (abs(x) > abs(y));
            return mix(M_PI/2.0 - atan(x,y), atan(y,x), s);
        }
        void main() {
            float hPixel = (1.0/texSize)*dilation;
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
                gl_Position = vec4(lp1.xy + offs, 0, 1);
                f_shadowPos = v_shadowPos[i1];
                f_normal = v_normal[i1];
                f_texcoord = v_texcoord[i1];
                EmitVertex();
            }
        }
        )"
    );
    success &= lightmapShader.setupShaderFromSource(GL_FRAGMENT_SHADER,
        R"(
        #version 330
        uniform sampler2DShadow shadowMap;
        uniform vec3 light; // shadow light position
        uniform float sampleCount;
        uniform float shadow_bias; // 0.003
        in vec4 f_shadowPos;
        in vec3 f_normal;
        in vec2 f_texcoord;
        out vec4 outputColor;
        vec3 coords = f_shadowPos.xyz / f_shadowPos.w;
        float shadow = 1.0f;
        void main() {
            coords.y = 1-coords.y;
            // check if coords are in shadowMap texture
            if (coords.x>0.0 && coords.x<1.0 && coords.y>0.0 && coords.y<1.0) {
                shadow = texture(shadowMap, vec3(coords.xy, coords.z - shadow_bias));
            }
            outputColor = vec4(vec3(shadow), 1.0 / (1.0 + sampleCount));
            //outputColor = vec4(f_texcoord.x, f_texcoord.y, 0.5,1);
        }
        )"
    );
    if(ofIsGLProgrammableRenderer()) lightmapShader.bindDefaults();
	success &= lightmapShader.linkProgram();
    //lightmapShader.setGeometryInputType(GL_TRIANGLE_STRIP);

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

    this->lastBiasedMatrix[fboIndex] = this->bias * viewProjection;
    this->lastLightPos[fboIndex] = light.getPosition();

    // begin depth render FBO and shader
    depthShader.begin();
    depthFBO[fboIndex].begin(OF_FBOMODE_NODEFAULTS);

    ofEnableDepthTest();

    ofPushView();

    ofSetMatrixMode(OF_MATRIX_PROJECTION);
    ofLoadMatrix(ortho);

    ofSetMatrixMode(OF_MATRIX_MODELVIEW);
    ofLoadViewMatrix(view);

    ofViewport(ofRectangle(1,0,depthFBO[fboIndex].getWidth(),depthFBO[fboIndex].getHeight()));
    ofClear(0);
}

void ofxGPULightmapper::endShadowMap() {
    // end depth render FBO and shader
    depthFBO[fboIndex].end();
    depthShader.end();
    ofPopView(); // pop at the end to prevent trigger update matrix stack
}

void ofxGPULightmapper::beginBake(ofFbo& fbo, int sampleCount) {
    // render shadowMap into texture space
    ofDisableDepthTest();

    // begin lightmap shader and FBO
    lightmapShader.begin();
    ofEnableBlendMode(OF_BLENDMODE_ALPHA);
    fbo.begin();

    //glClearColor(0,0,0,0);
    //glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // bind shadow map to texture
    lightmapShader.setUniformTexture("shadowMap", this->depthFBO[fboIndex].getDepthTexture(), 0);
    // pass the light projection view
    lightmapShader.setUniformMatrix4f("shadowViewProjectionMatrix", this->lastBiasedMatrix[fboIndex]);
    // pass the light position
    lightmapShader.setUniform3f("light", this->lastLightPos[fboIndex]);

    lightmapShader.setUniform1f("sampleCount", sampleCount);
    lightmapShader.setUniform1f("texSize", fbo.getWidth());
    lightmapShader.setUniform1f("dilation", this->geometry_dilation);
    lightmapShader.setUniform1f("shadow_bias", this->shadow_bias);
}

void ofxGPULightmapper::endBake(ofFbo& fbo) {
    fbo.end();
    ofDisableBlendMode();
    lightmapShader.end();
}

void ofxGPULightmapper::allocateFBO(ofFbo& fbo, glm::vec2 size) {
    lightFboSettings.width  = size.x;
    lightFboSettings.height = size.y;
    allocatFBO(fbo, FBO_LIGHT);
}

void ofxGPULightmapper::allocatFBO(ofFbo& fbo, FBO_TYPE type) {
    switch (type) {
        case FBO_TYPE::FBO_DEPTH:
            fbo.allocate(depthFboSettings);
            fbo.getDepthTexture().setRGToRGBASwizzles(true);
            // allow depth texture to compare in glsl
            fbo.getDepthTexture().bind();
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
            //glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LESS);
            fbo.getDepthTexture().unbind();
            break;
        case FBO_TYPE::FBO_LIGHT:
            fbo.allocate(this->lightFboSettings);
            break;
    }
}

void ofxGPULightmapper::updateShadowMap(ofNode & light, glm::vec3 origin, float softness, 
    float fustrumSize, float nearClip, float farClip) {
    for (int i = 0; i < depthFBO.size();  i++) {
        ofNode nlight;
        glm::vec3 lightDir = glm::sphericalRand(farClip/2.f);
        if (i % 2 == 0)
            lightDir = light.getPosition() + lightDir*(softness * (1+ofRandomf())/2.0);

        nlight.setPosition(lightDir + origin);
        nlight.lookAt(origin);

        this->fboIndex = i;
        beginShadowMap(nlight, fustrumSize, nearClip, farClip);
        scene();
        endShadowMap();
    }
    this->fboIndex = 0;
}

void ofxGPULightmapper::bake(ofMesh& mesh, ofFbo& fbo, ofNode& node, int sampleCount) {
    for (int i = 0; i < depthFBO.size();  i++) {
        this->fboIndex = i;
        beginBake(fbo, sampleCount*2+i);
        node.transformGL();
        mesh.draw();
        node.restoreTransformGL();
        endBake(fbo);
    }
    this->fboIndex = 0;
}
