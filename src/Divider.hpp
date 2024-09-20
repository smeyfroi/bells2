#pragma once

#include "glm/vec4.hpp"
#include "glm/vec2.hpp"
#include <vector>
#include "ofFbo.h"
#include "ofMath.h"

static constexpr float EPSILON = std::numeric_limits<float>::epsilon();
const float CLOSE_TOLERANCE_SQUARED = std::pow(1.0 / 10.0, 2.0);

// x1,y1 and x2,y2 are normalised coords; ref coords can be anything
struct DivisionLine {
  DivisionLine() : DivisionLine(0.0, 0.0, 0.0, 0.0) {};
  DivisionLine(float refX1_, float refY1_, float refX2_, float refY2_);
  int64_t age;
  float refX1, refY1, refX2, refY2;
  float x1, x2, y1, y2;
  inline bool isEqual(const DivisionLine& right) const {
    return ((std::abs(refX1-right.refX1) < EPSILON
            && std::abs(refY1-right.refY1) < EPSILON
            && std::abs(refX2-right.refX2) < EPSILON
            && std::abs(refY2-right.refY2) < EPSILON)
            ||
            (std::abs(refX2-right.refX1) < EPSILON
            && std::abs(refY2-right.refY1) < EPSILON
            && std::abs(refX1-right.refX2) < EPSILON
            && std::abs(refY1-right.refY2) < EPSILON));
  }
  inline bool isRefPoint(float x, float y) const {
    return (std::abs(refX1-x) < EPSILON && std::abs(refY1-y) < EPSILON)
      || (std::abs(refX2-x) < EPSILON && std::abs(refY2-y) < EPSILON);
  }
  inline bool isRefPointCloseTo(float x, float y) const {
    return ((ofDistSquared(refX1, refY1, x, y) < CLOSE_TOLERANCE_SQUARED)
            || (ofDistSquared(refX2, refY2, x, y) < CLOSE_TOLERANCE_SQUARED));
  }
  bool isValid() const;
  void draw(float width) const;
};
//bool operator == (const DivisionLine& left, const DivisionLine& right) {
//  return (left.isEqual(right));
//}


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
