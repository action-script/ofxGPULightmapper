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

        ofxGPULightmapper lighmapper;
        int sampleCount = 0;

        ofMesh mesh;
        ofEasyCam cam;
};
