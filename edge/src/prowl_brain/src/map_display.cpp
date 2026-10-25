#include "prowl_brain/map_display.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <SDL.h>

namespace prowl_brain
{

namespace
{

constexpr int kFrameWidth = 8;                // px of state-colored border
constexpr float kArrowLength = 22.0f;         // px, tip to tail
constexpr Rgb kBackground{24, 26, 30};        // around the map, inside the frame
constexpr Rgb kRobot{255, 140, 0};            // orange stands out on grey

}  // namespace

MapDisplay::~MapDisplay()
{
  close();
}

bool MapDisplay::open()
{
  if (window_) {
    return true;
  }
  // SDL would otherwise install its own Ctrl-C handler and replace the brain's (see main.cpp).
  SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
  // Plain X11 drawing, no OpenGL: the container has no GPU access, and trying it first
  // costs about a second.
  SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "0");
  SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
  if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
    error_ = SDL_GetError();
    return false;
  }
  window_ = SDL_CreateWindow("Prowl map", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 0, 0,
    SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_BORDERLESS);
  if (window_) {
    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
  }
  if (!renderer_) {
    error_ = SDL_GetError();
    close();
    return false;
  }
  SDL_ShowCursor(SDL_DISABLE);
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");  // nearest pixel: crisp map cells
  error_.clear();
  return true;
}

std::string MapDisplay::describe() const
{
  if (!renderer_) {
    return "closed";
  }
  int w = 0;
  int h = 0;
  SDL_GetRendererOutputSize(renderer_, &w, &h);
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%dx%d", w, h);
  return buf;
}

void MapDisplay::close()
{
  if (texture_) {
    SDL_DestroyTexture(texture_);
    texture_ = nullptr;
  }
  if (renderer_) {
    SDL_DestroyRenderer(renderer_);
    renderer_ = nullptr;
  }
  if (window_) {
    SDL_DestroyWindow(window_);
    window_ = nullptr;
  }
  if (SDL_WasInit(SDL_INIT_VIDEO)) {
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
  }
  uploaded_.reset();
  texture_w_ = texture_h_ = 0;
}

void MapDisplay::show(const std::shared_ptr<const MapImage> & image, Rgb frame)
{
  if (!window_) {
    return;
  }
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    const bool quit_key = e.type == SDL_KEYDOWN &&
      (e.key.keysym.sym == SDLK_ESCAPE || e.key.keysym.sym == SDLK_q);
    if (e.type == SDL_QUIT || quit_key) {
      close();
      error_ = "closed from the screen";
      return;
    }
  }

  int w = 0;
  int h = 0;
  SDL_GetRendererOutputSize(renderer_, &w, &h);
  SDL_SetRenderDrawColor(renderer_, frame.r, frame.g, frame.b, 255);
  SDL_RenderClear(renderer_);
  const SDL_Rect inner{kFrameWidth, kFrameWidth, w - 2 * kFrameWidth, h - 2 * kFrameWidth};
  SDL_SetRenderDrawColor(renderer_, kBackground.r, kBackground.g, kBackground.b, 255);
  SDL_RenderFillRect(renderer_, &inner);

  if (image && image->width > 0 && image->height > 0 && upload(image)) {
    // As big as fits, centered, without stretching.
    const float scale = std::min(static_cast<float>(inner.w) / static_cast<float>(image->width),
      static_cast<float>(inner.h) / static_cast<float>(image->height));
    const float map_w = scale * static_cast<float>(image->width);
    const float map_h = scale * static_cast<float>(image->height);
    const SDL_FRect dst{inner.x + (inner.w - map_w) / 2.0f, inner.y + (inner.h - map_h) / 2.0f, map_w, map_h};
    SDL_RenderCopyF(renderer_, texture_, nullptr, &dst);

    // The robot: a triangle pointing where it faces. Screen y grows downward, so yaw's
    // counter-clockwise turn is a minus sign on y.
    const float cx = dst.x + static_cast<float>(image->robot_x) * scale;
    const float cy = dst.y + static_cast<float>(image->robot_y) * scale;
    const float fx = static_cast<float>(std::cos(image->robot_yaw));
    const float fy = -static_cast<float>(std::sin(image->robot_yaw));
    const float half = kArrowLength / 2.0f;
    const SDL_Color c{kRobot.r, kRobot.g, kRobot.b, 255};
    const SDL_Vertex v[3] = {
      {{cx + fx * half, cy + fy * half}, c, {0.0f, 0.0f}},                                         // tip
      {{cx - fx * half - fy * half * 0.7f, cy - fy * half + fx * half * 0.7f}, c, {0.0f, 0.0f}},  // back corners
      {{cx - fx * half + fy * half * 0.7f, cy - fy * half - fx * half * 0.7f}, c, {0.0f, 0.0f}},
    };
    SDL_RenderGeometry(renderer_, nullptr, v, 3, nullptr, 0);
  }
  SDL_RenderPresent(renderer_);
}

bool MapDisplay::upload(const std::shared_ptr<const MapImage> & picture)
{
  if (uploaded_ == picture) {
    return true;  // already in the texture
  }
  const MapImage & image = *picture;
  if (!texture_ || texture_w_ != image.width || texture_h_ != image.height) {
    if (texture_) {
      SDL_DestroyTexture(texture_);
    }
    texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING,
      image.width, image.height);
    if (!texture_) {
      error_ = SDL_GetError();
      texture_w_ = texture_h_ = 0;
      return false;
    }
    texture_w_ = image.width;
    texture_h_ = image.height;
  }
  rgb_.resize(image.pixels.size() * 3);
  for (std::size_t i = 0; i < image.pixels.size(); ++i) {
    rgb_[3 * i] = rgb_[3 * i + 1] = rgb_[3 * i + 2] = image.pixels[i];
  }
  SDL_UpdateTexture(texture_, nullptr, rgb_.data(), image.width * 3);
  uploaded_ = picture;
  return true;
}

}  // namespace prowl_brain
