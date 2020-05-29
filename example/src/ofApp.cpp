#include "ofApp.h"

//--------------------------------------------------------------
void ofApp::setup() {
    // set up lightmapper and pass scene draw function
    function<void()> scene = bind(&ofApp::renderScene, this);
    bool success = lightmapper.setup(scene);
    if (success) ofLog() << "Lightmapper ready";

    // load models
    meshMonkey.load("monkey.ply");
    meshPlane.load("plane.ply");
    meshTube.load("tube.ply");
    meshWall.load("wall.ply");

    // place them into world
    nodeMonkey.setPosition({0,0,0});
    nodeMonkey.setScale({1.4,1.8,1.4});
    nodePlane.setPosition({0,0,0});
    nodeTube.setPosition({2,0,-2.8});
    nodeTube.setScale({1,1.8,1});
    nodeWall.setPosition({-2,0,2.8});
    nodeWall.setScale({1,0.8,1});
    nodeWall.panDeg(30);

    // generate lightmap coords for the model
    lightmapper.lightmapPack(meshMonkey);
    //lightmapper.lightmapPack(meshPlane, {800,800});
    lightmapper.lightmapPack(meshTube);
    lightmapper.lightmapPack(meshWall, {500,500});

    // setup lightmaps
    lightmapper.allocateFBO(fboMonkey);
    lightmapper.allocateFBO(fboPlane, {800,800});
    lightmapper.allocateFBO(fboTube);
    lightmapper.allocateFBO(fboWall, {500,500});

    // material
    ofDisableArbTex();
    material.load("phong");
    texture.load("UVtexture.png");

    // camera
    cam.setDistance(3.0f);
    cam.setNearClip(0.01f);
    cam.setFarClip(100.0f);
}

//--------------------------------------------------------------
void ofApp::update(){
    // bake
    ofLight light; // also works with ofNode
    light.setPosition(glm::vec3(-2.52348, 2.79526, 7.84836));
    light.lookAt(glm::vec3(0,0,0));

    // compute depth map for shadow
    lightmapper.updateShadowMap(light, {0,0,0}, 0.2, 12, 0.01, 15);

    // bake each geometry using its world transformation
    lightmapper.bake(meshMonkey, fboMonkey, nodeMonkey, sampleCount);
    lightmapper.bake(meshPlane, fboPlane, nodePlane, sampleCount);
    lightmapper.bake(meshTube, fboTube, nodeTube, sampleCount);
    lightmapper.bake(meshWall, fboWall, nodeWall, sampleCount);
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
    lightmapper.getDepthTexture().draw(ofGetWidth()-300,0, 300, 300);
    lightmapper.getDepthTexture(1).draw(ofGetWidth()-300,300, 300, 300);
    int debugSize = 150;
    fboMonkey.draw(0, 0, debugSize, debugSize);
    fboPlane.draw(debugSize, 0, debugSize, debugSize);
    fboTube.draw(0, debugSize, debugSize, debugSize);
    fboWall.draw(debugSize, debugSize, debugSize, debugSize);

    sampleCount++;
}

//--------------------------------------------------------------
void ofApp::renderScene() {
    // render scene
    
    nodeMonkey.transformGL();
    material.begin();
    material.setUniformTexture("tex0", texture, 0);
    material.setUniformTexture("tex1", fboMonkey.getTextureReference(), 1);
    meshMonkey.draw();
    material.end();
    nodeMonkey.restoreTransformGL();

    nodePlane.transformGL();
    fboPlane.getTextureReference().bind();
    meshPlane.draw();
    fboPlane.getTextureReference().unbind();
    nodePlane.restoreTransformGL();
    
    nodeTube.transformGL();
    material.begin();
    material.setUniformTexture("tex0", texture, 0);
    material.setUniformTexture("tex1", fboTube.getTextureReference(), 1);
    meshTube.draw();
    material.end();
    nodeTube.restoreTransformGL();
    
    nodeWall.transformGL();
    material.begin();
    material.setUniformTexture("tex0", texture, 0);
    material.setUniformTexture("tex1", fboWall.getTextureReference(), 1);
    meshWall.draw();
    material.end();
    nodeWall.restoreTransformGL();
}
//--------------------------------------------------------------
void ofApp::keyPressed(int key) {
    sampleCount = 0;
}
