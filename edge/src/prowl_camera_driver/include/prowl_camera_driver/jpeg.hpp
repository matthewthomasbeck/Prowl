#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "prowl_camera_driver/yuv_image.hpp"

namespace prowl_camera_driver
{

// JPEG encoding with TurboJPEG (libjpeg-turbo's simple API).
//
// The input is full-range BT.601 YUV (libcamera's sYCC colour space), which is exactly what JPEG
// stores, so encoding needs no colour conversion: the planes go straight to the DCT.
//
// Each instance holds TurboJPEG handles, which aren't thread safe: use one per thread.
class Jpeg
{
public:
  Jpeg();
  ~Jpeg();
  Jpeg(const Jpeg &) = delete;
  Jpeg & operator=(const Jpeg &) = delete;

  // Encodes `image` as a baseline JPEG with 4:2:0 chroma, `quality` 1-100, replacing the contents
  // of `jpeg`. Returns false and sets `error` on failure.
  bool encode(const YuvImage & image, int quality, std::vector<uint8_t> & jpeg, std::string & error);

  // Decodes a JPEG to packed RGB, 3 bytes per pixel. Not needed by the driver; the tests use it to check
  // encode().
  bool decode(
    const uint8_t * data, size_t size, int & width, int & height, std::vector<uint8_t> & rgb,
    std::string & error);

private:
  void * compressor_ = nullptr;    // tjhandle
  void * decompressor_ = nullptr;  // tjhandle, for decode()
};

}  // namespace prowl_camera_driver
