#pragma once

#include "ofMain.h"
#include "ofxGPULightmapper.h"

class ofApp : public ofBaseApp{

	public:
		void setup();
		void update();
		void draw();
        void renderScene();

		void keyPressed(int key);
        void resetScene();

        ofxGPULightmapper lightmapper;
        int sampleCount = 0;

        ofEasyCam cam;

        // models
        ofVboMesh meshBox, meshDonut, meshPill;

        std::vector<ofVboMesh*> models;

        std::vector<ofNode> transformations;

        std::vector<ofFbo> lightmaps;

        // material
        ofShader material;
        ofImage texture;

        unsigned int currentTarget = 0, maxSamples = 30*1.5;
};
