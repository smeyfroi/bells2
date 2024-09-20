#include "Plot.hpp"
#include "ofGraphicsCairo.h"

Plot::Plot() {
}

void Plot::addShapePtr(std::unique_ptr<Shape> shapePtr) {
  shapes.emplace_back(std::move(shapePtr));
}

void Plot::update() {
  for (auto& shape : shapes) {
    shape->update();
  }
  shapes.erase(std::remove_if(shapes.begin(),
                              shapes.end(),
                              [](const std::unique_ptr<Shape>& s) { return s->lifetime == 0; }),
               shapes.end());
}

void Plot::draw(float scale) {
  ofPushMatrix();
  ofScale(scale);
  for (auto& shape : shapes) {
    ofSetColor(shape->color);
    shape->draw();
  }
  ofPopMatrix();
}

void Plot::save(float scale, std::string filepath) {
  ofBeginSaveScreenAsSVG(filepath);
  draw(scale);
  ofEndSaveScreenAsSVG();
}
