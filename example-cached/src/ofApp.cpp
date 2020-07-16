#include "ofApp.h"

//--------------------------------------------------------------
void ofApp::setup() {
    // set up lightmapper and pass scene draw function
    function<void()> scene = bind(&ofApp::renderScene, this);
    bool success = lightmapper.setup(scene, 12);
    if (success) ofLog() << "Lightmapper ready";

    lightmapper.setShadowBias(0.0015f);
    lightmapper.setContactShadowFactor(0.005);

    // load models
    meshBox.load("box.ply");
    meshDonut.load("donut.ply");
    meshPill.load("pill.ply");

    // generate lightmap coords for the models
    lightmapper.lightmapPack(meshBox, {500,500});
    lightmapper.lightmapPack(meshPill);
    lightmapper.lightmapPack(meshDonut);

    // prepare random scene
    resetScene();

    // material
    ofDisableArbTex();
    material.load("material");
    texture.load("UVtexture.png");

    // camera
    cam.setDistance(10.0f);
    cam.setNearClip(0.01f);
    cam.setFarClip(100.0f);
}

//--------------------------------------------------------------
void ofApp::update() {
    // bake
    ofLight light; // also works with ofNode
    light.setPosition(glm::vec3(3, 10, -1.4));
    light.lookAt(glm::vec3(0,0,0));

    if (currentTarget < models.size()) {
        auto& model = *models[currentTarget];
        auto& node = transformations.at(currentTarget);
        auto& fbo = lightmaps.at(currentTarget);

        // compute depth map for shadow [sharing cache]
        lightmapper.updateCachedShadowMap(light, sampleCount, {0,0,0}, 0.05, 12, 0.01, 20);

        // bake target model
        lightmapper.bake(model, fbo, node, sampleCount);

        sampleCount++;

        if (sampleCount >= maxSamples) {
            sampleCount = 0;
            currentTarget += 1;
        }
    }
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
    int mapsize = 300;
    lightmapper.getDepthTexture().draw(ofGetWidth()-mapsize,0, mapsize, mapsize);
    lightmapper.getDepthTexture(1).draw(ofGetWidth()-mapsize, mapsize, mapsize, mapsize);
}

//--------------------------------------------------------------
void ofApp::renderScene() {
    // render scene

    for (size_t i; i < models.size(); i++) {
        auto& model = *models[i];
        auto& node = transformations.at(i);
        auto& fbo = lightmaps.at(i);

        node.transformGL();

    material.begin();
    material.setUniformTexture("tex0", texture, 0);
        material.setUniformTexture("tex1", fbo.getTextureReference(), 1);
        model.draw();
    material.end();

        node.restoreTransformGL();
    }
}
//--------------------------------------------------------------
void ofApp::keyPressed(int key) {
    resetScene();
}
//--------------------------------------------------------------
void ofApp::resetScene() {
    sampleCount = 0;
    currentTarget = 0;

    models.clear();
    transformations.clear();
    lightmaps.clear();

    size_t nDonuts = (int)ofRandom(4,8);
    size_t nPills = (int)ofRandom(3,10);
    //nDonuts = 1;
    //nPills = 1;

    models.resize(1+nDonuts+nPills);
    transformations.resize(1+nDonuts+nPills);
    lightmaps.resize(1+nDonuts+nPills);

    models[0] = &meshBox;
    ofNode nodeBox;
    nodeBox.panDeg(-90);
    transformations[0] = nodeBox;
    ofFbo fboBox;
    lightmaps[0] = fboBox;
    lightmapper.allocateFBO(lightmaps.at(0));

    // random place models on the box
    int bxs = 2.3;
    for (size_t i = 0; i < nDonuts; i++) {
        models[i+1] = &meshDonut;
        ofNode node;
        node.setScale(ofRandom(0.2, 0.9));
        node.setPosition({ofRandom(-bxs,bxs),ofRandom(-bxs,bxs),ofRandom(-bxs,bxs)});
        auto q = ofQuaternion(ofRandom(360), {1,0,0}, ofRandom(360), {0,1,0}, ofRandom(360), {0,0,1});
        node.rotate(q);
        transformations[i+1] = node;
        ofFbo fbo;
        lightmapper.allocateFBO(fbo);
        lightmaps[i+1] = fbo;
    }

    for (size_t i = 0; i < nPills; i++) {
        models[i+1+nDonuts] = &meshPill;
        ofNode node;
        node.setScale(ofRandom(0.2, 0.9));
        node.setPosition({ofRandom(-bxs,bxs),ofRandom(-bxs,bxs),ofRandom(-bxs,bxs)});
        auto q = ofQuaternion(ofRandom(360), {1,0,0}, ofRandom(360), {0,1,0}, ofRandom(360), {0,0,1});
        node.rotate(q);
        transformations[i+1+nDonuts] = node;
        ofFbo fbo;
        lightmapper.allocateFBO(fbo);
        lightmaps[i+1+nDonuts] = fbo;
    }
}
