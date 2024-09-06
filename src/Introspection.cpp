#include "Introspection.hpp"

#include "ofGraphics.h"
#include "ofUtils.h"

constexpr float FADE_FACTOR = 0.95;

Introspection::Shape::Shape(float x_, float y_, ofColor color_, uint64_t lifetimeFrames_) :
x(x_), y(y_), color(color_), lifetimeFrames(lifetimeFrames_), lifetimeStartFrame(ofGetFrameNum())
{}

bool Introspection::Shape::isDead() const {
  return (ofGetFrameNum() - lifetimeStartFrame > lifetimeFrames);
}

void Introspection::Shape::update() {
  color.a = color.a * FADE_FACTOR;
}

Introspection::Circle::Circle(float x_, float y_, float r_, ofColor color_, bool filled_, uint64_t lifetimeFrames_) :
Introspection::Shape(x_, y_, color_, lifetimeFrames_),
r(r_), filled(filled_)
{}

void Introspection::Circle::draw() {
  ofPushStyle();
  {
    ofSetColor(color);
    if (filled) {
      ofFill();
    } else {
      ofNoFill();
      ofSetLineWidth(6.0);
    }
    ofDrawCircle(x, y, r);
  }
  ofPopStyle();
}



Introspection::Introspection() {
}

void Introspection::update() {
  std::for_each(shapes.begin(),
                shapes.end(),
                [](auto& s) { s->update(); });
  shapes.erase(std::remove_if(
                              shapes.begin(),
                              shapes.end(),
                              [](const auto& s) { return s->isDead(); }),
               shapes.end());
}

void Introspection::draw() {
  if (!visible) return;
  ofEnableBlendMode(OF_BLENDMODE_ALPHA);
  std::for_each(shapes.begin(),
                shapes.end(),
                [](auto& s) { s->draw(); });
}

bool Introspection::keyPressed(int key) {
  if (key == 'i') {
    visible = !visible;
    return true;
  }
  return false;
}

void Introspection::addCircle(float x, float y, float r, ofColor color, bool filled, uint64_t lifetimeFrames) {
  shapes.push_back(std::make_unique<Introspection::Circle>(Introspection::Circle(x, y, r, color, filled, lifetimeFrames)));
}
