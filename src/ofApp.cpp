#include "ofApp.h"
#include "Constants.h"
#include "Introspection.hpp"
#include "ofxTimeMeasurements.h"
#include "dkm.hpp"

//--------------------------------------------------------------
void ofApp::setup(){
  ofSetVerticalSync(false);
  ofEnableAlphaBlending();
  ofDisableArbTex(); // required for texture2D to work in GLSL, makes texture coords normalized
  ofSetFrameRate(Constants::FRAME_RATE);
  ofSetCircleResolution(2048);
  TIME_SAMPLE_SET_FRAMERATE(Constants::FRAME_RATE);

  double minInstance[3] = { 0.0, 0.0, 0.0 };
  double maxInstance[3] = { 1.0, 1.0, 1.0 };
  som.setFeaturesRange(3, minInstance, maxInstance);
  som.setMapSize(Constants::SOM_WIDTH, Constants::SOM_HEIGHT); // can go to 3 dimensions
  som.setInitialLearningRate(0.1);
  som.setNumIterations(3000);
  som.setup();
  
  fluidSimulation.setup({ Constants::FLUID_WIDTH, Constants::FLUID_HEIGHT });
  
  maskFbo.allocate(Constants::CANVAS_WIDTH, Constants::CANVAS_HEIGHT, GL_R8);
  maskShader.load();
  
  foregroundFbo.allocate(Constants::CANVAS_WIDTH, Constants::CANVAS_HEIGHT, GL_RGBA32F);
  foregroundFbo.clearColorBuffer(ofFloatColor(0.0, 0.0, 0.0, 0.0));

  parameters.add(fluidSimulation.getParameterGroup());
  gui.setup(parameters);

  ofxTimeMeasurements::instance()->setEnabled(true);
}

//--------------------------------------------------------------
const int NUM_CLUSTER_CENTRES = 14; //14;
const int CLUSTER_SOURCE_SAMPLES_MAX = 3000; // Note: 1600 raw samples per frame at 30fps
const float POINT_DECAY_RATE = 0.3;
const float SAME_CLUSTER_TOLERANCE = 1.0/10.0;

