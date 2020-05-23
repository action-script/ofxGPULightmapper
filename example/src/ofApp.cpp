#include "ofApp.h"

//--------------------------------------------------------------
void ofApp::setup() {
    bool success = lightmapper.setup();
    if (success) ofLog() << "Lightmapper ready";

    meshMonkey.load("monkey.ply");
    meshPlane.load("plane.ply");
    meshTube.load("tube.ply");
    meshWall.load("wall.ply");

    lightmapper.allocateFBO(fboMonkey);
    lightmapper.allocateFBO(fboPlane, {800,800});
    lightmapper.allocateFBO(fboTube);
    lightmapper.allocateFBO(fboWall, {500,500});

    // scene
    cam.setDistance(3.0f);
    cam.setNearClip(0.01f);
    cam.setFarClip(20.0f);
}

//--------------------------------------------------------------
void ofApp::update(){
}

//--------------------------------------------------------------
void ofApp::draw() {
    ofEnableDepthTest();

    // TODO: auto light + soft shadow
    // set light
    ofLight light;
    glm::vec3 lightDir = glm::sphericalRand(6.f);
    if (sampleCount % 2 == 0)
        lightDir = glm::vec3(-2.8f, 1.6f, 2.3f) + lightDir*(0.3 * (1+ofRandomf())/2.0);

    if (lightDir.y < 0) lightDir = -lightDir;
    light.setPosition(lightDir);
    light.lookAt(glm::vec3(0,0,0));

    lightmapper.begin(light, 10, 0.01, 10);
    {
        // render scene
        meshMonkey.draw();
        meshPlane.draw();
        meshTube.draw();
        meshWall.draw();
    }
    lightmapper.end();

    lightmapper.beginBake(fboMonkey, sampleCount);
    meshMonkey.draw();
    lightmapper.endBake(fboMonkey);
    lightmapper.beginBake(fboPlane, sampleCount);
    meshPlane.draw();
    lightmapper.endBake(fboPlane);
    lightmapper.beginBake(fboTube, sampleCount);
    meshTube.draw();
    lightmapper.endBake(fboTube);
    lightmapper.beginBake(fboWall, sampleCount);
    meshWall.draw();
    lightmapper.endBake(fboWall);


    // render scene
    ofBackground(30);
    ofEnableDepthTest();
    cam.begin();
    {
        ofPushMatrix();
        fboMonkey.getTextureReference().bind();
        meshMonkey.draw();
        fboMonkey.getTextureReference().unbind();

        fboPlane.getTextureReference().bind();
        meshPlane.draw();
        fboPlane.getTextureReference().unbind();
        
        fboTube.getTextureReference().bind();
        meshTube.draw();
        fboTube.getTextureReference().unbind();
        
        fboWall.getTextureReference().bind();
        meshWall.draw();
        fboWall.getTextureReference().unbind();
        ofPopMatrix();
    }
    cam.end();

    ofDisableDepthTest();
    //lightmapper.getDepthTexture().draw(0,0, 300, 300);
    fboMonkey.draw(0, 0, 200, 200);
    fboPlane.draw(200, 0, 200, 200);
    fboTube.draw(0, 200, 200, 200);
    fboWall.draw(200, 200, 200, 200);

    sampleCount++;
}

//--------------------------------------------------------------
void ofApp::renderScene() {
}
//--------------------------------------------------------------
void ofApp::keyPressed(int key) {
    sampleCount = 0;
}
