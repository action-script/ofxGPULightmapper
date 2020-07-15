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

        ofxGPULightmapper lightmapper;
        int sampleCount = 0;

        ofEasyCam cam;

        // ply model [not VBO] It will share texture UV coords and lightmap coords
        ofMesh meshForm;
        ofPlanePrimitive primFloor; // just using a primitive as floor

        ofNode nodeForm;

        // lightmaps
        ofFbo fboForm;
        ofFbo fboFloor;
};
