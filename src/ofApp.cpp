#include "ofApp.h"
#include "Constants.h"
#include "Introspection.hpp"
#include "ofxTimeMeasurements.h"
#include "dkm.hpp"

//--------------------------------------------------------------
const size_t MAX_LINES = 3;
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
  
  lines.resize(MAX_LINES);

  parameters.add(fluidSimulation.getParameterGroup());
  gui.setup(parameters);

  ofxTimeMeasurements::instance()->setEnabled(true);
}

//--------------------------------------------------------------
const int CLUSTER_CENTRES = 11; //14;
const int CLUSTER_SAMPLES_MAX = 3000; // Note: 1600 raw samples per frame at 30fps
const float POINT_DECAY_RATE = 0.2;
const float POINT_TOLERANCE = 1.0/40.0;

// y = mx + b
float yForLineAtX(float x, float x1, float y1, float x2, float y2) {
  float m = (y2 - y1) / (x2 - x1);
  float b = y1 - (m * x1);
  return m * x + b;
}
//float xForLineAtY(float y, float x1, float y1, float x2, float y2) {
//  float m = (x2 - x1) / (y2 - y1);
//  float b = y1 - (m * x1);
//  return (y - b) / m;
//}

// assume normalised coords
std::tuple<glm::vec2, glm::vec2> extendedLine(float x1, float y1, float x2, float y2) {
  return std::tuple<glm::vec2, glm::vec2> { {0.0, yForLineAtX(0.0, x1, y1, x2, y2)}, {1.0, yForLineAtX(1.0, x1, y1, x2, y2)} };
}

void ofApp::update() {
  
  TS_START("update-introspection");
  introspection.update();
  TS_STOP("update-introspection");

  TS_START("update-audoanalysis");
  audioDataProcessorPtr->update();
  TS_STOP("update-audoanalysis");

  if (audioDataProcessorPtr->isDataValid()) {
    float s = audioDataProcessorPtr->getNormalisedScalarValue(ofxAudioAnalysisClient::AnalysisScalar::pitch, 200.0, 1800.0);// 700.0, 1300.0);
    float t = audioDataProcessorPtr->getNormalisedScalarValue(ofxAudioAnalysisClient::AnalysisScalar::rootMeanSquare, 0.0, 4600.0); ////400.0, 4000.0, false);
    float u = audioDataProcessorPtr->getNormalisedScalarValue(ofxAudioAnalysisClient::AnalysisScalar::spectralKurtosis, 0.0, 25.0);
    float v = audioDataProcessorPtr->getNormalisedScalarValue(ofxAudioAnalysisClient::AnalysisScalar::spectralCentroid, 0.4, 6.0);
    
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
    
    // temp draw on the fluid values layer: needs to be a shader that paints a circlar arc edge with smooth edges at some radius
    for (auto& p: points) {
      if (p.w < 10.0) continue;
      fluidSimulation.getFlowValuesFbo().getSource().begin();
      ofEnableBlendMode(OF_BLENDMODE_ADD);
      ofNoFill();
      ofSetColor(ofFloatColor(0.1, 0.1, 0.1, 0.3));
      ofDrawCircle(p.x * Constants::FLUID_WIDTH, p.y * Constants::FLUID_HEIGHT, u * 100.0); // p.w);
      fluidSimulation.getFlowValuesFbo().getSource().end();
    }

    TS_START("update-lines");
    // reverse sort points by age
    std::sort(points.begin(),
              points.end(),
              [](const glm::vec4& a, const glm::vec4& b) { return a.w > b.w; });
    if (points.size() > 2*MAX_LINES) {
      for (size_t i = 0; i < 2*MAX_LINES; i+=2) {
        glm::vec4 p1 = points[i*2]; glm::vec4 p2 = points[i*2+1];
//        introspection.addCircle(p1.x, p1.y, 1.0/Constants::WINDOW_WIDTH*20.0, ofFloatColor(1.0, 0.0, 1.0, 1.0), false);
//        introspection.addCircle(p2.x, p2.y, 1.0/Constants::WINDOW_WIDTH*15.0, ofFloatColor(1.0, 1.0, 0.0, 1.0), false);
        auto line = extendedLine(p1.x, p1.y, p2.x, p2.y);
        lines[i] = line;
        glm::vec2 ls = std::get<0>(line); glm::vec2 le = std::get<1>(line);
//        introspection.addCircle(ls.x, ls.y, 1.0/Constants::WINDOW_WIDTH*20.0, ofFloatColor(0.6, 0.6, 0.6, 1.0), false);
//        introspection.addCircle(le.x, le.y, 1.0/Constants::WINDOW_WIDTH*15.0, ofFloatColor(0.6, 0.6, 0.6, 1.0), false);
        introspection.addLine(ls.x, ls.y, le.x, le.y, ofFloatColor(1.0, 1.0, 1.0, 1.0));
      }
    }
    TS_STOP("update-lines");

  } //isDataValid()
  
  {
    TS_START("update-fluid");
    auto& clusterCentres = std::get<0>(clusterResults);
    for (auto& centre : clusterCentres) {
      float x = centre[0]; float y = centre[1];
      double* somValue = som.getMapAt(x * Constants::SOM_WIDTH, y * Constants::SOM_HEIGHT);
      const float COL_FACTOR = 0.008;
      ofFloatColor color = ofFloatColor(somValue[0], somValue[1], somValue[2], 0.005) * COL_FACTOR;
      FluidSimulation::Impulse impulse {
        { x * Constants::FLUID_WIDTH, y * Constants::FLUID_HEIGHT },
        Constants::FLUID_WIDTH * 0.08, // radius
        { 0.0, 0.0 }, // velocity
        0.0002, // radialVelocity
        color,
        10.0 // temperature
      };
      fluidSimulation.applyImpulse(impulse);
    }
    fluidSimulation.update();
    TS_STOP("update-fluid");
  }

}

//--------------------------------------------------------------
void ofApp::draw(){
  ofPushStyle();
  ofClear(0, 255);
  
  {
    ofPushStyle();
    ofEnableBlendMode(OF_BLENDMODE_ALPHA);
    fluidSimulation.draw(0, 0, Constants::WINDOW_WIDTH, Constants::WINDOW_HEIGHT);
    ofPopStyle();
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
