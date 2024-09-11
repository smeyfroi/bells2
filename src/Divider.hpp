#pragma once

#include "glm/vec4.hpp"
#include "glm/vec2.hpp"
#include <vector>
#include "ofFbo.h"

struct DivisionLine {
  DivisionLine() {};
  DivisionLine(float x1_, float y1_, float x2_, float y2_) :
    x1(x1_), y1(y1_), x2(x2_), y2(y2_) {};
  uint64_t age;
  float x1, x2, y1, y2;
};

class Divider {
public:
  Divider(int divisions_);
  inline std::vector<DivisionLine>& getDivisionLines() { return lines; };
  
  // Return true when a change happened. Sorts the points vector.
  bool update(std::vector<glm::vec4>& points);
  
private:
  int divisions;
  std::vector<DivisionLine> lines;

};
