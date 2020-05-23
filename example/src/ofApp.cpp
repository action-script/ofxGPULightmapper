#include "ofApp.h"

//--------------------------------------------------------------
void ofApp::setup() {
    ofDisableArbTex(); // user TEXTURE_2D instead RECTANGLE [0-1] instead of px

    bool success = lighmapper.setup();
    if (success) ofLog() << "Lightmapper ready";

    // scene
    cam.setDistance(3.0f);
    cam.setNearClip(0.01f);
    cam.setFarClip(20.0f);

    mesh.load("monkey.ply");
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

    lighmapper.begin(light, 10, 0.01, 10);
    {
        // render scene
        mesh.draw();
    }
    lighmapper.end();

    lighmapper.beginBake(sampleCount);
    {
        // pass model and geometry
        ofPushMatrix();
        mesh.draw();
        ofPopMatrix();
    }
    lighmapper.endBake();


    // render scene
    ofBackground(30);
    ofEnableDepthTest();
    cam.begin();
    {
        ofPushMatrix();
        lighmapper.getLightmapTexture().bind();
        mesh.draw();
        lighmapper.getLightmapTexture().unbind();
        ofPopMatrix();
    }
    cam.end();

    ofDisableDepthTest();
    lighmapper.getDepthTexture().draw(0,0, 300, 300);
    lighmapper.getLightmapTexture().draw(300, 0, 300, 300);

    sampleCount++;
}

//--------------------------------------------------------------
void ofApp::renderScene() {
}
//--------------------------------------------------------------
void ofApp::keyPressed(int key) {
    sampleCount = 0;
}
