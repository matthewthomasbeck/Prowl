#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "prowl_brain/local_map.hpp"

// SDL's types, declared here so only map_display.cpp needs SDL's headers.
struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

namespace prowl_brain
{

struct Rgb
{
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
};

// The robot's screen (the DSI LCD): a borderless full-screen window on the
// desktop that shows the map, the robot as an arrow, and a colored frame that
// says what the brain is doing.
//
// Drawn with SDL2's software renderer, so it needs no GPU access in the
// container, only the desktop's X server (DISPLAY, set up by docker/dev.sh up).
//
// Only thread 1 uses this: SDL wants a window to be drawn on, and its events
// handled, by the same thread.
class MapDisplay
{
public:
  MapDisplay() = default;
  ~MapDisplay();  // closes the window

  MapDisplay(const MapDisplay &) = delete;
  MapDisplay & operator=(const MapDisplay &) = delete;

  // Opens the window. Returns false if there's no screen to open it on; error() says why.
  bool open();
  bool isOpen() const {return window_ != nullptr;}
  const std::string & error() const {return error_;}
  // The window's size, e.g. "800x480", for the log.
  std::string describe() const;

  // Draws one frame: `image` (null = no map yet: just the frame) inside a border of
  // `frame` color. Also handles the window's events; Esc or q closes it (the brain
  // keeps running). Takes a few ms, so call it a few times a second, not every tick.
  void show(const std::shared_ptr<const MapImage> & image, Rgb frame);

private:
  void close();
  // Copies `picture` into texture_ (unless it's already there), making a new texture if
  // the size changed.
  bool upload(const std::shared_ptr<const MapImage> & picture);

  SDL_Window * window_ = nullptr;
  SDL_Renderer * renderer_ = nullptr;
  SDL_Texture * texture_ = nullptr;
  int texture_w_ = 0;
  int texture_h_ = 0;
  std::shared_ptr<const MapImage> uploaded_;  // the picture in texture_, so it's only copied once
  std::vector<uint8_t> rgb_;  // scratch space for the grey -> RGB copy
  std::string error_;
};

}  // namespace prowl_brain
