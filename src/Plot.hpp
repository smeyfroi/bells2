#pragma once

#include <string>
#include <vector>
#include "ofGraphics.h"
#include "ofPath.h"

const int ARC_RESOLUTION = 512;

class Shape {
public:
  Shape(ofColor color_, uint64_t lifetime_) : color(color_), lifetime(lifetime_) {};
  virtual ~Shape() {};
  void update() { if (lifetime != 0) lifetime--; };
  bool isExpired() { return (lifetime == 0); };
  virtual void draw() =0;
  ofColor color;
  uint64_t lifetime;
};

class LineShape : public Shape {
public:
  LineShape(float x1_, float y1_, float x2_, float y2_, ofColor color_, uint64_t lifetime_) :
    Shape(color_, lifetime_), x1(x1_), y1(y1_), x2(x2_), y2(y2_) {};
  void draw() override { ofDrawLine(x1, y1, x2, y2); };
private:
  float x1, y1, x2, y2;
};

class CircleShape : public Shape {
public:
  CircleShape(float x_, float y_, float r_, ofColor color_, uint64_t lifetime_) :
    Shape(color_, lifetime_), x(x_), y(y_), r(r_) {};
  void draw() override { ofDrawCircle(x, y, r); };
private:
  float x, y, r;
};

class ArcShape : public Shape {
public:
  ArcShape(float x_, float y_, float r_, float angleBegin_, float angleEnd_, ofColor color_, uint64_t lifetime_) :
    Shape(color_, lifetime_), x(x_), y(y_), r(r_), angleBegin(angleBegin_), angleEnd(angleEnd_) {};
  void draw() override { ofPolyline path; path.arc(x, y, r, r, angleBegin, angleEnd, ARC_RESOLUTION); path.draw(); };
private:
  float x, y, r;
  float angleBegin, angleEnd;
};



class Plot {
public:
  Plot();
  void addShapePtr(std::unique_ptr<Shape> shapePtr);
  void update();
  void draw(float scale);
  void save(float scale, std::string filepath);
  
private:
  std::vector<std::unique_ptr<Shape>> shapes;
};
