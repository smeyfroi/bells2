#include "ofApp.h"
#include "Constants.h"
#include "Introspection.hpp"
#include "ofxTimeMeasurements.h"
#include "dkm.hpp"

const int DEFAULT_CIRCLE_RESOLUTION = 32;
const int FOREGROUND_CIRCLE_RESOLUTION = 96;

//--------------------------------------------------------------
void ofApp::setup(){
  ofSetVerticalSync(false);
  ofEnableAlphaBlending();
  ofDisableArbTex(); // required for texture2D to work in GLSL, makes texture coords normalized
  ofSetFrameRate(Constants::FRAME_RATE);
  ofSetCircleResolution(DEFAULT_CIRCLE_RESOLUTION);
  TIME_SAMPLE_SET_FRAMERATE(Constants::FRAME_RATE);

  double minInstance[3] = { 0.0, 0.0, 0.0 };
  double maxInstance[3] = { 1.0, 1.0, 1.0 };
  som.setFeaturesRange(3, minInstance, maxInstance);
  som.setMapSize(Constants::SOM_WIDTH, Constants::SOM_HEIGHT); // can go to 3 dimensions
  som.setInitialLearningRate(0.1);
  som.setNumIterations(3000);
  som.setup();
  
  fluidSimulation.setup({ Constants::FLUID_WIDTH, Constants::FLUID_HEIGHT });
  
  foregroundLinesFbo.allocate(Constants::CANVAS_WIDTH, Constants::CANVAS_HEIGHT, GL_RGBA32F);
  foregroundLinesFbo.clearColorBuffer(ofFloatColor(0.0, 0.0, 0.0, 0.0));
  foregroundFbo.allocate(Constants::CANVAS_WIDTH, Constants::CANVAS_HEIGHT, GL_RGBA32F);
  foregroundFbo.clearColorBuffer(ofFloatColor(0.0, 0.0, 0.0, 0.0));
  foregroundMaskFbo.allocate(Constants::CANVAS_WIDTH, Constants::CANVAS_HEIGHT, GL_R8);
  maskShader.load();

  auto fluidParameterGroup = fluidSimulation.getParameterGroup();
  fluidParameterGroup.getFloat("dt").set(0.03);
  fluidParameterGroup.getFloat("vorticity").set(15.0);
  fluidParameterGroup.getFloat("value:dissipation").set(0.9975);
  fluidParameterGroup.getFloat("velocity:dissipation").set(0.9999);
  fluidParameterGroup.getInt("pressure:iterations").set(22);
  parameters.add(fluidParameterGroup);
  
  parameters.add(validLowerRmsParameter);
  parameters.add(validLowerPitchParameter);
  parameters.add(validUpperPitchParameter);
  
  gui.setup(parameters);
  
  plotVisible = false;

  ofxTimeMeasurements::instance()->setEnabled(false);
}