void ofApp::update() {
  
  TS_START("update-introspection");
  introspection.update();
  TS_STOP("update-introspection");

  TS_START("update-audoanalysis");
  audioDataProcessorPtr->update();
  TS_STOP("update-audoanalysis");

  // fade foreground
  foregroundFbo.begin();
  ofEnableBlendMode(OF_BLENDMODE_ALPHA);
  ofSetColor(ofFloatColor(0.0, 0.0, 0.0, 0.005));
  ofDrawRectangle(0.0, 0.0, foregroundFbo.getWidth(), foregroundFbo.getHeight());
  foregroundFbo.end();

  float s = audioDataProcessorPtr->getNormalisedScalarValue(ofxAudioAnalysisClient::AnalysisScalar::pitch, 200.0, 1800.0);// 700.0, 1300.0);
  float t = audioDataProcessorPtr->getNormalisedScalarValue(ofxAudioAnalysisClient::AnalysisScalar::rootMeanSquare, 0.0, 4600.0); ////400.0, 4000.0, false);
  float u = audioDataProcessorPtr->getNormalisedScalarValue(ofxAudioAnalysisClient::AnalysisScalar::spectralKurtosis, 0.0, 25.0);
  float v = audioDataProcessorPtr->getNormalisedScalarValue(ofxAudioAnalysisClient::AnalysisScalar::spectralCentroid, 0.4, 6.0);
  if (audioDataProcessorPtr->isDataValid()) {
    TS_START("update-som");
    {
      double instance[3] = { static_cast<double>(s), static_cast<double>(t), static_cast<double>(v) };
      som.updateMap(instance);
    }
    TS_STOP("update-som");

    ofFloatColor somColor = somColorAt(s, t);
    ofFloatColor darkSomColor = somColor; darkSomColor.setBrightness(0.2); darkSomColor.setSaturation(1.0);

    // Draw foreground mark for raw audio data sample in darkened saturated SOM color
    foregroundFbo.begin();
    {
      ofEnableBlendMode(OF_BLENDMODE_DISABLED);
      ofSetColor(darkSomColor);
      ofDrawCircle(s*foregroundFbo.getWidth(), t*foregroundFbo.getHeight(), 10.0);
    }
    foregroundFbo.end();

    // Draw fluid mark for raw audio data sample in darkened SOM color
    fluidSimulation.getFlowValuesFbo().getSource().begin();
    {
      ofEnableBlendMode(OF_BLENDMODE_DISABLED);
      ofSetColor(darkSomColor);
      ofDrawCircle(s*Constants::FLUID_WIDTH, t*Constants::FLUID_HEIGHT, 3.0);
    }
    fluidSimulation.getFlowValuesFbo().getSource().end();
    
    TS_START("update-kmeans");
    if (clusterSourceData.size() > CLUSTER_SOURCE_SAMPLES_MAX) {
      clusterSourceData.erase(clusterSourceData.end() - CLUSTER_SOURCE_SAMPLES_MAX, clusterSourceData.end());
    }
    clusterSourceData.push_back({ s, t });
    introspection.addCircle(s, t, 1.0/Constants::WINDOW_WIDTH*5.0, ofColor::grey, true, 30); // introspection: small grey circle for new raw source sample
    if (clusterSourceData.size() > NUM_CLUSTER_CENTRES) {
      dkm::clustering_parameters<float> params(NUM_CLUSTER_CENTRES);
      params.set_random_seed(1000); // keep clusters stable
      clusterResults = dkm::kmeans_lloyd(clusterSourceData, params);
    }
    TS_STOP("update-kmeans");
    
    TS_START("update-clusterCentres");
    {
      // glm::vec4 w is age
      // add to clusterCentres from new clusters
      for (auto& cluster : std::get<0>(clusterResults)) {
        float x = cluster[0]; float y = cluster[1];
        auto it = std::find_if(clusterCentres.begin(),
                               clusterCentres.end(),
                               [x, y](const glm::vec4& p) {
          return ((std::abs(p.x-x) < SAME_CLUSTER_TOLERANCE) && (std::abs(p.y-y) < SAME_CLUSTER_TOLERANCE));
        });
        if (it == clusterCentres.end()) {
          // don't have this clusterCentre so make it
          clusterCentres.push_back(glm::vec4(x, y, 0.0, 1.0)); // start at age=1
//          introspection.addCircle(x, y, 5.0*1.0/Constants::WINDOW_WIDTH, ofColor::red, true, 60); // introspection: small red circle is new cluster centre
        } else {
          // existing cluster so add to its age to preserve it
          it->w++;
//          introspection.addCircle(it->x, it->y, 3.0*1.0/Constants::WINDOW_WIDTH, ofColor::yellow, false, 10); // introspection: small yellow circle is existing cluster centre that continues to exist
        }
      }
    }
    TS_STOP("update-clusterCentres");

    // draw circles around longer-lasting clusterCentres into fluid layer
    fluidSimulation.getFlowValuesFbo().getSource().begin();
    ofEnableBlendMode(OF_BLENDMODE_ADD);
    ofNoFill();
    ofSetColor(ofFloatColor(0.1, 0.1, 0.1, 0.7));
    for (auto& p: clusterCentres) {
      if (p.w < 5.0) continue;
      ofDrawCircle(p.x * Constants::FLUID_WIDTH, p.y * Constants::FLUID_HEIGHT, u * 100.0);
    }
    fluidSimulation.getFlowValuesFbo().getSource().end();

    TS_START("update-divider");
    bool dividerChanged = divider.update(clusterCentres);
    if (dividerChanged) {
      fluidSimulation.getFlowValuesFbo().getSource().begin();
      ofEnableBlendMode(OF_BLENDMODE_ALPHA);
      ofSetColor(ofFloatColor(1.0, 1.0, 1.0, 0.5));
      ofPushMatrix();
      ofScale(Constants::FLUID_WIDTH);
      const float lineWidth = 1.0 * 1.0 / Constants::FLUID_WIDTH;
      for(auto& divisionLine : divider.getDivisionLines()) {
        divisionLine.draw(lineWidth);
      }
      ofPopMatrix();
      fluidSimulation.getFlowValuesFbo().getSource().end();

      ofPixels frozenPixels;
      fluidSimulation.getFlowValuesFbo().getSource().getTexture().readToPixels(frozenPixels);
      frozenFluid.allocate(frozenPixels);
      
      // update mask
      maskFbo.begin();
      ofClear(ofColor::black);
      glEnable(GL_BLEND);
      glBlendEquation(GL_FUNC_ADD);
      glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_ZERO);
      for (auto& line : divider.getDivisionLines()) {
        ofPath path;
        path.moveTo(line.x1, line.y1);
        path.lineTo(line.x2, line.y2);
        path.lineTo(1.0, 0.0);
        path.lineTo(0.0, 0.0);
        path.close();
        path.scale(maskFbo.getWidth(), maskFbo.getHeight());
        path.setColor(ofColor::white);
        path.draw();
      }
      maskFbo.end();
    }
    TS_STOP("update-divider");
    
  } //isDataValid()

  {
    TS_START("decay-clusterCentres");
    // age all clusterCentres
    for (auto& p: clusterCentres) {
      p.w -= POINT_DECAY_RATE;
      if (p.w > 5.0) {
        introspection.addCircle(p.x, p.y, 10.0*1.0/Constants::WINDOW_WIDTH, ofColor::lightGreen, true, 60); // large lightGreen circle is long-lived clusterCentre
      } else {
        introspection.addCircle(p.x, p.y, 6.0*1.0/Constants::WINDOW_WIDTH, ofColor::darkOrange, true, 30); // small darkOrange circle is short-lived clusterCentre
      }
    }
    // delete decayed clusterCentres
    clusterCentres.erase(std::remove_if(
                                        clusterCentres.begin(),
                                        clusterCentres.end(),
                                        [](const glm::vec4& n) { return n.w <=0; }),
                         clusterCentres.end());
    TS_STOP("decay-clusterCentres");
  }
  
  // draw arcs around longer-lasting clusterCentres into foreground
  {
    foregroundFbo.begin();
    ofEnableBlendMode(OF_BLENDMODE_ALPHA);
    ofNoFill();
    for (auto& p: clusterCentres) {
      if (p.w < 4.0) continue;
      ofFloatColor somColor = somColorAt(p.x, p.y);
      ofFloatColor darkSomColor = somColor; darkSomColor.setBrightness(0.2); darkSomColor.setSaturation(1.0);
      ofSetColor(darkSomColor);
      ofPolyline path;
      float radius = std::fmod(p.w*4.0, 300);
      path.arc(p.x*foregroundFbo.getWidth(), p.y*foregroundFbo.getHeight(), radius, radius, -180*(u+p.x), 180.0*(v+p.y));
      path.draw();
    }
    foregroundFbo.end();
  }

  // draw divisions on foreground
  {
    foregroundFbo.begin();
    ofSetColor(ofFloatColor(0.0, 0.0, 0.0, 1.0));
    ofPushMatrix();
    ofScale(foregroundFbo.getWidth());
    const float lineWidth = 60.0 * 1.0 / foregroundFbo.getWidth();
    for(auto& divisionLine : divider.getDivisionLines()) {
      divisionLine.draw(lineWidth);
    }
    ofPopMatrix();
    foregroundFbo.end();
  }

  {
    TS_START("update-fluid-clusters");
    auto& clusterCentres = std::get<0>(clusterResults);
    for (auto& centre : clusterCentres) {
      float x = centre[0]; float y = centre[1];
      const float COL_FACTOR = 0.008;
      ofFloatColor color = somColorAt(x, y) * COL_FACTOR;
      color.a = 0.005 * ofRandom(1.0);
      FluidSimulation::Impulse impulse {
        { x * Constants::FLUID_WIDTH, y * Constants::FLUID_HEIGHT },
        Constants::FLUID_WIDTH * 0.085, // radius
        { 0.0, 0.0 }, // velocity
        0.0003, // radialVelocity
        color,
        10.0 // temperature
      };
      fluidSimulation.applyImpulse(impulse);
    }
    fluidSimulation.update();
    TS_STOP("update-fluid-clusters");
  }
}

