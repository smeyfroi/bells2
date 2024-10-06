#pragma once

#include "ofMain.h"
#include "ofxGui.h"
#include "ofxSelfOrganizingMap.h"
#include "ofxAudioAnalysisClient.h"
#include "ofxAudioData.h"
#include "FluidSimulation.h"
#include "Introspection.hpp"
#include "Divider.hpp"
#include "MaskShader.h"
#include "Plot.hpp"

class ofApp : public ofBaseApp{
  
public:
  void setup() override;
  void update() override;
  void draw() override;
  void exit() override;
  
  void keyPressed(int key) override;
  void keyReleased(int key) override;
  void mouseMoved(int x, int y ) override;
  void mouseDragged(int x, int y, int button) override;
  void mousePressed(int x, int y, int button) override;
  void mouseReleased(int x, int y, int button) override;
  void mouseScrolled(int x, int y, float scrollX, float scrollY) override;
  void mouseEntered(int x, int y) override;
  void mouseExited(int x, int y) override;
  void windowResized(int w, int h) override;
  void dragEvent(ofDragInfo dragInfo) override;
  void gotMessage(ofMessage msg) override;
  
private:
  // bells
    std::shared_ptr<ofxAudioAnalysisClient::FileClient> audioAnalysisClientPtr {
      std::make_shared<ofxAudioAnalysisClient::FileClient>("Jam-20240517-155805463/____-80_41_155_x_22141-0-1.wav",
                                                           "Jam-20240517-155805463/____-80_41_155_x_22141.oscs") };
  // nightsong
//    std::shared_ptr<ofxAudioAnalysisClient::FileClient> audioAnalysisClientPtr {
//      std::make_shared<ofxAudioAnalysisClient::FileClient>("Jam-20240402-094851837/____-46_137_90_x_22141-0-1.wav",
//                                                           "Jam-20240402-094851837/____-46_137_90_x_22141.oscs") };
  // treganna
//  std::shared_ptr<ofxAudioAnalysisClient::FileClient> audioAnalysisClientPtr {
//    std::make_shared<ofxAudioAnalysisClient::FileClient>("Jam-20240719-093508910/____-92_9_186_x_22141-0-1.wav",
//                                                         "Jam-20240719-093508910/____-92_9_186_x_22141.oscs") };
  
  std::shared_ptr<ofxAudioData::Processor> audioDataProcessorPtr { std::make_shared<ofxAudioData::Processor>(audioAnalysisClientPtr) };
  std::shared_ptr<ofxAudioData::Plots> audioDataPlotsPtr { std::make_shared<ofxAudioData::Plots>(audioDataProcessorPtr) };
  std::shared_ptr<ofxAudioData::SpectrumPlots> audioDataSpectrumPlotsPtr { std::make_shared<ofxAudioData::SpectrumPlots>(audioDataProcessorPtr) };
  
  ofxSelfOrganizingMap som;
  ofFloatColor somColorAt(float x, float y) const;
  
  FluidSimulation fluidSimulation;
  ofTexture frozenFluid;
  
  ofFbo foregroundLinesFbo;
  ofFbo foregroundFbo;
  ofFbo foregroundMaskFbo;
  MaskShader maskShader;
  
  std::vector<std::array<float, 2>> recentNoteXYs;
  std::tuple<std::vector<std::array<float, 2>>, std::vector<uint32_t>> clusterResults;
  std::vector<glm::vec4> clusterCentres;
  
  Divider divider { 7 };
  
  Plot plot;
  bool plotVisible;
  
  Introspection introspection; // we add things to this in normalised coords
  
  bool guiVisible { false };
  ofxPanel gui;
  ofParameterGroup parameters;
  ofParameter<float> validLowerRmsParameter { "validLowerRms", 300.0, 100.0, 5000.0 };
  ofParameter<float> validLowerPitchParameter { "validLowerPitch", 50.0, 50.0, 8000.0 };
  ofParameter<float> validUpperPitchParameter { "validUpperPitch", 5000.0, 50.0, 8000.0 };
  
  //  const int NUM_CLUSTER_CENTRES = 18;
  //  const int CLUSTER_SOURCE_SAMPLES_MAX = 3000; // Note: 1600 raw samples per frame at 30fps
  //  const float POINT_DECAY_RATE = 0.3;
  //  const float SAME_CLUSTER_TOLERANCE = 1.0/10.0;
  // fade foreground lines
  // fade foreground
  //  float s = audioDataProcessorPtr->getNormalisedScalarValue(ofxAudioAnalysisClient::AnalysisScalar::pitch, 200.0, 1800.0);// 700.0, 1300.0);
  //  float t = audioDataProcessorPtr->getNormalisedScalarValue(ofxAudioAnalysisClient::AnalysisScalar::rootMeanSquare, 0.0, 4600.0); ////400.0, 4000.0, false);
  //  float u = audioDataProcessorPtr->getNormalisedScalarValue(ofxAudioAnalysisClient::AnalysisScalar::spectralKurtosis, 0.0, 25.0);
  //  float v = audioDataProcessorPtr->getNormalisedScalarValue(ofxAudioAnalysisClient::AnalysisScalar::spectralCentroid, 0.4, 6.0);
  // find some number of note clusters
  //  for (int i = 0; i < 7; i++) {
  // pick a number of additional random notes and keep if from this cluster
  //  for(int i = 0; i < 13; i++) {
  // draw extended outlines in the foreground (saving them for redrawing into fluid)
  //  float width = 15 * 1.0 / foregroundLinesFbo.getWidth();
  // redraw extended lines into the fluid layer
  //  float width = 1.0 * 1.0 / Constants::FLUID_WIDTH;
  // draw circles around longer-lasting clusterCentres into fluid layer
  //  if (p.w < 5.0) continue;
  //  ofDrawCircle(p.x * Constants::FLUID_WIDTH, p.y * Constants::FLUID_HEIGHT, u * 100.0);
  //  TS_START("update-divider");
  //  const float lineWidth = 1.0 * 1.0 / Constants::FLUID_WIDTH;
  //  TS_START("decay-clusterCentres");
  //  if (p.w > 5.0) {
  // draw divisions on foreground
  //  const float lineWidth = 80.0 * 1.0 / foregroundLinesFbo.getWidth();
  // draw arcs around longer-lasting clusterCentres into foreground
  //  if (p.w < 4.0) continue;
  //  float radius = std::fmod(p.w*5.0, 480);
  // ^^^ similar for plots
  //  FluidSimulation::Impulse impulse {
  //    { x * Constants::FLUID_WIDTH, y * Constants::FLUID_HEIGHT },
  //    Constants::FLUID_WIDTH * 0.085, // radius
  //    { 0.0, 0.0 }, // velocity
  //    0.0003, // radialVelocity
  //    color,
  //    10.0 // temperature
  //  };
  
  // All the plot lifetimes
  
};
