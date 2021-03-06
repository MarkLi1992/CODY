//@HEADER
// ***************************************************
//
// HPCG: High Performance Conjugate Gradient Benchmark
//
// Contact:
// Michael A. Heroux ( maherou@sandia.gov)
// Jack Dongarra     (dongarra@eecs.utk.edu)
// Piotr Luszczek    (luszczek@eecs.utk.edu)
//
// ***************************************************
//@HEADER

/*!
 @file CheckAspectRatio.cpp

 HPCG routine
 */

#include "CheckAspectRatio.hpp"

#include "hpcg.hpp"

#include <algorithm>
#include <iostream>

int
CheckAspectRatio(double smallest_ratio, int x, int y, int z, const char *what, bool DoIo) {
  double current_ratio = std::min(std::min(x, y), z) / double(std::max(std::max(x, y), z));

  if (current_ratio < smallest_ratio) { // ratio of the smallest to the largest
    if (DoIo) {
        std::cerr << "The " << what << " sizes (" << x << "," << y << "," << z <<
        ") are invalid because the ratio min(x,y,z)/max(x,y,z)=" << current_ratio <<
        " is too small (at least " << smallest_ratio << " is required).";
        std::cerr << "The shape should resemble a 3D cube. Please adjust and try again.";
    }
    return 127;
  }

  return 0;
}