ofFloatColor ofApp::somColorAt(float x, float y) const {
  double* somValue = som.getMapAt(x * Constants::SOM_WIDTH, y * Constants::SOM_HEIGHT);
  return ofFloatColor(somValue[0], somValue[1], somValue[2], 1.0);
}

//--------------------------------------------------------------
void ofApp::draw(){
  ofPushStyle();
  ofClear(0, 255);
  
  // fluid and frozen fluid
  {
    ofEnableBlendMode(OF_BLENDMODE_ALPHA);
    ofSetColor(ofFloatColor(1.0, 1.0, 1.0, 1.0));
    fluidSimulation.getFlowValuesFbo().getSource().draw(0.0, 0.0, Constants::WINDOW_WIDTH, Constants::WINDOW_HEIGHT);
//    maskShader.render(fluidSimulation.getFlowValuesFbo().getSource(), maskFbo, Constants::WINDOW_WIDTH, Constants::WINDOW_HEIGHT, true);
    if (frozenFluid.isAllocated()) {
      ofSetColor(ofFloatColor(1.0, 1.0, 1.0, 0.95));
      maskShader.render(frozenFluid, maskFbo, Constants::WINDOW_WIDTH, Constants::WINDOW_HEIGHT);
    }
//    ofSetColor(ofFloatColor(1.0, 1.0, 1.0, 0.3));
//    maskShader.render(fluidSimulation.getFlowValuesFbo().getSource(), maskFbo, Constants::WINDOW_WIDTH, Constants::WINDOW_HEIGHT);
  }
  
  // foreground
  {
    ofEnableBlendMode(OF_BLENDMODE_ALPHA);
    ofSetColor(ofFloatColor(1.0, 1.0, 1.0, 1.0));
    foregroundFbo.draw(0, 0, Constants::WINDOW_WIDTH, Constants::WINDOW_HEIGHT);
  }
  
  // introspection
  {
    TS_START("draw-introspection");
    ofPushView();
    ofEnableBlendMode(OF_BLENDMODE_ALPHA);
    ofScale(Constants::WINDOW_WIDTH); // drawing on Introspection is normalised so scale up
    introspection.draw();
    ofPopView();
    TS_STOP("draw-introspection");
  }
  
  // audio analysis graphs
  {
    ofPushView();
    ofEnableBlendMode(OF_BLENDMODE_ADD);
    float plotHeight = ofGetWindowHeight() / 4.0;
    audioDataPlotsPtr->drawPlots(ofGetWindowWidth(), plotHeight);
    audioDataSpectrumPlotsPtr->draw();
    ofPopView();
  }

  ofPopStyle();

  // gui
  if (guiVisible) gui.draw();

}

