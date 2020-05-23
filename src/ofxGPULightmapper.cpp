#include "ofxGPULightmapper.h"

bool ofxGPULightmapper::setup() {
    // set up depthFBO
    ofFbo::Settings depthFboSettings;

    depthFboSettings.depthStencilAsTexture = true;
    depthFboSettings.depthStencilInternalFormat = GL_DEPTH_COMPONENT32;
    depthFboSettings.width = 1024;
    depthFboSettings.height = 1024;
    depthFboSettings.minFilter = GL_NEAREST;
    depthFboSettings.maxFilter = GL_NEAREST;
    depthFboSettings.numColorbuffers = 0;
    depthFboSettings.textureTarget = GL_TEXTURE_2D;
    depthFboSettings.useDepth = true;
    depthFboSettings.useStencil = false;
    depthFboSettings.wrapModeHorizontal = GL_CLAMP_TO_EDGE;
    depthFboSettings.wrapModeVertical = GL_CLAMP_TO_EDGE;
    depthFBO.allocate(depthFboSettings);
    depthFBO.getDepthTexture().setRGToRGBASwizzles(true);

    // allow depth texture to compare in glsl
    depthFBO.getDepthTexture().bind();
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    depthFBO.getDepthTexture().unbind();


    // set up lightmapFBO
    //ofFbo::Settings lightFboSettings;
/*
 *
 *    //lightFboSettings.depthStencilAsTexture = false;
 *    lightFboSettings.depthStencilInternalFormat = GL_RGBA32F_ARB;
 *    lightFboSettings.width = 1024;
 *    lightFboSettings.height = 1024;
 *    //lightFboSettings.minFilter = GL_NEAREST;
 *    //lightFboSettings.maxFilter = GL_NEAREST;
 *    //lightFboSettings.minFilter = GL_LINEAR;
 *    //lightFboSettings.maxFilter = GL_LINEAR;
 *    //lightFboSettings.numColorbuffers = 2;
 *    lightFboSettings.textureTarget = GL_TEXTURE_2D;
 *    lightFboSettings.useDepth = false;
 *    lightFboSettings.useStencil = false;
 *    //lightFboSettings.numSamples = 0;
 *    lightFboSettings.wrapModeHorizontal = GL_CLAMP_TO_EDGE;
 *    lightFboSettings.wrapModeVertical = GL_CLAMP_TO_EDGE;
 *    lightmapFBO.allocate(lightFboSettings);
 *
 */

    lightmapFBO.allocate(1024, 1024, GL_RGBA32F_ARB, 8);
    //lightmapFBO.allocate(900, 900, GL_RGBA32F_ARB);


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
            float hPixel = (1.0/texSize)*2;
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

void ofxGPULightmapper::begin(ofNode& light, float fustrumSize, float nearClip, float farClip) {
    // calculate light projection 
    float left      = -fustrumSize / 2.;
    float right     =  fustrumSize / 2.;
    float top       =  fustrumSize / 2.;
    float bottom    = -fustrumSize / 2.;
    auto ortho  = glm::ortho(left, right, bottom, top, nearClip, farClip);
    auto view   = glm::inverse(light.getGlobalTransformMatrix());
    auto viewProjection = ortho * view;

    this->lastBiasedMatrix = this->bias * viewProjection;
    this->lastLightPos = light.getPosition();

    // begin depth render FBO and shader
    depthShader.begin();
    depthFBO.begin(OF_FBOMODE_NODEFAULTS);

    ofEnableDepthTest();

    ofPushView();

    ofSetMatrixMode(OF_MATRIX_PROJECTION);
    ofLoadMatrix(ortho);

    ofSetMatrixMode(OF_MATRIX_MODELVIEW);
    ofLoadViewMatrix(view);

    ofViewport(ofRectangle(0,0,depthFBO.getWidth(),depthFBO.getHeight()));
    ofClear(0);
}

void ofxGPULightmapper::end() {
    // end depth render FBO and shader
    depthFBO.end();
    depthShader.end();
    ofPopView(); // pop at the end to prevent trigger update matrix stack
}

void ofxGPULightmapper::beginBake(int sampleCount) {
    // render shadowMap into texture space
    ofDisableDepthTest();

    // begin lightmap shader and FBO
    lightmapShader.begin();
    ofEnableBlendMode(OF_BLENDMODE_ALPHA);
    lightmapFBO.begin();

    //glClearColor(0,0,0,0);
    //glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // bind shadow map to texture
    lightmapShader.setUniformTexture("shadowMap", this->depthFBO.getDepthTexture(), 0);
    // pass the light projection view
    lightmapShader.setUniformMatrix4f("shadowViewProjectionMatrix", this->lastBiasedMatrix);
    // pass the light position
    lightmapShader.setUniform3f("light", this->lastLightPos);

    lightmapShader.setUniform1f("sampleCount", sampleCount);
    lightmapShader.setUniform1f("texSize", lightmapFBO.getWidth());
    lightmapShader.setUniform1f("shadow_bias", this->shadow_bias);
}

void ofxGPULightmapper::endBake() {
    lightmapFBO.end();
    ofDisableBlendMode();
    lightmapShader.end();
}

