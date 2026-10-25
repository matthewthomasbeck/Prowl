#pragma once

#include <cstdint>

namespace prowl_camera_driver
{

// One YUV 4:2:0 picture as the Pi's ISP writes it: a full-size Y plane, then U and V planes at half
// the width and half the height. Rows can be padded, so each plane has its own stride in bytes.
// The pixels belong to someone else (a camera buffer, or a test's vector); this only points at them.
struct YuvImage
{
  const uint8_t * y = nullptr;
  const uint8_t * u = nullptr;
  const uint8_t * v = nullptr;
  int y_stride = 0;
  int uv_stride = 0;
  int width = 0;
  int height = 0;
};

// The U and V planes are subsampled 2:1 both ways, rounding up for odd sizes.
inline int chromaWidth(int width) {return (width + 1) / 2;}
inline int chromaHeight(int height) {return (height + 1) / 2;}

}  // namespace prowl_camera_driver
