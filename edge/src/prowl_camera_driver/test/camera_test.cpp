// Hardware-free checks of the camera driver: JPEG encoding from padded planes, and the timing helpers.
// Exits non-zero if anything fails.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "prowl_camera_driver/jpeg.hpp"
#include "prowl_camera_driver/timing.hpp"
#include "prowl_camera_driver/yuv_image.hpp"

using namespace prowl_camera_driver;

namespace
{

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const char * expr, const char * file, int line)
{
  ++g_checks;
  if (!ok) {
    ++g_failures;
    std::printf("FAIL %s:%d: %s\n", file, line, expr);
  }
}

void checkNear(double actual, double expected, double tol, const char * expr, const char * file, int line)
{
  ++g_checks;
  if (!(std::abs(actual - expected) <= tol)) {
    ++g_failures;
    std::printf("FAIL %s:%d: %s = %.9g, expected %.9g (+-%g)\n", file, line, expr, actual, expected, tol);
  }
}

#define CHECK(expr) check((expr), #expr, __FILE__, __LINE__)
#define CHECK_NEAR(actual, expected, tol) checkNear((actual), (expected), (tol), #actual, __FILE__, __LINE__)

// A YUV 4:2:0 picture with padded rows, like the ISP's. The padding is filled with 0xEE so that a
// stride mix-up shows up as bright garbage.
struct TestPicture
{
  int width, height, y_stride, uv_stride;
  std::vector<uint8_t> y, u, v;

  TestPicture(int w, int h, int pad)
  : width(w), height(h), y_stride(w + pad), uv_stride(chromaWidth(w) + pad / 2),
    y(static_cast<size_t>(y_stride) * h, 0xEE),
    u(static_cast<size_t>(uv_stride) * chromaHeight(h), 0xEE),
    v(static_cast<size_t>(uv_stride) * chromaHeight(h), 0xEE)
  {
  }

  // Y from `luma(x, y)`; U and V constant.
  template<typename Luma>
  void fill(Luma luma, uint8_t cb, uint8_t cr)
  {
    for (int r = 0; r < height; ++r) {
      for (int c = 0; c < width; ++c) {
        y[r * y_stride + c] = luma(c, r);
      }
    }
    for (int r = 0; r < chromaHeight(height); ++r) {
      for (int c = 0; c < chromaWidth(width); ++c) {
        u[r * uv_stride + c] = cb;
        v[r * uv_stride + c] = cr;
      }
    }
  }

  YuvImage view() const
  {
    YuvImage image;
    image.y = y.data();
    image.u = u.data();
    image.v = v.data();
    image.y_stride = y_stride;
    image.uv_stride = uv_stride;
    image.width = width;
    image.height = height;
    return image;
  }
};

const uint8_t * pixel(const std::vector<uint8_t> & rgb, int width, int x, int y)
{
  return &rgb[(static_cast<size_t>(y) * width + x) * 3];
}

void testChromaSize()
{
  CHECK(chromaWidth(1280) == 640);
  CHECK(chromaWidth(63) == 32);
  CHECK(chromaHeight(720) == 360);
  CHECK(chromaHeight(47) == 24);
}

void testTiming()
{
  CHECK(frameDurationUs(30.0) == 33333);
  CHECK(frameDurationUs(10.0) == 100000);
  CHECK(frameDurationUs(56.0) == 17857);
  // A frame exposed 25 ms before "now" on the monotonic clock is 25 ms before "now" on the system clock.
  const int64_t mono_now = 5000000000, sys_now = 1760000000000000000;
  CHECK(monotonicToSystemNs(mono_now - 25000000, mono_now, sys_now) == sys_now - 25000000);
  CHECK(monotonicToSystemNs(mono_now, mono_now, sys_now) == sys_now);
}

// A grey ramp (U = V = 128, so R = G = B = Y) survives encoding and decoding, padding ignored.
void testEncodeGreyRamp()
{
  TestPicture pic(64, 48, 16);
  pic.fill([](int x, int y) {return static_cast<uint8_t>(40 + 2 * x + y);}, 128, 128);
  Jpeg jpeg;
  std::vector<uint8_t> data;
  std::string error;
  CHECK(jpeg.encode(pic.view(), 95, data, error));
  CHECK(data.size() > 4 && data[0] == 0xFF && data[1] == 0xD8);  // SOI marker

  int w = 0, h = 0;
  std::vector<uint8_t> rgb;
  CHECK(jpeg.decode(data.data(), data.size(), w, h, rgb, error));
  CHECK(w == 64 && h == 48);
  if (w != 64 || h != 48) {
    return;
  }
  for (int y : {0, 20, 47}) {
    for (int x : {0, 31, 63}) {
      const uint8_t * p = pixel(rgb, w, x, y);
      const double expected = 40 + 2 * x + y;
      CHECK_NEAR(p[0], expected, 4);
      CHECK_NEAR(p[1], expected, 4);
      CHECK_NEAR(p[2], expected, 4);
    }
  }
}

// Full-range BT.601: pure red is Y 76, U 85, V 255, and it stays red through a JPEG.
void testEncodeColour()
{
  TestPicture pic(63, 47, 10);  // odd sizes, so the last chroma column and row cover one pixel
  pic.fill([](int, int) {return 76;}, 85, 255);
  Jpeg jpeg;
  std::vector<uint8_t> data, decoded;
  std::string error;
  int w = 0, h = 0;
  CHECK(jpeg.encode(pic.view(), 90, data, error));
  CHECK(jpeg.decode(data.data(), data.size(), w, h, decoded, error));
  CHECK(w == 63 && h == 47);
  if (!decoded.empty()) {
    for (const auto & xy : {std::pair<int, int>{0, 0}, {62, 46}, {30, 20}}) {
      const uint8_t * p = pixel(decoded, w, xy.first, xy.second);
      CHECK(p[0] >= 240 && p[1] <= 15 && p[2] <= 15);
    }
  }
}

void testRejectsBadInput()
{
  TestPicture pic(32, 32, 0);
  pic.fill([](int, int) {return 128;}, 128, 128);
  Jpeg jpeg;
  std::vector<uint8_t> out;
  std::string error;
  CHECK(!jpeg.encode(pic.view(), 0, out, error));
  CHECK(!jpeg.encode(pic.view(), 101, out, error));
  YuvImage empty;
  CHECK(!jpeg.encode(empty, 80, out, error));
  YuvImage short_stride = pic.view();
  short_stride.y_stride = 16;
  CHECK(!jpeg.encode(short_stride, 80, out, error));
  const uint8_t junk[] = {1, 2, 3, 4};
  int w = 0, h = 0;
  CHECK(!jpeg.decode(junk, sizeof(junk), w, h, out, error));
  CHECK(!error.empty());
}

}  // namespace

int main()
{
  testChromaSize();
  testTiming();
  testEncodeGreyRamp();
  testEncodeColour();
  testRejectsBadInput();
  std::printf("%d checks, %d failed\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
