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
// assume normalised coords
std::tuple<glm::vec2, glm::vec2> extendedLine(float x1, float y1, float x2, float y2) {
  return std::tuple<glm::vec2, glm::vec2> { {0.0, yForLineAtX(0.0, x1, y1, x2, y2)}, {1.0, yForLineAtX(1.0, x1, y1, x2, y2)} };
}

Divider::Divider(int divisions_) :
  divisions(divisions_)
{
  lines.resize(divisions_);
  maskFbo.allocate(Constants::CANVAS_WIDTH, Constants::CANVAS_HEIGHT, GL_R8);
}

bool Divider::update(std::vector<glm::vec4>& points) {
  bool linesChanged = false;
  
  // reverse sort points by age
  std::sort(points.begin(),
            points.end(),
            [](const glm::vec4& a, const glm::vec4& b) { return a.w > b.w; });
  
  // find new divisions
  if (points.size() > 2*divisions) {
    for (size_t i = 0; i < 2*divisions; i+=2) {
      glm::vec4 p1 = points[i*2]; glm::vec4 p2 = points[i*2+1];
      auto line = extendedLine(p1.x, p1.y, p2.x, p2.y);
      glm::vec2 ls = std::get<0>(line); glm::vec2 le = std::get<1>(line);
      auto oldLine = lines[i];
      if (ls.x != oldLine.x1 || le.x != oldLine.x2 || ls.y != oldLine.y1 || le.y != oldLine.y2) {
        linesChanged = true;
        lines[i] = DivisionLine(ls.x, ls.y, le.x, le.y);
      }
    }
  }
  
  // update mask
  if (linesChanged) {
    maskFbo.begin();
    ofClear(ofColor::black);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_ZERO);
    for (auto& line : lines) {
      ofPath path;
      path.moveTo(line.x1, line.y1);
      path.lineTo(line.x2, line.y2);
      path.lineTo(1.0, 0.0);
      path.lineTo(0.0, 0.0);
      path.close();
      path.scale(maskFbo.getWidth(), maskFbo.getHeight());
      path.setColor(ofColor::white);
      path.draw();
    }
    maskFbo.end();
  }

  return linesChanged;
}
