#pragma once

struct Constants {
  static constexpr float FRAME_RATE = 30.0;
  
  static const size_t WINDOW_WIDTH = 1200;
  static const size_t WINDOW_HEIGHT = 1200;
  
  static const size_t CANVAS_WIDTH = WINDOW_WIDTH * 5.0;
  static const size_t CANVAS_HEIGHT = WINDOW_HEIGHT * 5.0;

  static const size_t FLUID_WIDTH = WINDOW_WIDTH * 2.0;
  static const size_t FLUID_HEIGHT = WINDOW_HEIGHT * 2.0;
  
  static const size_t SOM_WIDTH = 128;
  static const size_t SOM_HEIGHT = 128;

};
