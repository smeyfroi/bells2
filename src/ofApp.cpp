#include "ofApp.h"
#include "Constants.h"
#include "Introspection.hpp"
#include "ofxTimeMeasurements.h"
#include "dkm.hpp"

//--------------------------------------------------------------
const size_t MAX_LINES = 21;
void ofApp::setup(){
  ofSetVerticalSync(false);
  ofEnableAlphaBlending();
  ofDisableArbTex(); // required for texture2D to work in GLSL, makes texture coords normalized
  ofSetFrameRate(Constants::FRAME_RATE);
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
const int CLUSTER_CENTRES = 11; //14;
const int CLUSTER_SAMPLES_MAX = 3000; // Note: 1600 raw samples per frame at 30fps
const float POINT_DECAY_RATE = 0.2;
const float POINT_TOLERANCE = 1.0/40.0;

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
  ofSetColor(ofFloatColor(0.0, 0.0, 0.0, 0.3));
  ofDrawRectangle(0.0, 0.0, foregroundFbo.getWidth(), foregroundFbo.getHeight());
  foregroundFbo.end();

  if (audioDataProcessorPtr->isDataValid()) {
    float s = audioDataProcessorPtr->getNormalisedScalarValue(ofxAudioAnalysisClient::AnalysisScalar::pitch, 200.0, 1800.0);// 700.0, 1300.0);
    float t = audioDataProcessorPtr->getNormalisedScalarValue(ofxAudioAnalysisClient::AnalysisScalar::rootMeanSquare, 0.0, 4600.0); ////400.0, 4000.0, false);
    float u = audioDataProcessorPtr->getNormalisedScalarValue(ofxAudioAnalysisClient::AnalysisScalar::spectralKurtosis, 0.0, 25.0);
    float v = audioDataProcessorPtr->getNormalisedScalarValue(ofxAudioAnalysisClient::AnalysisScalar::spectralCentroid, 0.4, 6.0);

    ofFloatColor somColor = somColorAt(s, t);
    foregroundFbo.begin();
    ofEnableBlendMode(OF_BLENDMODE_ALPHA);
    ofSetColor(ofColor::black);
//    ofSetColor(somColor);
    ofDrawCircle(s*foregroundFbo.getWidth(), t*foregroundFbo.getHeight(), 3.0);
    foregroundFbo.end();
    fluidSimulation.getFlowValuesFbo().getSource().begin();
    ofEnableBlendMode(OF_BLENDMODE_ALPHA);
    ofSetColor(ofColor::black);
//    ofSetColor(somColor);
    ofDrawCircle(s*Constants::FLUID_WIDTH, t*Constants::FLUID_HEIGHT, 2.0);
    fluidSimulation.getFlowValuesFbo().getSource().end();
    
    TS_START("update-kmeans");
    if (clusterSourceData.size() > CLUSTER_SAMPLES_MAX) {
      clusterSourceData.erase(clusterSourceData.end()-CLUSTER_SAMPLES_MAX, clusterSourceData.end());
    }
    clusterSourceData.push_back({ s, t });
    introspection.addCircle(s, t, 1.0/Constants::WINDOW_WIDTH*2.0, ofColor::darkRed, true); // introspection: red outline for raw cluster
    if (clusterSourceData.size() > CLUSTER_CENTRES) {
      dkm::clustering_parameters<float> params(CLUSTER_CENTRES);
      params.set_random_seed(1000); // keep clusters stable
      clusterResults = dkm::kmeans_lloyd(clusterSourceData, params);
    }
    TS_STOP("update-kmeans");
    
    TS_START("update-som");
    {
      double instance[3] = { static_cast<double>(s), static_cast<double>(t), static_cast<double>(v) };
      som.updateMap(instance);
    }
    TS_STOP("update-som");
    
    TS_START("update-points");
    {
      // w is age
      // add to points from clusters
      for (auto& cluster : std::get<0>(clusterResults)) {
        float x = cluster[0]; float y = cluster[1];
        auto it = std::find_if(points.begin(),
                               points.end(),
                               [x, y](const glm::vec4& p) {
          return ((std::abs(p.x-x) < POINT_TOLERANCE) && (std::abs(p.y-y) < POINT_TOLERANCE));
        });
        if (it == points.end()) {
          points.push_back(glm::vec4(x, y, 0.0, 1.0));
          introspection.addCircle(x, y, 1.0/Constants::WINDOW_WIDTH*2.0, ofFloatColor(0.0, 0.0, 1.0, 1.0), true, 360); // introspection: blue is new point
        } else {
          it->w++;
          introspection.addCircle(x, y, 1.0/Constants::WINDOW_WIDTH*2.0, ofFloatColor(1.0, 1.0, 0.0, 0.1), false, 600); // introspection: yellow is existing point
        }
      }
      // age all points
      for (auto& p: points) {
        p.w -= POINT_DECAY_RATE;
        if (p.w > 10.0) introspection.addCircle(p.x, p.y, 1.0/Constants::WINDOW_WIDTH*10.0, ofFloatColor(0.2, 1.0, 0.2, 1.0), true, 600); // green filled: longlived savedNote
      }
      // delete expired points
      points.erase(std::remove_if(points.begin(),
                                  points.end(),
                                  [](const glm::vec4& n) { return n.w <=0; }),
                   points.end());
    }
    TS_STOP("update-points");

    // draw circles around older points into fluid layer
    for (auto& p: points) {
      if (p.w < 5.0) continue;
      fluidSimulation.getFlowValuesFbo().getSource().begin();
      ofEnableBlendMode(OF_BLENDMODE_ADD);
      ofNoFill();
      ofSetColor(ofFloatColor(0.1, 0.1, 0.1, 0.7));
      ofDrawCircle(p.x * Constants::FLUID_WIDTH, p.y * Constants::FLUID_HEIGHT, u * 100.0);
      fluidSimulation.getFlowValuesFbo().getSource().end();
    }

    // For lines:
    // - find line ends that are still valid and leave them
    // - find line ends that are invalid and find a nearby point to connect
    // -   for invalid ends that have no replacement, remove the line
    // - add new lines to fill quota

    // Also: make lines that are close to the edges of 0.0,1.0
    
    // Keep an age attached to lines so the drawn line and mask can fade in and out
    
    TS_START("update-lines");
    bool dividerChanged = divider.update(points);
    if (dividerChanged) {
      fluidSimulation.getFlowValuesFbo().getSource().begin();
      ofEnableBlendMode(OF_BLENDMODE_ALPHA);
      ofSetColor(ofFloatColor(1.0, 1.0, 1.0, 0.5));
      for(auto& divisionLine : divider.getDivisionLines()) {
//        if (divisionLine.age < 1) {
          ofDrawLine(divisionLine.x1*Constants::FLUID_WIDTH, divisionLine.y1*Constants::FLUID_HEIGHT, divisionLine.x2*Constants::FLUID_WIDTH, divisionLine.y2*Constants::FLUID_HEIGHT);
//        }
      }
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
    TS_STOP("update-lines");
  } //isDataValid()  
  
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
