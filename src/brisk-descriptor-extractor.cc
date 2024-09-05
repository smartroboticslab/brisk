/*
 Copyright (C) 2011  The Autonomous Systems Lab, ETH Zurich,
 Stefan Leutenegger, Simon Lynen and Margarita Chli.

 Copyright (C) 2013  The Autonomous Systems Lab, ETH Zurich,
 Stefan Leutenegger and Simon Lynen.

 All rights reserved.

 This is the Author's implementation of BRISK: Binary Robust Invariant
 Scalable Keypoints [1]. Various (partly unpublished) extensions are provided,
 some of which are described in [2].

 [1] Stefan Leutenegger, Margarita Chli and Roland Siegwart. BRISK: Binary
     Robust Invariant Scalable Keypoints. In Proceedings of the IEEE
     International Conference on Computer Vision (ICCV), 2011.

 [2] Stefan Leutenegger. Unmanned Solar Airplanes: Design and Algorithms for
     Efficient and Robust Autonomous Operation. Doctoral dissertation, 2014.

 This file is part of BRISK.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:
 * Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
 * Neither the name of the Autonomous Systems Lab, ETH Zurich nor the
   names of its contributors may be used to endorse or promote products
   derived from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <bitset>
#include <istream>  // NOLINT
#include <fstream>  // NOLINT
#include <iostream>  // NOLINT
#include <stdexcept>

#include <brisk/brisk-descriptor-extractor.h>

#include <brisk/internal/helper-structures.h>
#include <brisk/internal/integral-image.h>
#include <brisk/internal/macros.h>
#include <brisk/internal/pattern-provider.h>

namespace brisk {
const float BriskDescriptorExtractor::basicSize_ = 12.0;
const unsigned int BriskDescriptorExtractor::scales_ = 64;
// 40->4 Octaves - else, this needs to be adjusted...
const float BriskDescriptorExtractor::scalerange_ = 30;
// Discretization of the rotation look-up.
const unsigned int BriskDescriptorExtractor::n_rot_ = 1024;

// legacy BRISK 1.0
void BriskDescriptorExtractor::generateKernel(std::vector<float> &radiusList,
                                              std::vector<int> &numberList,
                                              float dMax, float dMin,
                                              std::vector<int> indexChange) {

  dMax_ = dMax;
  dMin_ = dMin;

  // Get the total number of points.
  const int rings = radiusList.size();
  assert(radiusList.size() != 0 && radiusList.size() == numberList.size());
  points_ = 0;  // remember the total number of points.
  for (int ring = 0; ring < rings; ring++) {
    points_ += numberList[ring];
  }
  // Set up the patterns.
  patternPoints_ = new BriskPatternPoint[points_ * scales_ * n_rot_];
  BriskPatternPoint* patternIterator = patternPoints_;

  // Define the scale discretization:
  static const float lb_scale = log(scalerange_) / log(2.0);
  static const float lb_scale_step = lb_scale / (scales_);

  scaleList_ = new float[scales_];
  sizeList_ = new unsigned int[scales_];

  const float sigma_scale = 1.3;

  for (unsigned int scale = 0; scale < scales_; ++scale) {
    scaleList_[scale] = pow((double) 2.0, (double) (scale * lb_scale_step));
    sizeList_[scale] = 0;

    // Generate the pattern points look-up.
    double alpha, theta;
    for (size_t rot = 0; rot < n_rot_; ++rot) {
      // This is the rotation of the feature.
      theta = double(rot) * 2 * M_PI / double(n_rot_);
      for (int ring = 0; ring < rings; ++ring) {
        for (int num = 0; num < numberList[ring]; ++num) {
          // The actual coordinates on the circle.
          alpha = (double(num)) * 2 * M_PI / double(numberList[ring]);
          patternIterator->x = scaleList_[scale] * radiusList[ring]
              * cos(alpha + theta);  // Feature rotation + angle of the point.
          patternIterator->y = scaleList_[scale] * radiusList[ring]
              * sin(alpha + theta);
          // And the gaussian kernel sigma.
          if (ring == 0) {
            patternIterator->sigma = sigma_scale * scaleList_[scale] * 0.5;
          } else {
            patternIterator->sigma = sigma_scale * scaleList_[scale]
                * (double(radiusList[ring])) * sin(M_PI / numberList[ring]);
          }

          // Adapt the sizeList if necessary.
          const unsigned int size = ceil(
              ((scaleList_[scale] * radiusList[ring]) + patternIterator->sigma))
              + 1;
          if (sizeList_[scale] < size) {
            sizeList_[scale] = size;
          }

          ++patternIterator;
        }
      }
    }
  }

  // Now also generate pairings.
  shortPairs_ = new BriskShortPair[points_ * (points_ - 1) / 2];
  longPairs_ = new BriskLongPair[points_ * (points_ - 1) / 2];
  noShortPairs_ = 0;
  noLongPairs_ = 0;

  // Fill indexChange with 0..n if empty.
  unsigned int indSize = indexChange.size();
  if (indSize == 0) {
    indexChange.resize(points_ * (points_ - 1) / 2);
    indSize = indexChange.size();
    for (unsigned int i = 0; i < indSize; i++) {
      indexChange[i] = i;
    }
  }
  const float dMin_sq = dMin_ * dMin_;
  const float dMax_sq = dMax_ * dMax_;
  for (unsigned int i = 1; i < points_; i++) {
    for (unsigned int j = 0; j < i; j++) {  // Find all the pairs.
      // Point pair distance:
      const float dx = patternPoints_[j].x - patternPoints_[i].x;
      const float dy = patternPoints_[j].y - patternPoints_[i].y;
      const float norm_sq = (dx * dx + dy * dy);
      if (norm_sq > dMin_sq) {
        // Save to long pairs.
        BriskLongPair& longPair = longPairs_[noLongPairs_];
        longPair.weighted_dx = int((dx / (norm_sq)) * 2048.0 + 0.5);
        longPair.weighted_dy = int((dy / (norm_sq)) * 2048.0 + 0.5);
        longPair.i = i;
        longPair.j = j;
        ++noLongPairs_;
      }
      if (norm_sq < dMax_sq) {
        // Save to short pairs.
        assert(noShortPairs_ < indSize);
        BriskShortPair& shortPair = shortPairs_[indexChange[noShortPairs_]];
        shortPair.j = j;
        shortPair.i = i;
        ++noShortPairs_;
      }
    }
  }

// Number of bytes the descriptor consists of:
strings_=(int)ceil((float(noShortPairs_))/128.0)*4*4;

}

void BriskDescriptorExtractor::InitFromStream(bool rotationInvariant,
                                              bool scaleInvariant,
                                              std::istream& pattern_stream,
                                              float patternScale) {
  dMax_ = 0;
  dMin_ = 0;
  rotationInvariance = rotationInvariant;
  scaleInvariance = scaleInvariant;

  assert(pattern_stream.good());

  // Read number of points.
  pattern_stream >> points_;

  // Set up the patterns.
  patternPoints_ = new brisk::BriskPatternPoint[points_ * scales_ * n_rot_];
  brisk::BriskPatternPoint* patternIterator = patternPoints_;

  // Define the scale discretization:
  static const float lb_scale = log(scalerange_) / log(2.0);
  static const float lb_scale_step = lb_scale / (scales_);

  scaleList_ = new float[scales_];
  sizeList_ = new unsigned int[scales_];

  const float sigma_scale = 1.3;

  // First fill the unscaled and unrotated pattern:
  float* u_x = new float[points_];
  float* u_y = new float[points_];
  float* sigma = new float[points_];
  for (unsigned int i = 0; i < points_; i++) {
    pattern_stream >> u_x[i]; u_x[i]*=patternScale;
    pattern_stream >> u_y[i]; u_y[i]*=patternScale;
    pattern_stream >> sigma[i]; sigma[i]*=patternScale;
  }

  // Now fill all the scaled and rotated versions.
  for (unsigned int scale = 0; scale < scales_; ++scale) {
    scaleList_[scale] = pow(2.0, static_cast<double>(scale * lb_scale_step));
    sizeList_[scale] = 0;

    // Generate the pattern points look-up.
    double theta;
    for (size_t rot = 0; rot < n_rot_; ++rot) {
      for (unsigned int i = 0; i < points_; i++) {
        // This is the rotation of the feature.
        theta = static_cast<double>(rot) * 2 * M_PI
            / static_cast<double>(n_rot_);
        // Feature rotation plus angle of the point.
        patternIterator->x = scaleList_[scale]
            * (u_x[i] * cos(theta) - u_y[i] * sin(theta));
        patternIterator->y = scaleList_[scale]
            * (u_x[i] * sin(theta) + u_y[i] * cos(theta));
        // And the Gaussian kernel sigma.
        patternIterator->sigma = sigma_scale * scaleList_[scale] * sigma[i];

        // Adapt the sizeList if necessary.
        const unsigned int size = ceil(
            ((sqrt(
                patternIterator->x * patternIterator->x
                    + patternIterator->y * patternIterator->y))
                + patternIterator->sigma)) + 1;
        if (sizeList_[scale] < size) {
          sizeList_[scale] = size;
        }

        // Increment the iterator.
        ++patternIterator;
      }
    }
  }

  // Now also generate pairings.
  pattern_stream >> noShortPairs_;
  shortPairs_ = new brisk::BriskShortPair[noShortPairs_];
  for (unsigned int p = 0; p < noShortPairs_; p++) {
    unsigned int i, j;
    pattern_stream >> i;
    shortPairs_[p].i = i;
    pattern_stream >> j;
    shortPairs_[p].j = j;
  }

  pattern_stream >> noLongPairs_;
  longPairs_ = new brisk::BriskLongPair[noLongPairs_];
  for (unsigned int p = 0; p < noLongPairs_; p++) {
    unsigned int i, j;
    pattern_stream >> i;
    longPairs_[p].i = i;
    pattern_stream >> j;
    longPairs_[p].j = j;
    float dx = (u_x[j] - u_x[i]);
    float dy = (u_y[j] - u_y[i]);
    float norm_sq = dx * dx + dy * dy;
    longPairs_[p].weighted_dx =
        static_cast<int>((dx / (norm_sq)) * 2048.0 + 0.5);
    longPairs_[p].weighted_dy =
        static_cast<int>((dy / (norm_sq)) * 2048.0 + 0.5);
  }

  // Number of bytes in the descriptor.
  strings_ = static_cast<int>(ceil((static_cast<float>(noShortPairs_)) / 128.0))
      * 4 * 4;

  constexpr int kDescriptorLength = 384;
  if(noShortPairs_ != kDescriptorLength){
    throw std::runtime_error("short pairs must be equal descriptor length");
  }

  delete[] u_x;
  delete[] u_y;
  delete[] sigma;
}

BriskDescriptorExtractor::BriskDescriptorExtractor(bool rotationInvariant,
                                                   bool scaleInvariant, int version, float patternScale) {
  if(!(version==Version::briskV1 || version==Version::briskV2)){
    throw std::runtime_error("unknown BRISK Version");
  }
  if(version==Version::briskV2){
    std::stringstream ss;
    brisk::GetDefaultPatternAsStream(&ss);
    InitFromStream(rotationInvariant, scaleInvariant, ss, patternScale);
  } else if(version==Version::briskV1){
    std::vector<float> rList;
    std::vector<int> nList;

    // This is the standard pattern found to be suitable also.
    rList.resize(5);
    nList.resize(5);
    const double f = 0.85*patternScale;

    rList[0] = f * 0;
    rList[1] = f * 2.9;
    rList[2] = f * 4.9;
    rList[3] = f * 7.4;
    rList[4] = f * 10.8;

    nList[0] = 1;
    nList[1] = 10;
    nList[2] = 14;
    nList[3] = 15;
    nList[4] = 20;

    rotationInvariance = rotationInvariant;
    scaleInvariance = scaleInvariant;
    generateKernel(rList, nList, 5.85 , 8.2 );
  } else {
    throw std::runtime_error("only Version::briskV1 or Version::briskV2 supported!");
  }
}

BriskDescriptorExtractor::BriskDescriptorExtractor(const std::string& fname,
                                                   bool rotationInvariant,
                                                   bool scaleInvariant,
                                                   float patternScale) {
  std::ifstream myfile(fname.c_str());
  assert(myfile.is_open());

  InitFromStream(rotationInvariant, scaleInvariant, myfile, patternScale);

  myfile.close();
}

// Simple alternative:
template<typename ImgPixel_T, typename IntegralPixel_T>
__inline__ IntegralPixel_T BriskDescriptorExtractor::SmoothedIntensity(
    const cv::Mat& image, const cv::Mat& integral, const float key_x,
    const float key_y, const unsigned int scale, const unsigned int rot,
    const unsigned int point, const float warp[4], const float warpScale) const {
  // Get the float position.
  const brisk::BriskPatternPoint& briskPoint0 = patternPoints_[scale * n_rot_
      * points_ + rot * points_ + point];

  brisk::BriskPatternPoint briskPoint1 = briskPoint0;
  if(warp) {
    // account for camera model
    briskPoint1.x = warp[0]*briskPoint0.x + warp[1]*briskPoint0.y;
    briskPoint1.y = warp[2]*briskPoint0.x + warp[3]*briskPoint0.y;
    briskPoint1.sigma = warpScale * briskPoint0.sigma; // this should be 2d transformed in theory...
  }
  const brisk::BriskPatternPoint briskPoint = warp ? briskPoint1 : briskPoint0;

  const float xf = briskPoint.x + key_x;
  const float yf = briskPoint.y + key_y;
  const int x = static_cast<int>(xf);
  const int y = static_cast<int>(yf);
  const int& imagecols = image.cols;

  // Get the sigma:
  const float sigma_half = briskPoint.sigma;
  const float area = 4.0 * sigma_half * sigma_half;

  // Calculate borders.
  const float x_1 = xf - sigma_half;
  const float x1 = xf + sigma_half;
  const float y_1 = yf - sigma_half;
  const float y1 = yf + sigma_half;

  // Calculate output:
  int ret_val;
  if (sigma_half < 0.5) {
    // check outside image
    if(x < 0) return -1;
    if(x > (image.cols-2)) return -1;
    if(y < 0) return -1;
    if(y > (image.rows-2)) return -1;

    // Interpolation multipliers:
    const int r_x = (xf - x) * 1024;
    const int r_y = (yf - y) * 1024;
    const int r_x_1 = (1024 - r_x);
    const int r_y_1 = (1024 - r_y);
    ImgPixel_T* ptr = reinterpret_cast<ImgPixel_T*>(image.data) + x
        + y * imagecols;
    // Just interpolate:
    ret_val = (r_x_1 * r_y_1 * IntegralPixel_T(*ptr));
    ptr++;
    ret_val += (r_x * r_y_1 * IntegralPixel_T(*ptr));
    ptr += imagecols;
    ret_val += (r_x * r_y * IntegralPixel_T(*ptr));
    ptr--;
    ret_val += (r_x_1 * r_y * IntegralPixel_T(*ptr));
    return (ret_val) / 1024;
  }

  // This is the standard case (simple, not speed optimized yet):
  if(x_1 < 0.0f) return -1;
  if(x1 > float(image.cols-1)) return -1;
  if(y_1 < 0.0f) return -1;
  if(y1 > float(image.rows-1)) return -1;

  // Scaling:
  const IntegralPixel_T scaling = 4194304.0 / area;
  const IntegralPixel_T scaling2 = static_cast<float>(scaling) * area / 1024.0;

  // The integral image is larger:
  const int integralcols = imagecols + 1;

  const int x_left = static_cast<int>(x_1 + 0.5);
  const int y_top = static_cast<int>(y_1 + 0.5);
  const int x_right = static_cast<int>(x1 + 0.5);
  const int y_bottom = static_cast<int>(y1 + 0.5);

  // Overlap area - multiplication factors:
  const float r_x_1 = static_cast<float>(x_left) - x_1 + 0.5;
  const float r_y_1 = static_cast<float>(y_top) - y_1 + 0.5;
  const float r_x1 = x1 - static_cast<float>(x_right) + 0.5;
  const float r_y1 = y1 - static_cast<float>(y_bottom) + 0.5;
  const int dx = x_right - x_left - 1;
  const int dy = y_bottom - y_top - 1;
  const IntegralPixel_T A = (r_x_1 * r_y_1) * scaling;
  const IntegralPixel_T B = (r_x1 * r_y_1) * scaling;
  const IntegralPixel_T C = (r_x1 * r_y1) * scaling;
  const IntegralPixel_T D = (r_x_1 * r_y1) * scaling;
  const IntegralPixel_T r_x_1_i = r_x_1 * scaling;
  const IntegralPixel_T r_y_1_i = r_y_1 * scaling;
  const IntegralPixel_T r_x1_i = r_x1 * scaling;
  const IntegralPixel_T r_y1_i = r_y1 * scaling;

  if (dx + dy > 2) {
    // Now the calculation:
    ImgPixel_T* ptr = reinterpret_cast<ImgPixel_T*>(image.data) + x_left
        + imagecols * y_top;
    // First the corners:
    ret_val = A * IntegralPixel_T(*ptr);
    ptr += dx + 1;
    ret_val += B * IntegralPixel_T(*ptr);
    ptr += dy * imagecols + 1;
    ret_val += C * IntegralPixel_T(*ptr);
    ptr -= dx + 1;
    ret_val += D * IntegralPixel_T(*ptr);

    // Next the edges:
    IntegralPixel_T* ptr_integral = reinterpret_cast<IntegralPixel_T*>(integral
        .data) + x_left + integralcols * y_top + 1;
    // Find a simple path through the different surface corners.
    const IntegralPixel_T tmp1 = (*ptr_integral);
    ptr_integral += dx;
    const IntegralPixel_T tmp2 = (*ptr_integral);
    ptr_integral += integralcols;
    const IntegralPixel_T tmp3 = (*ptr_integral);
    ptr_integral++;
    const IntegralPixel_T tmp4 = (*ptr_integral);
    ptr_integral += dy * integralcols;
    const IntegralPixel_T tmp5 = (*ptr_integral);
    ptr_integral--;
    const IntegralPixel_T tmp6 = (*ptr_integral);
    ptr_integral += integralcols;
    const IntegralPixel_T tmp7 = (*ptr_integral);
    ptr_integral -= dx;
    const IntegralPixel_T tmp8 = (*ptr_integral);
    ptr_integral -= integralcols;
    const IntegralPixel_T tmp9 = (*ptr_integral);
    ptr_integral--;
    const IntegralPixel_T tmp10 = (*ptr_integral);
    ptr_integral -= dy * integralcols;
    const IntegralPixel_T tmp11 = (*ptr_integral);
    ptr_integral++;
    const IntegralPixel_T tmp12 = (*ptr_integral);

    // Assign the weighted surface integrals:
    const IntegralPixel_T upper = (tmp3 - tmp2 + tmp1 - tmp12) * r_y_1_i;
    const IntegralPixel_T middle = (tmp6 - tmp3 + tmp12 - tmp9) * scaling;
    const IntegralPixel_T left = (tmp9 - tmp12 + tmp11 - tmp10) * r_x_1_i;
    const IntegralPixel_T right = (tmp5 - tmp4 + tmp3 - tmp6) * r_x1_i;
    const IntegralPixel_T bottom = (tmp7 - tmp6 + tmp9 - tmp8) * r_y1_i;

    return IntegralPixel_T(
        (ret_val + upper + middle + left + right + bottom) / scaling2);
  }

  // Now the calculation:
  ImgPixel_T* ptr = reinterpret_cast<ImgPixel_T*>(image.data) + x_left
      + imagecols * y_top;
  // First row:
  ret_val = A * IntegralPixel_T(*ptr);
  ptr++;
  const ImgPixel_T* end1 = ptr + dx;
  for (; ptr < end1; ptr++) {
    ret_val += r_y_1_i * IntegralPixel_T(*ptr);
  }
  ret_val += B * IntegralPixel_T(*ptr);
  // Middle ones:
  ptr += imagecols - dx - 1;
  const ImgPixel_T* end_j = ptr + dy * imagecols;
  for (; ptr < end_j; ptr += imagecols - dx - 1) {
    ret_val += r_x_1_i * IntegralPixel_T(*ptr);
    ptr++;
    const ImgPixel_T* end2 = ptr + dx;
    for (; ptr < end2; ptr++) {
      ret_val += IntegralPixel_T(*ptr) * scaling;
    }
    ret_val += r_x1_i * IntegralPixel_T(*ptr);
  }
  // Last row:
  ret_val += D * IntegralPixel_T(*ptr);
  ptr++;
  const ImgPixel_T* end3 = ptr + dx;
  for (; ptr < end3; ptr++) {
    ret_val += r_y1_i * IntegralPixel_T(*ptr);
  }
  ret_val += C * IntegralPixel_T(*ptr);

  return IntegralPixel_T((ret_val) / scaling2);
}

bool RoiPredicate(const float minX, const float minY, const float maxX,
                  const float maxY, const cv::KeyPoint& keyPt) {
  return (cv::KeyPointX(keyPt) < minX) || (cv::KeyPointX(keyPt) >= maxX)
      || (cv::KeyPointY(keyPt) < minY) || (cv::KeyPointY(keyPt) >= maxY);
}

void BriskDescriptorExtractor::setDescriptorBits(int keypoint_idx,
                                                 const int* values,
                                                 cv::Mat* descriptors) const {
  if(descriptors==nullptr){
    throw std::runtime_error("descriptors NULL");
  }
  unsigned char* ptr = descriptors->data + strings_ * keypoint_idx;

  // Now iterate through all the pairings.
  brisk::UINT32_ALIAS* ptr2 = reinterpret_cast<brisk::UINT32_ALIAS*>(ptr);
  const brisk::BriskShortPair* max = shortPairs_ + noShortPairs_;
  int shifter = 0;
  for (brisk::BriskShortPair* iter = shortPairs_; iter < max; ++iter) {
    int t1 = *(values + iter->i);
    int t2 = *(values + iter->j);
    if (t1 > t2) {
      *ptr2 |= ((1) << shifter);
    }  // Else already initialized with zero.
    // Take care of the iterators:
    ++shifter;
    if (shifter == 32) {
      shifter = 0;
      ++ptr2;
    }
  }
}

void BriskDescriptorExtractor::AllocateDescriptors(size_t count,
                                                   cv::Mat& descriptors) const {
  descriptors = cv::Mat::zeros(count, strings_, CV_8UC1);
}

void BriskDescriptorExtractor::doDescriptorComputation(const cv::Mat& image,
                               std::vector<cv::KeyPoint>& keypoints,
                               cv::Mat& descriptors) const {

  if(!image.isContinuous() || image.channels()!=1 || image.elemSize()!=1) {
    throw std::runtime_error("BRISK requires continuous 1-channel 8-bit images");
  }
  // Remove keypoints very close to the border.
    size_t ksize = keypoints.size();
    std::vector<int> kscales;  // Remember the scale per keypoint.
    kscales.resize(ksize);
    static const float log2 = 0.693147180559945;
    static const float lb_scalerange = log(scalerange_) / (log2);

    std::vector<cv::KeyPoint> valid_kp;
    std::vector<int> valid_scales;
    valid_kp.reserve(keypoints.size());
    valid_scales.reserve(keypoints.size());

    static const float basicSize06 = basicSize_ * 0.6;
    unsigned int basicscale = 0;
    if (!scaleInvariance)
      basicscale = std::max(
          static_cast<int>(scales_ / lb_scalerange
              * (log(1.45 * basicSize_ / (basicSize06)) / log2) + 0.5),
          0);
    for (size_t k = 0; k < ksize; k++) {
      unsigned int scale;
      if (scaleInvariance) {
        scale = std::max(
            static_cast<int>(scales_ / lb_scalerange
                * (log(cv::KeyPointSize(keypoints[k]) / (basicSize06)) / log2) + 0.5),
            0);
        // Saturate.
        if (scale >= scales_)
          scale = scales_ - 1;
        kscales[k] = scale;
      } else {
        scale = basicscale;
        kscales[k] = scale;
      }
      const int border = sizeList_[scale];
      const int border_x = image.cols - border;
      const int border_y = image.rows - border;
      if (!RoiPredicate(border, border, border_x, border_y, keypoints[k])) {
        valid_kp.push_back(keypoints[k]);
        valid_scales.push_back(kscales[k]);
      }
    }

    keypoints.swap(valid_kp);
    kscales.swap(valid_scales);
    ksize = keypoints.size();
    AllocateDescriptors(keypoints.size(), descriptors);

    // First, calculate the integral image over the whole image:
    // current integral image.

    cv::Mat _integral;  // The integral image.
    cv::Mat imageScaled;
    if (image.type() == CV_16UC1) {
      IntegralImage16(imageScaled, &_integral);
    } else if (image.type() == CV_8UC1) {
      IntegralImage8(image, &_integral);
    } else {
      throw std::runtime_error("Unsupported image format. Must be CV_16UC1 or CV_8UC1.");
    }

    int* _values = new int[points_];  // For temporary use.

    // Now do the extraction for all keypoints:
    for (size_t k = 0; k < ksize; ++k) {
      int theta;
      cv::KeyPoint& kp = keypoints[k];
      const int& scale = kscales[k];
      int* pvalues = _values;
      const float& x = cv::KeyPointX(kp);
      const float& y = cv::KeyPointY(kp);

      // compute warp / extraction direction, if provided
      float* warpptr = nullptr;
      float sigmaScale = 1.0;
      cv::Mat warp(2, 2, CV_32FC1);
      bool directional = false;
      if(!imageJacobians_.empty()) {
        // for bilinear interpolation
        int x0 = std::floor(kp.pt.x);
        int y0 = std::floor(kp.pt.y);
        float dx = kp.pt.x-x0;
        float dy = kp.pt.y-y0;

        float weight_tl = (1.0f - dx) * (1.0f - dy);
        float weight_tr = (dx)        * (1.0f - dy);
        float weight_bl = (1.0f - dx) * (dy);
        float weight_br = (dx)        * (dy);

        // ray direction interpolated
        cv::Vec3f dir_tl = rayDirections_.at<cv::Vec3f>(y0,x0);
        cv::Vec3f dir_tr = rayDirections_.at<cv::Vec3f>(y0,x0+1);
        cv::Vec3f dir_bl = rayDirections_.at<cv::Vec3f>(y0+1,x0);
        cv::Vec3f dir_br = rayDirections_.at<cv::Vec3f>(y0+1,x0+1);
        if(dir_tl.dot(dir_tl)>1e-12 && dir_tr.dot(dir_tr)>1e-12
           && dir_bl.dot(dir_bl)>1e-12 && dir_br.dot(dir_br)>1e-12) {
        cv::Vec3f dir
            = weight_tl* dir_tl
            + weight_tr* dir_tr
            + weight_bl* dir_bl
            + weight_br* dir_br;

        // local 3D pattern directions, scaled to virtual camera of focal length virtualFocalLength_
        cv::Vec3f eu = extractionDirection_.cross(dir);
        if(eu[0]*eu[0]+eu[1]*eu[1]+eu[2]*eu[2] > 0.01) { // 6 deg tolerance; make this a parameter!
          directional = true;
        }
        eu = 1.0f/virtualFocalLength_*cv::normalize(eu);
        cv::Vec3f ev = 1.0f/virtualFocalLength_*cv::normalize(dir.cross(eu));
        cv::Mat eu_ev(3, 2, CV_32FC1);
        eu_ev.at<float>(0,0) = eu[0];
        eu_ev.at<float>(1,0) = eu[1];
        eu_ev.at<float>(2,0) = eu[2];
        eu_ev.at<float>(0,1) = ev[0];
        eu_ev.at<float>(1,1) = ev[1];
        eu_ev.at<float>(2,1) = ev[2];

        // image jacobian as nearest-neighbour (should be close enough)
        int xm = std::round(kp.pt.x);
        int ym = std::round(kp.pt.y);
        auto j = imageJacobians_.at<cv::Vec6f>(ym,xm);
        cv::Mat J(2, 3, CV_32FC1);
        J.at<float>(0,0) = j[0];
        J.at<float>(0,1) = j[1];
        J.at<float>(0,2) = j[2];
        J.at<float>(1,0) = j[3];
        J.at<float>(1,1) = j[4];
        J.at<float>(1,2) = j[5];

        // now compute the warp...
        warp = J * eu_ev;
        warpptr = (float*)(warp.data);

        // ... and finally scale also the sigmas (just 1D because we can't do skewed Gaussian blur)
        cv::Vec2f eigenvalues;
        if(cv::eigen(warp, eigenvalues)){
          sigmaScale = 0.5f*(fabs(eigenvalues[0]) + fabs(eigenvalues[1]));
        }

        if(directional) {
          kp.angle = atan2(warpptr[3], warpptr[1]) / M_PI * 180.0;
        }
        }
      }

      // compute angle
      //if(!directional) {
      //  kp.angle = -1; // not directional...
      //}
      if (cv::KeyPointAngle(kp) == -1) {
        if (!rotationInvariance) {
          // Don't compute the gradient direction, just assign a rotation of 0°.
          theta = 0;
        } else {
          // Get the gray values in the unrotated pattern.
          if (image.type() == CV_8UC1) {
            for (unsigned int i = 0; i < points_; i++) {
              *(pvalues++) = SmoothedIntensity<unsigned char, int>(image, _integral, x, y,
                                                           scale, 0, i);
            }
          } else {
            for (unsigned int i = 0; i < points_; i++) {
              *(pvalues++) = static_cast<int>(65536.0
                  * SmoothedIntensity<float, float>(imageScaled, _integral, x, y,
                                                    scale, 0, i));
            }
          }
          int direction0 = 0;
          int direction1 = 0;
          // Now iterate through the long pairings.
          const brisk::BriskLongPair* max = longPairs_ + noLongPairs_;
          for (brisk::BriskLongPair* iter = longPairs_; iter < max; ++iter) {
            int t1 = *(_values + iter->i);
            int t2 = *(_values + iter->j);
            const int delta_t = (t1 - t2);
            // Update the direction:
            const int tmp0 = delta_t * (iter->weighted_dx) / 1024;
            const int tmp1 = delta_t * (iter->weighted_dy) / 1024;
            direction0 += tmp0;
            direction1 += tmp1;
          }
          kp.angle = atan2(static_cast<float>(direction1),
                           static_cast<float>(direction0)) / M_PI * 180.0;
          theta = static_cast<int>((n_rot_ * cv::KeyPointAngle(kp)) /
                                   (360.0) + 0.5);
          if (theta < 0)
            theta += n_rot_;
          if (theta >= static_cast<int>(n_rot_))
            theta -= n_rot_;
        }
      } else {
        // Figure out the direction:
        if (!rotationInvariance) {
          theta = 0;
        } else {
          theta = static_cast<int>(n_rot_ * (cv::KeyPointAngle(kp) /
              (360.0)) + 0.5);
          if (theta < 0)
            theta += n_rot_;
          if (theta >= static_cast<int>(n_rot_))
            theta -= n_rot_;
        }
      }

      // Now also extract the stuff for the actual direction:
      // Let us compute the smoothed values.
      pvalues = _values;

      // Get the gray values in the rotated pattern.
      if (image.type() == CV_8UC1) {
        for (unsigned int i = 0; i < points_; i++) {
          *(pvalues++) = SmoothedIntensity<unsigned char, int>(
                image, _integral, x, y, scale, directional ? 0 : theta, i, warpptr, sigmaScale);
        }
      } else {
        for (unsigned int i = 0; i < points_; i++) {
          *(pvalues++) = static_cast<int>(
                65536.0 * SmoothedIntensity<float, float>(
                  image, _integral, x, y, scale, directional ? 0 : theta, i, warpptr, sigmaScale));
        }
      }

      setDescriptorBits(k, _values, &descriptors);
    }
    delete[] _values;
}

int BriskDescriptorExtractor::descriptorSize() const {
  return strings_;
}

int BriskDescriptorExtractor::descriptorType() const {
  return CV_8U;
}

BriskDescriptorExtractor::~BriskDescriptorExtractor() {
  delete[] patternPoints_;
  delete[] shortPairs_;
  delete[] longPairs_;
  delete[] scaleList_;
  delete[] sizeList_;
}
}  // namespace brisk
