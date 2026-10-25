#include "prowl_camera_driver/jpeg.hpp"

#include <turbojpeg.h>

#include <stdexcept>

namespace prowl_camera_driver
{

namespace
{

bool validImage(const YuvImage & image, std::string & error)
{
  if (!image.y || !image.u || !image.v || image.width <= 0 || image.height <= 0) {
    error = "empty image";
    return false;
  }
  if (image.y_stride < image.width || image.uv_stride < chromaWidth(image.width)) {
    error = "stride shorter than a row";
    return false;
  }
  return true;
}

std::string tjError(void * handle)
{
  return tjGetErrorStr2(static_cast<tjhandle>(handle));
}

}  // namespace

Jpeg::Jpeg()
: compressor_(tjInitCompress()), decompressor_(tjInitDecompress())
{
  if (!compressor_ || !decompressor_) {
    if (compressor_) {
      tjDestroy(static_cast<tjhandle>(compressor_));
    }
    if (decompressor_) {
      tjDestroy(static_cast<tjhandle>(decompressor_));
    }
    throw std::runtime_error("couldn't start TurboJPEG");
  }
}

Jpeg::~Jpeg()
{
  tjDestroy(static_cast<tjhandle>(compressor_));
  tjDestroy(static_cast<tjhandle>(decompressor_));
}

bool Jpeg::encode(const YuvImage & image, int quality, std::vector<uint8_t> & jpeg, std::string & error)
{
  if (!validImage(image, error)) {
    return false;
  }
  if (quality < 1 || quality > 100) {
    error = "quality must be 1-100";
    return false;
  }
  const unsigned char * planes[3] = {image.y, image.u, image.v};
  const int strides[3] = {image.y_stride, image.uv_stride, image.uv_stride};
  unsigned char * out = nullptr;  // TurboJPEG allocates it
  unsigned long size = 0;
  if (tjCompressFromYUVPlanes(
      static_cast<tjhandle>(compressor_), planes, image.width, strides, image.height, TJSAMP_420,
      &out, &size, quality, 0) != 0)
  {
    error = tjError(compressor_);
    tjFree(out);
    return false;
  }
  jpeg.assign(out, out + size);
  tjFree(out);
  return true;
}

bool Jpeg::toRgb(const YuvImage & image, std::vector<uint8_t> & rgb, std::string & error)
{
  if (!validImage(image, error)) {
    return false;
  }
  const unsigned char * planes[3] = {image.y, image.u, image.v};
  const int strides[3] = {image.y_stride, image.uv_stride, image.uv_stride};
  rgb.resize(static_cast<size_t>(image.width) * image.height * 3);
  if (tjDecodeYUVPlanes(
      static_cast<tjhandle>(decompressor_), planes, strides, TJSAMP_420, rgb.data(), image.width,
      image.width * 3, image.height, TJPF_RGB, 0) != 0)
  {
    error = tjError(decompressor_);
    return false;
  }
  return true;
}

bool Jpeg::decode(
  const uint8_t * data, size_t size, int & width, int & height, std::vector<uint8_t> & rgb,
  std::string & error)
{
  int subsamp = 0, colorspace = 0;
  auto * handle = static_cast<tjhandle>(decompressor_);
  if (tjDecompressHeader3(handle, data, size, &width, &height, &subsamp, &colorspace) != 0) {
    error = tjError(decompressor_);
    return false;
  }
  rgb.resize(static_cast<size_t>(width) * height * 3);
  if (tjDecompress2(handle, data, size, rgb.data(), width, width * 3, height, TJPF_RGB, 0) != 0) {
    error = tjError(decompressor_);
    return false;
  }
  return true;
}

}  // namespace prowl_camera_driver
