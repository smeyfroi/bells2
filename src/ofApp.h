#pragma once

#include "ofMain.h"
#include "ofxGui.h"
#include "ofxSelfOrganizingMap.h"
#include "ofxAudioAnalysisClient.h"
#include "ofxAudioData.h"
#include "FluidSimulation.h"
#include "Introspection.hpp"
#include "MaskShader.h"

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
  std::shared_ptr<ofxAudioAnalysisClient::FileClient> audioAnalysisClientPtr { std::make_shared<ofxAudioAnalysisClient::FileClient>("Jam-20240517-155805463") }; // bells
  std::shared_ptr<ofxAudioData::Processor> audioDataProcessorPtr { std::make_shared<ofxAudioData::Processor>(audioAnalysisClientPtr) };
  std::shared_ptr<ofxAudioData::Plots> audioDataPlotsPtr { std::make_shared<ofxAudioData::Plots>(audioDataProcessorPtr) };
  std::shared_ptr<ofxAudioData::SpectrumPlots> audioDataSpectrumPlotsPtr { std::make_shared<ofxAudioData::SpectrumPlots>(audioDataProcessorPtr) };

  ofxSelfOrganizingMap som;

  FluidSimulation fluidSimulation;
  ofTexture frozenFluid;
  MaskShader maskShader;
  
  std::vector<glm::vec4> points;
  std::vector<std::tuple<glm::vec2, glm::vec2>> lines;
  ofFbo maskFbo;

  std::vector<std::array<float, 2>> clusterSourceData;
  std::tuple<std::vector<std::array<float, 2>>, std::vector<uint32_t>> clusterResults;
  
  Introspection introspection; // we add things to this in normalised coords

  bool guiVisible { false };
  ofxPanel gui;
  ofParameterGroup parameters;

};