//--------------------------------------------------------------
void ofApp::exit(){

}

//--------------------------------------------------------------
void ofApp::keyPressed(int key){
  if (audioAnalysisClientPtr->keyPressed(key)) return;
  if (key == OF_KEY_TAB) guiVisible = not guiVisible;
  {
    float plotHeight = ofGetWindowHeight() / 4.0;
    int plotIndex = ofGetMouseY() / plotHeight;
    bool plotKeyPressed = audioDataPlotsPtr->keyPressed(key, plotIndex);
    bool spectrumPlotKeyPressed = audioDataSpectrumPlotsPtr->keyPressed(key);
    if (plotKeyPressed || spectrumPlotKeyPressed) return;
  }
  if (introspection.keyPressed(key)) return;
}

//--------------------------------------------------------------
void ofApp::keyReleased(int key){

}

//--------------------------------------------------------------
void ofApp::mouseMoved(int x, int y ){

}

//--------------------------------------------------------------
void ofApp::mouseDragged(int x, int y, int button){

}

//--------------------------------------------------------------
void ofApp::mousePressed(int x, int y, int button){

}

//--------------------------------------------------------------
void ofApp::mouseReleased(int x, int y, int button){

}

//--------------------------------------------------------------
void ofApp::mouseScrolled(int x, int y, float scrollX, float scrollY){

}

//--------------------------------------------------------------
void ofApp::mouseEntered(int x, int y){

}

//--------------------------------------------------------------
void ofApp::mouseExited(int x, int y){

}

//--------------------------------------------------------------
void ofApp::windowResized(int w, int h){

}

//--------------------------------------------------------------
void ofApp::gotMessage(ofMessage msg){

}

//--------------------------------------------------------------
void ofApp::dragEvent(ofDragInfo dragInfo){ 

}
