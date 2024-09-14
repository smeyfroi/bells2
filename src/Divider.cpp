#include "Divider.hpp"
#include "ofPath.h"
#include "ofColor.h"
#include "ofGraphics.h"
#include "Constants.h"

// y = mx + b
float yForLineAtX(float x, float x1, float y1, float x2, float y2) {
  float m = (y2 - y1) / (x2 - x1);
  float b = y1 - (m * x1);
  return m * x + b;
}
float xForLineAtY(float y, float x1, float y1, float x2, float y2) {
  float m = (y2 - y1) / (x2 - x1);
  float b = y1 - (m * x1);
  return (y - b) / m;
}

DivisionLine::DivisionLine(float refX1_, float refY1_, float refX2_, float refY2_) :
  refX1(refX1_), refY1(refY1_), refX2(refX2_), refY2(refY2_),
  x1(0.0), y1(yForLineAtX(0.0, refX1, refY1, refX2, refY2)),
  x2(1.0), y2(yForLineAtX(1.0, refX1, refY1, refX2, refY2)),
  age(0)
{};

bool DivisionLine::isValid() const {
  return (ofDist(refX1, refY1, refX2, refY2) > EPSILON);
};

void DivisionLine::draw(float width) const {
  ofPushMatrix();
  ofTranslate(x1, y1);
  ofRotateRad(std::atan2((y2 - y1), (x2 - x1)));
  ofDrawRectangle(-1.0, -width/2.0, ofDist(x1, y1, x2, y2)+1.0, width);
  ofPopMatrix();
}



Divider::Divider(int divisions_) :
  divisions(divisions_)
{
  lines.resize(divisions_);
}

bool Divider::isPointInPoints(float x, float y, const std::vector<glm::vec4>& points) const {
  return std::any_of(points.begin(),
                     points.end(),
                     [&] (const auto& p) { return (ofDistSquared(x, y, p.x, p.y) < EPSILON); });
}

std::optional<glm::vec4> Divider::findPointCloseTo(const std::vector<glm::vec4>& points, float x, float y) const {
  for (auto iter = points.begin(); iter != points.end(); iter++) {
    glm::vec4 p = *iter;
    if (ofDistSquared(p.x, p.y, x, y) < CLOSE_TOLERANCE_SQUARED) {
      return p;
    }
  }
  return {};
}

bool Divider::isRefPointInLines(float x, float y) const {
  return std::any_of(lines.begin(),
                     lines.end(),
                     [&] (const auto& l) { return (l.isRefPoint(x, y)); });
}

bool Divider::isLineInLines(const DivisionLine& line) const {
  return std::any_of(lines.begin(),
                     lines.end(),
                     [&](const auto& l) { return line.isEqual(l); });
}

//constexpr float EDGE_TOLERANCE = 1.0 / 5.0;
bool Divider::isLineEligible(const DivisionLine& line) const {
  if (!line.isValid()) return false;
  if (isLineInLines(line)) return false;
  return true;
//  if (isRefPointInLines(line.refX1, line.refY1) || isRefPointInLines(line.refX2, line.refY2)) return false; // Ref point already used
  // Attempt to limit to edges, but needs fixing and not sure it's a good idea
//  return ((std::abs(line.x1) < EDGE_TOLERANCE || std::abs(line.x2 - 1.0) < EDGE_TOLERANCE)
//          && (std::abs(line.y1) < EDGE_TOLERANCE || std::abs(line.y2 - 1.0) < EDGE_TOLERANCE));
}

DivisionLine Divider::findNewLineCloseTo(const std::vector<glm::vec4>& points, float x1, float y1, float x2, float y2) const {
  // might pick the same point for start and end to make an invalid line
  if (auto p1 = findPointCloseTo(points, x1, y1)) {
    if (auto p2 = findPointCloseTo(points, x2, y2)) {
      return DivisionLine(p1->x, p1->y, p2->x, p2->y);
    }
  }
  return DivisionLine(); // an invalid DivisionLine
}

// Replace obsolete lines with close equivalents if possible
// Add new lines to fill blanks
bool Divider::update(std::vector<glm::vec4>& points) {
  bool linesChanged = false;
  
  for (auto& line : lines) {
    if (line.isValid()
        && (!isPointInPoints(line.refX1, line.refY1, points)
        || !isPointInPoints(line.refX2, line.refY2, points))) {
      // line no longer connects valid points so find a nearby equivalent using points not in use, retaining the existing line if no valid replacement
      auto newLine = findNewLineCloseTo(points, line.refX1, line.refY1, line.refX2, line.refY2);
      if (newLine.isValid() && !isLineInLines(newLine)) {
        line = newLine;
        linesChanged = true;
      }
    }
  }
  
  // reverse sort points by age (w) so make new lines from more stable points
//  std::sort(points.begin(),
//            points.end(),
//            [](const glm::vec4& a, const glm::vec4& b) { return a.w > b.w; });

  // find new divisions to replace any invalid ones
  if (points.size() < 3) return linesChanged;
  for (auto& line : lines) {
    if (line.isValid()) continue;
    glm::vec4 p1 = points[ofRandom(points.size())];
    glm::vec4 p2 = points[ofRandom(points.size())];
    DivisionLine newLine { p1.x, p1.y, p2.x, p2.y };
    if (!isLineEligible(newLine)) continue;
    line = newLine;
    linesChanged = true;
  }
//    for (auto iter1 = points.begin(); iter1 != points.end(); iter1++) {
//      glm::vec4 p1 = *iter1;
//      for (auto iter2 = iter1+1; iter2 != points.end(); iter2++) {
//        glm::vec4 p2 = *iter2;
//        DivisionLine newLine { p1.x, p1.y, p2.x, p2.y };
//        if (!isLineEligible(newLine)) continue;
//        line = newLine;
//        linesChanged = true;
//      }
//    }
//  }
  return linesChanged;
}
