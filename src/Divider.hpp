#pragma once

#include "glm/vec4.hpp"
#include "glm/vec2.hpp"
#include <vector>
#include "ofFbo.h"
#include "ofMath.h"

static constexpr float EPSILON = std::numeric_limits<float>::epsilon();
const float CLOSE_TOLERANCE_SQUARED = std::pow(1.0 / 10.0, 2.0);

// x1,y1 and x2,y2 are normalised coords; ref coords can be anything
class DivisionLine {
public:
  DivisionLine() : DivisionLine(0.0, 0.0, 0.0, 0.0) {};
  DivisionLine(float refX1_, float refY1_, float refX2_, float refY2_);
  bool isEqual(const DivisionLine& right) const;
  bool isRefPointEqual(float x, float y) const;
  bool isRefPointCloseTo(float x, float y) const;
  bool isEndCloseTo(const DivisionLine& line) const;
  bool isValid() const;
  void draw(float width) const;
public:
  // TODO: refactor to make these private
  float refX1, refY1, refX2, refY2;
  float x1, x2, y1, y2; // normalised
private:
  float xNorth, xSouth, yWest, yEast; // normalised
  int64_t age;
};



class Divider {
public:
  Divider(int divisions_);
  inline const std::vector<DivisionLine>& getDivisionLines() const { return lines; };

  bool isPointInPoints(float x, float y, const std::vector<glm::vec4>& points) const; // looks like something for a PointVector
  std::optional<glm::vec4> findPointCloseTo(const std::vector<glm::vec4>& points, float x, float y) const; // looks like something for a PointVector

  bool isRefPointInLines(float x, float y) const;
  bool isLineInLines(const DivisionLine& line) const;
  bool isLineCloseToLines(const DivisionLine& line) const;
  bool isLineEligible(const DivisionLine& line) const;
  DivisionLine findNewDivisionLineCloseTo(const std::vector<glm::vec4>& points, float x1, float y1, float x2, float y2) const;
  std::tuple<glm::vec2, glm::vec2> extendedLineEnclosedByDivider(float x1, float y1, float x2, float y2) const;

  // Return true when a change happened. May modify the points
  bool update(std::vector<glm::vec4>& points);
  
private:
  int divisions;
  std::vector<DivisionLine> lines;

};
