#include "ofApp.h"

//--------------------------------------------------------------
void ofApp::setup() {
    // set up lightmapper and pass scene draw function
    function<void()> scene = bind(&ofApp::renderScene, this);
    bool success = lightmapper.setup(scene);
    if (success) ofLog() << "Lightmapper ready";

    lightmapper.setShadowBias(0.001f);
    lightmapper.setContactShadowFactor(0.005);

    // load model
    meshForm.load("basic_form.ply");

    // setup lightmaps
    lightmapper.allocateFBO(fboForm);
    lightmapper.allocateFBO(fboFloor);

    // set up models position
    nodeForm.rotateDeg(90, {0,1,0});
    primFloor.set(5,5);
    primFloor.rotateDeg(90, {1,0,0});

    // camera
    cam.setDistance(3.0f);
    cam.setNearClip(0.01f);
    cam.setFarClip(100.0f);
}

//--------------------------------------------------------------
void ofApp::update(){
    // bake
    ofLight light; // also works with ofNode
    light.setPosition(glm::vec3(4, 2.8, 5));
    light.lookAt(glm::vec3(0,0,0));

    // compute depth map for shadow
    lightmapper.updateShadowMap(light, {0,0,0}, 0.2, 6, 0.1, 11);

    // bake each geometry using its world transformation
    lightmapper.bake(meshForm, fboForm, nodeForm, sampleCount);
    lightmapper.bake(primFloor.getMesh(), fboFloor, primFloor, sampleCount);
}

//--------------------------------------------------------------
void ofApp::draw() {
    // forward render scene from camera
    ofBackground(30);
    ofEnableDepthTest();
    cam.begin();
    {
        renderScene();
    }
    cam.end();

    // debug textures
    ofDisableDepthTest();
    int mapsize = 400;
    lightmapper.getDepthTexture().draw(ofGetWidth()-mapsize,0, mapsize, mapsize);
    lightmapper.getDepthTexture(1).draw(ofGetWidth()-mapsize, mapsize, mapsize, mapsize);
    fboForm.draw(0, 0, mapsize, mapsize);
    fboFloor.draw(0, mapsize, mapsize, mapsize);

    sampleCount++;
}

//--------------------------------------------------------------
void ofApp::renderScene() {
    // render scene
    nodeForm.transformGL();
    fboForm.getTextureReference().bind();
    meshForm.draw();
    fboForm.getTextureReference().unbind();
    nodeForm.restoreTransformGL();

    fboFloor.getTextureReference().bind();
    primFloor.draw();
    fboFloor.getTextureReference().unbind();
}
//--------------------------------------------------------------
void ofApp::keyPressed(int key) {
    sampleCount = 0;
}
