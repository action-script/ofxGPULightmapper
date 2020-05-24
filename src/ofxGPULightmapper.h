#pragma once

#include "ofMain.h"

namespace gpulm
{
    enum FBO_TYPE {
        FBO_DEPTH,
        FBO_LIGHT
    };
}

using namespace gpulm;

class ofxGPULightmapper {
    public:
        ofxGPULightmapper() {
            // define depthFBO settings
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

            // define lightmapFBO settings
            lightFboSettings.depthStencilAsTexture = false;
            lightFboSettings.internalformat = GL_RGBA32F_ARB;
            lightFboSettings.width = 1024;
            lightFboSettings.height = 1024;
            lightFboSettings.minFilter = GL_LINEAR;
            lightFboSettings.maxFilter = GL_LINEAR;
            lightFboSettings.textureTarget = GL_TEXTURE_2D;
            lightFboSettings.useDepth = false;
            lightFboSettings.useStencil = false;
            lightFboSettings.numSamples = 8;
            lightFboSettings.wrapModeHorizontal = GL_CLAMP_TO_EDGE;
            lightFboSettings.wrapModeVertical = GL_CLAMP_TO_EDGE;
        }
        bool setup();
        bool setup(function<void()> scene);

        // easy API
        void updateShadowMap(ofNode & light, glm::vec3 origin = {0,0,0}, float softness = 0.3,
                float fustrumSize = 10, float nearClip = 0.01, float farClip = 100);
        void bake(ofMesh& mesh, ofFbo& fbo, ofNode& node, int sampleCount);
 
        // shadow mapping
        void beginShadowMap(ofNode & light, float fustrumSize = 10, float nearClip = 0.01, float farClip = 100);
        void endShadowMap();

        // light packing
        void allocateFBO(ofFbo& fbo, glm::vec2 size = {1024, 1024});
        void beginBake(ofFbo& fbo, int sampleCount);
        void endBake(ofFbo& fbo);

        const ofTexture& getDepthTexture(unsigned int index = 0) const { return depthFBO[index].getDepthTexture(); }
        ofTexture& getDepthTexture(unsigned int index = 0) { return depthFBO[index].getDepthTexture(); }

    private:
        void allocatFBO(ofFbo& fbo, FBO_TYPE type);

        ofFbo::Settings depthFboSettings;
        ofFbo::Settings lightFboSettings;

        std::vector<ofFbo> depthFBO;   // scene depth
        unsigned int fboIndex = 0;

        ofShader depthShader;       // depth test
        ofShader lightmapShader;    // shadow mapping

        // conservative rasterization.
        float geometry_dilation = 2;

        float shadow_bias = 0.003f;
        glm::mat4 bias = glm::mat4(
            0.5, 0.0, 0.0, 0.0,
            0.0, 0.5, 0.0, 0.0,
            0.0, 0.0, 0.5, 0.0,
            0.5, 0.5, 0.5, 1.0);

        std::vector<glm::mat4> lastBiasedMatrix;
        std::vector<glm::vec3> lastLightPos;

        // render scene function
        function<void()> scene;
};