//--------------------------------------------------------------
const int NUM_CLUSTER_CENTRES = 18;
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

  // fade foreground lines
  foregroundLinesFbo.begin();
  ofEnableBlendMode(OF_BLENDMODE_ALPHA);
  ofSetColor(ofFloatColor(0.0, 0.0, 0.0, 0.07));
  ofDrawRectangle(0.0, 0.0, foregroundLinesFbo.getWidth(), foregroundLinesFbo.getHeight());
  foregroundLinesFbo.end();

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
  
  std::vector<ofxAudioData::ValiditySpec> sampleValiditySpecs {
    {ofxAudioAnalysisClient::AnalysisScalar::rootMeanSquare, false, validLowerRmsParameter},
    {ofxAudioAnalysisClient::AnalysisScalar::pitch, false, validLowerPitchParameter},
    {ofxAudioAnalysisClient::AnalysisScalar::pitch, true, validUpperPitchParameter}
  };

  if (audioDataProcessorPtr->isDataValid(sampleValiditySpecs)) {
    TS_START("update-som");
    {
      double instance[3] = { static_cast<double>(s), static_cast<double>(t), static_cast<double>(v) };
      som.updateMap(instance);
    }
    TS_STOP("update-som");

    ofFloatColor somColor = somColorAt(s, t);
    ofFloatColor darkSomColor = somColor; darkSomColor.setBrightness(0.25); darkSomColor.setSaturation(1.0);

    // Draw foreground mark for raw audio data sample in darkened SOM color
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

    // Maintain recent notes
    if (recentNoteXYs.size() > CLUSTER_SOURCE_SAMPLES_MAX) {
      recentNoteXYs.erase(recentNoteXYs.end() - CLUSTER_SOURCE_SAMPLES_MAX, recentNoteXYs.end());
    }
    recentNoteXYs.push_back({ s, t });
    introspection.addCircle(s, t, 1.0/Constants::WINDOW_WIDTH*5.0, ofColor::grey, true, 30); // introspection: small grey circle for new raw source sample

    TS_START("update-kmeans");
    if (recentNoteXYs.size() > NUM_CLUSTER_CENTRES) {
      dkm::clustering_parameters<float> params(NUM_CLUSTER_CENTRES);
      params.set_random_seed(1000); // keep clusters stable
      clusterResults = dkm::kmeans_lloyd(recentNoteXYs, params);
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
    
    // Make fine structure from some recent notes
    const std::vector<uint32_t>& recentNoteXYIds = std::get<1>(clusterResults);
    if (recentNoteXYs.size() > 70) {
      
      // find some number of note clusters
      for (int i = 0; i < 7; i++) {
        
        std::vector<uint32_t> sameClusterNoteIds; // collect note IDs all from the same cluster
        size_t id = ofRandom(recentNoteXYIds.size()); // start with a random note TODO: don't use ofRandom
        sameClusterNoteIds.push_back(id);
        uint32_t clusterId = recentNoteXYIds[id];
        
        // pick a number of additional random notes and keep if from this cluster
        for(int i = 0; i < 13; i++) {
          id = ofRandom(recentNoteXYIds.size());
          if (recentNoteXYIds[id] == clusterId) {
            sameClusterNoteIds.push_back(id);
          }
        }

        // if we found enough related notes then draw something
        if (sameClusterNoteIds.size() > 2) {
          
          // make path from notes in normalised coords
          ofPath path;
          for (uint32_t id : sameClusterNoteIds) {
            const auto& note = recentNoteXYs[id];
            path.lineTo(note[0], note[1]);
          }
          path.close();

          // paint into the fluid layer
          fluidSimulation.getFlowValuesFbo().getSource().begin();
          {
            ofPath fluidPath = path;
            fluidPath.scale(Constants::FLUID_WIDTH, Constants::FLUID_HEIGHT);
            ofEnableBlendMode(OF_BLENDMODE_ALPHA);
            ofFloatColor fillColor = somColor;
            fillColor.a = 0.3;
            fluidPath.setColor(fillColor);
            fluidPath.setFilled(true);
            fluidPath.draw();
          }
          fluidSimulation.getFlowValuesFbo().getSource().end();

          // find normalised path bounds
          ofRectangle pathBounds;
          for (const auto& polyline : path.getOutline()) {
            pathBounds = pathBounds.getUnion(polyline.getBoundingBox());
          }

          // translate it to start from (0, 0)
          path.translate({ -pathBounds.x, -pathBounds.y });
          
          // scale up to some limit
          constexpr float MAX_SCALE = 3.0;
          float scaleX = std::fminf(MAX_SCALE, 1.0 / pathBounds.width);
          float scaleY = std::fminf(MAX_SCALE, 1.0 / pathBounds.height);
          path.scale(scaleX, scaleY); // FIXME: end up with asymmetric scaling

          // and make a mask from the path
          foregroundMaskFbo.begin();
          {
            ofClear(0, 255);
            ofSetColor(255);
            path.setFilled(true);
            ofPushMatrix();
            ofScale(foregroundMaskFbo.getWidth(), foregroundMaskFbo.getHeight());
            path.draw();
            ofPushMatrix();
          }
          foregroundMaskFbo.end();

          // draw a reduced version of the foreground into the mask
          if (frozenFluid.isAllocated()) {
            foregroundFbo.begin();
            {
              ofEnableBlendMode(OF_BLENDMODE_ADD);
              ofSetColor(216);
              ofPushMatrix();
              ofScale(1.0 / scaleX, 1.0 / scaleY);
              ofTranslate((pathBounds.x) * foregroundFbo.getWidth(), (pathBounds.y) * foregroundFbo.getHeight());
              maskShader.render(frozenFluid, foregroundMaskFbo, foregroundFbo.getWidth(), foregroundFbo.getHeight());
              ofPopMatrix();
            }
            foregroundFbo.end();
          }
          
          // draw extended outlines in the foreground (saving them for redrawing into fluid)
          std::vector<std::tuple<glm::vec2, glm::vec2>> extendedLines;
          float width = 15 * 1.0 / foregroundLinesFbo.getWidth();
          foregroundLinesFbo.begin();
          ofEnableBlendMode(OF_BLENDMODE_ALPHA);
          ofSetColor(ofColor::black);
          ofPushMatrix();
          ofScale(foregroundFbo.getWidth(), foregroundFbo.getHeight());
          {
            for(auto iter = sameClusterNoteIds.begin(); iter < sameClusterNoteIds.end(); iter++) {
              auto id1 = *iter;
              const auto& note1 = recentNoteXYs[id1];
              float x1 = note1[0]; float y1 = note1[1];
              uint32_t id2;
              if (iter == sameClusterNoteIds.end() - 1) {
                id2 = *sameClusterNoteIds.begin();
              } else {
                id2 = *(iter + 1);
              }
              const auto& note2 = recentNoteXYs[id2];
              float x2 = note2[0]; float y2 = note2[1];
              if (note1 == note2) continue;
              auto line = divider.extendedLineEnclosedByDivider(x1, y1, x2, y2);
              extendedLines.push_back(line);
              glm::vec2 p1 = std::get<0>(line); glm::vec2 p2 = std::get<1>(line);
              ofPushMatrix();
              ofTranslate(p1.x, p1.y);
              ofRotateRad(std::atan2((p2.y-p1.y), (p2.x-p1.x)));
              ofDrawRectangle(0.0, -width/2.0, ofDist(p1.x, p1.y, p2.x, p2.y), width);
              ofPopMatrix();
            }
          }
          ofPopMatrix();
          foregroundLinesFbo.end();
          
          // plot connected clustered notes
          {
            uint32_t lastNoteId = *(sameClusterNoteIds.end() - 1);
            auto lastNote = recentNoteXYs[lastNoteId];
            for (uint32_t id : sameClusterNoteIds) {
              const auto& note = recentNoteXYs[id];
              auto linePtr = std::unique_ptr<Shape>(new LineShape(lastNote[0], lastNote[1], note[0], note[1], ofColor::red, 50));
              plot.addShapePtr(std::move(linePtr));
              lastNote = note;
            }
          }

          // plot extended lines
          {
            for (const auto& line : extendedLines) {
              glm::vec2 p1 = std::get<0>(line); glm::vec2 p2 = std::get<1>(line);
              auto linePtr = std::unique_ptr<Shape>(new LineShape(p1.x, p1.y, p2.x, p2.y, ofColor::green, 20));
              plot.addShapePtr(std::move(linePtr));
            }
          }
          
          // redraw extended lines into the fluid layer
          fluidSimulation.getFlowValuesFbo().getSource().begin();
          ofEnableBlendMode(OF_BLENDMODE_ALPHA);
          ofSetColor(ofFloatColor(1.0, 1.0, 1.0, 0.7));
          ofPushMatrix();
          ofScale(Constants::FLUID_WIDTH, Constants::FLUID_HEIGHT);
          {
            float width = 1.0 * 1.0 / Constants::FLUID_WIDTH;
            for (const auto& line : extendedLines) {
              glm::vec2 p1 = std::get<0>(line); glm::vec2 p2 = std::get<1>(line);
              ofPushMatrix();
              ofTranslate(p1.x, p1.y);
              ofRotateRad(std::atan2((p2.y-p1.y), (p2.x-p1.x)));
              ofDrawRectangle(0.0, -width/2.0, ofDist(p1.x, p1.y, p2.x, p2.y), width);
              ofPopMatrix();
            }
          }
          ofPopMatrix();
          fluidSimulation.getFlowValuesFbo().getSource().end();
        }
      }
    }

    // draw circles around longer-lasting clusterCentres into fluid layer
    fluidSimulation.getFlowValuesFbo().getSource().begin();
    ofEnableBlendMode(OF_BLENDMODE_ADD);
    ofNoFill();
    ofSetColor(ofFloatColor(0.1, 0.1, 0.1, 0.6));
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
      ofSetColor(ofFloatColor(1.0, 1.0, 1.0, 1.0));
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
    clusterCentres.erase(std::remove_if(clusterCentres.begin(),
                                        clusterCentres.end(),
                                        [](const glm::vec4& n) { return n.w <=0; }),
                         clusterCentres.end());
    TS_STOP("decay-clusterCentres");
  }
  
  // draw divisions on foreground
  {
    foregroundLinesFbo.begin();
    ofSetColor(ofFloatColor(0.0, 0.0, 0.0, 1.0));
    ofPushMatrix();
    ofScale(foregroundLinesFbo.getWidth(), foregroundLinesFbo.getHeight());
    const float lineWidth = 80.0 * 1.0 / foregroundLinesFbo.getWidth();
    for(auto& divisionLine : divider.getDivisionLines()) {
      divisionLine.draw(lineWidth);
    }
    ofPopMatrix();
    foregroundLinesFbo.end();
  }

  // draw arcs around longer-lasting clusterCentres into foreground
  {
    foregroundFbo.begin();
    ofEnableBlendMode(OF_BLENDMODE_ALPHA);
    ofNoFill();
    for (auto& p: clusterCentres) {
      if (p.w < 4.0) continue;
      ofFloatColor somColor = somColorAt(p.x, p.y);
      ofFloatColor darkSomColor = somColor; darkSomColor.setBrightness(0.7); darkSomColor.setSaturation(1.0);
      darkSomColor.a = 0.7;
      ofSetColor(darkSomColor);
      ofPolyline path;
      float radius = std::fmod(p.w*5.0, 480);
      path.arc(p.x*foregroundFbo.getWidth(), p.y*foregroundFbo.getHeight(), radius, radius, -180.0*(u+p.x), 180.0*(v+p.y), FOREGROUND_CIRCLE_RESOLUTION);
      path.draw();
    }
    foregroundFbo.end();
  }
  
  // plot arcs around longer-lasting clusterCentres
  {
    for (auto& p: clusterCentres) {
      if (p.w < 4.0) continue;
      float radius = std::fmod(p.w*5.0/Constants::CANVAS_WIDTH, 480.0/Constants::CANVAS_WIDTH);
      auto arcPtr = std::unique_ptr<Shape>(new ArcShape(p.x, p.y, radius, -180.0*(u+p.x), 180.0*(v+p.y), ofColor::blue, 30));
      plot.addShapePtr(std::move(arcPtr));
    }
  }
  
  // plot divisions
  {
    for(auto& l : divider.getDivisionLines()) {
      auto linePtr = std::unique_ptr<Shape>(new LineShape(l.x1, l.y1, l.x2, l.y2, ofColor::black, 10));
      plot.addShapePtr(std::move(linePtr));
    }
  }

  plot.update();

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
        1.0 // temperature
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
void ofApp::draw() {
  if (plotVisible) {
    ofClear(255, 255);
    ofPushMatrix();
    plot.draw(ofGetWindowWidth());
    ofPopMatrix();
    return;
  }
  
  ofPushStyle();
  ofClear(0, 255);
  
  // fluid
  {
    ofEnableBlendMode(OF_BLENDMODE_ALPHA);
    ofSetColor(ofFloatColor(1.0, 1.0, 1.0, 1.0));
    fluidSimulation.getFlowValuesFbo().getSource().draw(0.0, 0.0, Constants::WINDOW_WIDTH, Constants::WINDOW_HEIGHT);
  }
  
  // foreground
  {
    ofEnableBlendMode(OF_BLENDMODE_ALPHA);
    ofSetColor(ofFloatColor(1.0, 1.0, 1.0, 1.0));
    foregroundFbo.draw(0, 0, Constants::WINDOW_WIDTH, Constants::WINDOW_HEIGHT);
  }
  
  // foreground lines
  {
    ofEnableBlendMode(OF_BLENDMODE_ALPHA);
    ofSetColor(ofFloatColor(1.0, 1.0, 1.0, 1.0));
    foregroundLinesFbo.draw(0, 0, Constants::WINDOW_WIDTH, Constants::WINDOW_HEIGHT);
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
  if (key == 'S') {
    ofFbo compositeFbo;
    compositeFbo.allocate(Constants::CANVAS_WIDTH, Constants::CANVAS_HEIGHT, GL_RGB);
    compositeFbo.begin();
    {
      // blackground
      ofClear(0, 255);

      // fluid
      {
        ofEnableBlendMode(OF_BLENDMODE_ALPHA);
        ofSetColor(ofFloatColor(1.0, 1.0, 1.0, 1.0));
        fluidSimulation.getFlowValuesFbo().getSource().draw(0.0, 0.0, Constants::CANVAS_WIDTH, Constants::CANVAS_HEIGHT);
      }
      
      // foreground
      {
        ofEnableBlendMode(OF_BLENDMODE_ALPHA);
        ofSetColor(ofFloatColor(1.0, 1.0, 1.0, 1.0));
        foregroundFbo.draw(0, 0, Constants::CANVAS_WIDTH, Constants::CANVAS_HEIGHT);
      }
      
      // foreground lines
      {
        ofEnableBlendMode(OF_BLENDMODE_ALPHA);
        ofSetColor(ofFloatColor(1.0, 1.0, 1.0, 1.0));
        foregroundLinesFbo.draw(0, 0, Constants::CANVAS_WIDTH, Constants::CANVAS_HEIGHT);
      }
    }
    compositeFbo.end();
    ofPixels pixels;
    compositeFbo.readToPixels(pixels);
    ofSaveImage(pixels, ofFilePath::getUserHomeDir()+"/Documents/bells2/snapshot-"+ofGetTimestampString()+".png", OF_IMAGE_QUALITY_BEST);
  }
  if (key == 'v') {
    plotVisible = !plotVisible;
    return;
  }
  if (key == 'V') {
    plot.save(ofGetWindowWidth(), ofFilePath::getUserHomeDir()+"/Documents/bells2/plot-"+ofGetTimestampString()+".svg");
  }
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
