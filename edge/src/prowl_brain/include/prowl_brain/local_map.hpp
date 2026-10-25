#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "prowl_brain/types.hpp"

namespace prowl_brain
{

// Settings for the map. Loaded from config/brain.yaml.
struct LocalMapConfig
{
  double resolution = 0.05;     // m per cell (5 cm squares)
  double size = 20.0;           // m, the map is size x size, centered where the robot started
  double hit_increment = 0.35;  // how much "more wall" a cell gets each time a beam ends in it
  double miss_decrement = 0.1;  // how much "less wall" a cell gets each time a beam flies through it
};

// A picture of the explored part of the map, for the robot's screen (MapDisplay).
// Thread 2 makes one every so often and hands it to thread 1, which draws it.
struct MapImage
{
  int width = 0;   // pixels; one pixel per map cell
  int height = 0;
  // width * height grey levels, row 0 at the TOP (screen order, +y up the screen):
  // 0 = wall, 255 = empty floor, 128 = not seen yet.
  std::vector<uint8_t> pixels;
  // Where the robot is, in pixels from the image's top-left corner (pixel i spans i..i+1),
  // and which way it faces: yaw 0 points right on screen, growing counter-clockwise.
  double robot_x = 0.0;
  double robot_y = 0.0;
  double robot_yaw = 0.0;
  double resolution = 0.0;  // m per pixel
  double stamp = 0.0;       // s, the scan that last went into the map
};

// The robot's memory of the room: a big sheet of graph paper laid over the
// floor. Each square (cell) holds a number from 0 to 1 meaning "how sure are
// we there's a wall here?". 0 = definitely empty floor, 1 = definitely wall,
// 0.5 = no idea yet.
//
// The matcher's whole job is: "slide and turn the new scan around until its
// dots land on the squares that are already marked as walls". So the map has
// to answer "how wall-ish is this spot?" for any (x, y), even between the
// centers of squares. Ceres also needs to know which way to slide to get
// more wall-ish (the slope), so the values are blended smoothly between
// squares (bicubic interpolation, done in the matcher with Ceres's own
// BiCubicInterpolator). This class just stores the squares.
//
// Only thread 2 (the matcher) touches the map, so it has no lock.
class LocalMap
{
public:
  explicit LocalMap(const LocalMapConfig & config);

  // Wipe the paper: every cell back to 0.5 ("no idea").
  void reset();

  // Draw one scan onto the map, as seen from `pose`. For each of the 720 beams:
  // the cell where it ended gets more wall-ish, and the cells it flew through
  // on the way get less wall-ish.
  void insertScan(const LidarFrame & scan, const Pose2D & pose);

  // A picture of every cell that's been seen, plus the robot, with `margin` meters
  // of border around them (cut off at the edge of the map).
  MapImage image(const Pose2D & robot, double stamp, double margin) const;

  // Has at least one scan been drawn? (The matcher can't match against blank paper.)
  bool empty() const {return scans_inserted_ == 0;}
  std::size_t scansInserted() const {return scans_inserted_;}

  // Raw access for the interpolator: cells in rows (row = y, column = x),
  // row 0 at the bottom (most negative y).
  const std::vector<double> & cells() const {return cells_;}
  int rows() const {return rows_;}
  int cols() const {return cols_;}

  // Turn a floor position (m) into a fractional cell position, e.g. (12.5, 40.0)
  // means halfway between columns 12 and 13 in row 40. A template so Ceres can
  // push its special "number that remembers its own slope" type (a Jet)
  // through it as well as plain doubles.
  //
  // The origin is cell (0, 0)'s outer corner, so cell c spans [c, c + 1) cells
  // from it and its center is at c + 0.5. The interpolator puts each cell's value
  // at a whole number, so we subtract that half cell: a point at a cell's center
  // comes out as exactly (col, row). Rounding the result gives the cell a point is in.
  template<typename T>
  void worldToGrid(const T & x, const T & y, T & col, T & row) const
  {
    col = (x - origin_x_) / config_.resolution - 0.5;
    row = (y - origin_y_) / config_.resolution - 0.5;
  }

private:
  // Mark one cell (by integer column/row) as more or less wall-ish, keeping it in [0, 1].
  void addToCell(int col, int row, double amount);
  // Walk the cells along a straight line from (c0, r0) to (c1, r1), calling
  // addToCell(..., -miss_decrement) on each one except the last.
  void traceMisses(int c0, int r0, int c1, int r1);

  LocalMapConfig config_;
  int rows_ = 0;
  int cols_ = 0;
  double origin_x_ = 0.0;  // m, the floor position of cell (0, 0)'s outer corner
  double origin_y_ = 0.0;
  std::vector<double> cells_;
  std::size_t scans_inserted_ = 0;
};

}  // namespace prowl_brain
