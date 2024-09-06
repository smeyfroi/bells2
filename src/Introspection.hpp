#pragma once

#include "ofFbo.h"

// Draw intermediate introspections to this layer, which fade over time
class Introspection {
  
  class Shape {
  public:
    Shape(float x_, float y_, ofColor color_, uint64_t lifetimeFrames_);
    virtual ~Shape() {};
    bool isDead() const;
    void update();
    virtual void draw() =0;
  protected:
    float x, y;
    ofColor color;
    uint64_t lifetimeStartFrame, lifetimeFrames;
  };
  
  class Circle : public Shape {
  public:
    Circle(float x_, float y_, float r_, ofColor color_, bool filled_, uint64_t lifetimeFrames_);
    void draw() override;
  private:
    float r;
    bool filled;
  };
  
public:
  Introspection();

  void update();
  void draw();
  bool keyPressed(int key);

  void addCircle(float x, float y, float r, ofColor color, bool filled, uint64_t lifetimeFrames=40);
  
private:
  std::vector<std::unique_ptr<Introspection::Shape>> shapes;
  bool visible;
};
