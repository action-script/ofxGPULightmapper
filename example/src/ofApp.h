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

        // ply models
        ofVboMesh meshMonkey, meshPlane, meshTube, meshWall;
        // model transformatins
        ofNode nodeMonkey, nodePlane, nodeTube, nodeWall;
        // lightmaps
        ofFbo fboMonkey, fboPlane, fboTube, fboWall;

        // material
        ofShader material;
        ofImage texture;
};
