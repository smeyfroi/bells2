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

Introspection::Line::Line(float x_, float y_, float x2_, float y2_, ofColor color_, uint64_t lifetimeFrames_) :
Introspection::Shape(x_, y_, color_, lifetimeFrames_),
x2(x2_), y2(y2_)
{}

void Introspection::Line::draw() {
  ofPushStyle();
  {
    ofSetColor(color);
    ofSetLineWidth(6.0);
    ofDrawLine(x, y, x2, y2);
  }
  ofPopStyle();
}



Introspection::Introspection() {
}

void Introspection::update() {
  if (!visible) return;
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
  if (!visible) return;
  shapes.push_back(std::make_unique<Introspection::Circle>(Introspection::Circle(x, y, r, color, filled, lifetimeFrames)));
}

void Introspection::addLine(float x, float y, float x2, float y2, ofColor color, uint64_t lifetimeFrames) {
  if (!visible) return;
  shapes.push_back(std::make_unique<Introspection::Line>(Introspection::Line(x, y, x2, y2, color, lifetimeFrames)));
}
