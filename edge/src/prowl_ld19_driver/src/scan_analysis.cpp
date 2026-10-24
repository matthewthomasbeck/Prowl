#include "prowl_ld19_driver/scan_analysis.hpp"

#include <algorithm>
#include <cmath>

namespace prowl_ld19_driver
{

namespace
{

constexpr double kDegToRad = 3.14159265358979323846 / 180.0;

double wrap360(double deg)
{
  const double d = std::fmod(deg, 360.0);
  return d < 0.0 ? d + 360.0 : d;
}

bool valid(const ScanPoint & p, const AnalysisConfig & config)
{
  return p.distance_mm > 0 && p.distance_mm >= config.min_range_mm;
}

// Distance between two points given in polar form (mm, deg).
double chord(double r1, double a1, double r2, double a2)
{
  const double c = std::cos((a2 - a1) * kDegToRad);
  return std::sqrt(std::max(r1 * r1 + r2 * r2 - 2.0 * r1 * r2 * c, 0.0));
}

struct Cluster
{
  size_t first;
  size_t last;
  size_t count;
  size_t nearest;
};

// Whether point b, coming after a with `missing` invalid points between them,
// is still the same surface.
bool continues(const ScanPoint & a, const ScanPoint & b, size_t missing, const AnalysisConfig & config)
{
  const double gap = std::abs(std::remainder(b.angle_deg - a.angle_deg, 360.0));
  const double jump = std::abs(static_cast<double>(b.distance_mm) - a.distance_mm);
  return missing <= config.max_missing_points && gap <= config.max_gap_deg &&
         jump <= config.jump_mm + config.jump_ratio * b.distance_mm;
}

}  // namespace

double Mount::bearing(double sensor_angle_deg) const
{
  return wrap360((mirrored ? -sensor_angle_deg : sensor_angle_deg) + yaw_deg);
}

size_t sectorOf(double bearing_deg)
{
  const double width = 360.0 / kSectors;
  return static_cast<size_t>(std::floor(wrap360(bearing_deg + width / 2.0) / width)) % kSectors;
}

Nearest nearestPoint(const Scan & scan, const Mount & mount, const AnalysisConfig & config)
{
  Nearest best;
  for (const ScanPoint & p : scan.points) {
    if (valid(p, config) && (!best.found || p.distance_mm < best.distance_m * 1000.0)) {
      best = {true, p.distance_mm / 1000.0, mount.bearing(p.angle_deg)};
    }
  }
  return best;
}

std::array<Nearest, kSectors> sectorNearest(const Scan & scan, const Mount & mount, const AnalysisConfig & config)
{
  std::array<Nearest, kSectors> sectors{};
  for (const ScanPoint & p : scan.points) {
    if (!valid(p, config)) {
      continue;
    }
    const double bearing = mount.bearing(p.angle_deg);
    Nearest & s = sectors[sectorOf(bearing)];
    if (!s.found || p.distance_mm < s.distance_m * 1000.0) {
      s = {true, p.distance_mm / 1000.0, bearing};
    }
  }
  return sectors;
}

std::vector<Object> findObjects(const Scan & scan, const Mount & mount, const AnalysisConfig & config)
{
  const auto & pts = scan.points;
  std::vector<Cluster> clusters;
  size_t missing = 0;
  for (size_t i = 0; i < pts.size(); ++i) {
    if (!valid(pts[i], config)) {
      ++missing;
      continue;
    }
    if (!clusters.empty() && continues(pts[clusters.back().last], pts[i], missing, config)) {
      Cluster & c = clusters.back();
      c.last = i;
      ++c.count;
      if (pts[i].distance_mm < pts[c.nearest].distance_mm) {
        c.nearest = i;
      }
    } else {
      clusters.push_back({i, i, 1, i});
    }
    missing = 0;
  }

  // The scan starts at 0 deg, so a surface straddling 0 deg is split in two.
  if (clusters.size() >= 2) {
    Cluster & head = clusters.front();
    const Cluster & tail = clusters.back();
    const size_t across = (pts.size() - 1 - tail.last) + head.first;
    if (continues(pts[tail.last], pts[head.first], across, config)) {
      head.first = tail.first;
      head.count += tail.count;
      if (pts[tail.nearest].distance_mm < pts[head.nearest].distance_mm) {
        head.nearest = tail.nearest;
      }
      clusters.pop_back();
    }
  }

  std::vector<Object> objects;
  for (const Cluster & c : clusters) {
    if (c.count < config.min_cluster_points) {
      continue;
    }
    const ScanPoint & a = pts[c.first];
    const ScanPoint & b = pts[c.last];
    objects.push_back({
        pts[c.nearest].distance_mm / 1000.0,
        mount.bearing(pts[c.nearest].angle_deg),
        chord(a.distance_mm, a.angle_deg, b.distance_mm, b.angle_deg) / 1000.0,
        c.count});
  }
  std::sort(objects.begin(), objects.end(),
    [](const Object & x, const Object & y) {return x.distance_m < y.distance_m;});
  return objects;
}

}  // namespace prowl_ld19_driver
