#pragma once

#include "ofMain.h"

class ofxGPULightmapper {
    public:
        bool setup();
 
        // shadow mapping
        void begin(ofNode & light, float fustrumSize = 10, float nearClip = 0.01, float farClip = 100);
        void end();

        // light packing
        void beginBake(int sampleCount);
        void endBake();

        const ofTexture& getDepthTexture() const { return depthFBO.getDepthTexture(); }
        ofTexture& getDepthTexture() { return depthFBO.getDepthTexture(); }

        const ofTexture& getLightmapTexture() const { return lightmapFBO.getTextureReference(); }
        ofTexture& getLightmapTexture() { return lightmapFBO.getTextureReference(); }

    private:
        ofFbo depthFBO;     // scene depth
        ofFbo lightmapFBO;  // texture space projected shadow

        ofShader depthShader;       // depth test
        ofShader lightmapShader;   // shadow mapping

        float shadow_bias = 0.003f;
        glm::mat4 bias = glm::mat4(
            0.5, 0.0, 0.0, 0.0,
            0.0, 0.5, 0.0, 0.0,
            0.0, 0.0, 0.5, 0.0,
            0.5, 0.5, 0.5, 1.0);

        glm::mat4 lastBiasedMatrix;
        glm::vec3 lastLightPos;

};
