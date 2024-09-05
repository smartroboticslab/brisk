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

#ifdef __ARM_NEON__
#include <arm_neon.h>
#include <sse2neon/SSE2NEON.h>
#else
#include <emmintrin.h>
#include <tmmintrin.h>
#endif
#include <stdint.h>
#include <brisk/internal/harris-scores.h>

namespace brisk {
// This is a straightforward harris corner implementation.
// This is REALLY bad, it performs so many passes through the data...
void HarrisScoresSSE(const cv::Mat& src, cv::Mat& scores) {
  const int cols = src.cols;
  const int rows = src.rows;
  const int stride = src.step[0];
  const int maxJ = cols - 1 - 16;

  // Allocate stuff.
  int16_t *DxDx1, *DyDy1, *DxDy1;
  scores = cv::Mat::zeros(rows, cols, CV_32S);
  DxDx1 = new int16_t[rows * cols];
  DxDy1 = new int16_t[rows * cols];
  DyDy1 = new int16_t[rows * cols];

  // Masks.
#ifdef __ARM_NEON__
  // different order of initialisation
  uint8x16_t mask_hi = {0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF,
                        0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF};
  uint8x16_t mask_lo = {0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00,
                        0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00};

  // Consts.
  int16x8_t const_3_epi16 = {3, 3, 3, 3, 3, 3, 3, 3};
  int16x8_t const_10_epi16 = {10, 10, 10, 10, 10, 10, 10, 10};
#else //__ARM_NEON__
  __m128i mask_lo = _mm_set_epi8(0, -1, 0, -1, 0, -1, 0, -1,
                                 0, -1, 0, -1, 0, -1, 0, -1);
  __m128i mask_hi = _mm_set_epi8(-1, 0, -1, 0, -1, 0, -1, 0,
                                 -1, 0, -1, 0, -1, 0, -1, 0);

  // Consts.
  __m128i const_3_epi16 = _mm_set_epi16(3, 3, 3, 3, 3, 3, 3, 3);
  __m128i const_10_epi16 = _mm_set_epi16(10, 10, 10, 10, 10, 10, 10, 10);
#endif //__ARM_NEON__

  // Calculate gradients and products.
  const unsigned char* data = src.data;
  for (int i = 1; i < rows - 1; ++i) {
    bool end = false;
    for (int j = 1; j < cols - 1;) {
      // Load.
      const __m128i src_m1_m1 = _mm_loadu_si128(
          reinterpret_cast<const __m128i *>(data + (i - 1) * stride + j - 1));
      const __m128i src_m1_0 = _mm_loadu_si128(
          reinterpret_cast<const __m128i *>(data + (i - 1) * stride + j));
      const __m128i src_m1_p1 = _mm_loadu_si128(
          reinterpret_cast<const __m128i *>(data + (i - 1) * stride + j + 1));
      const __m128i src_0_m1 = _mm_loadu_si128(
          reinterpret_cast<const __m128i *>(data + (i) * stride + j - 1));
      const __m128i src_0_p1 = _mm_loadu_si128(
          reinterpret_cast<const __m128i *>(data + (i) * stride + j + 1));
      const __m128i src_p1_m1 = _mm_loadu_si128(
          reinterpret_cast<const __m128i *>(data + (i + 1) * stride + j - 1));
      const __m128i src_p1_0 = _mm_loadu_si128(
          reinterpret_cast<const __m128i *>(data + (i + 1) * stride + j));
      const __m128i src_p1_p1 = _mm_loadu_si128(
          reinterpret_cast<const __m128i *>(data + (i + 1) * stride + j + 1));

      // Scharr x.
      const __m128i dx_lo = _mm_slli_epi16(
          _mm_add_epi16(
              _mm_add_epi16(
                  _mm_mullo_epi16(
                      const_10_epi16,
                      _mm_sub_epi16(_mm_and_si128(mask_lo, src_0_m1),
                                    _mm_and_si128(mask_lo, src_0_p1))),
                  _mm_mullo_epi16(
                      const_3_epi16,
                      _mm_sub_epi16(_mm_and_si128(mask_lo, src_m1_m1),
                                    _mm_and_si128(mask_lo, src_m1_p1)))),
              _mm_mullo_epi16(
                  const_3_epi16,
                  _mm_sub_epi16(_mm_and_si128(mask_lo, src_p1_m1),
                                _mm_and_si128(mask_lo, src_p1_p1)))),
          3);
      const __m128i dx_hi = _mm_slli_epi16(
          _mm_add_epi16(
              _mm_add_epi16(
                  _mm_mullo_epi16(
                      const_10_epi16,
                      _mm_sub_epi16(
                          _mm_srli_si128(_mm_and_si128(mask_hi, src_0_m1), 1),
                          _mm_srli_si128(_mm_and_si128(mask_hi, src_0_p1), 1))),
                  _mm_mullo_epi16(
                      const_3_epi16,
                      _mm_sub_epi16(
                          _mm_srli_si128(_mm_and_si128(mask_hi, src_m1_m1), 1),
                          _mm_srli_si128(_mm_and_si128(mask_hi, src_m1_p1),
                                         1)))),
              _mm_mullo_epi16(
                  const_3_epi16,
                  _mm_sub_epi16(
                      _mm_srli_si128(_mm_and_si128(mask_hi, src_p1_m1), 1),
                      _mm_srli_si128(_mm_and_si128(mask_hi, src_p1_p1), 1)))),
                      3);

      // Scharr y.
      const __m128i dy_lo = _mm_slli_epi16(
          _mm_add_epi16(
              _mm_add_epi16(
                  _mm_mullo_epi16(
                      const_10_epi16,
                      _mm_sub_epi16(_mm_and_si128(mask_lo, src_m1_0),
                                    _mm_and_si128(mask_lo, src_p1_0))),
                  _mm_mullo_epi16(
                      const_3_epi16,
                      _mm_sub_epi16(_mm_and_si128(mask_lo, src_m1_m1),
                                    _mm_and_si128(mask_lo, src_p1_m1)))),
              _mm_mullo_epi16(
                  const_3_epi16,
                  _mm_sub_epi16(_mm_and_si128(mask_lo, src_m1_p1),
                                _mm_and_si128(mask_lo, src_p1_p1)))),
          3);
      const __m128i dy_hi = _mm_slli_epi16(
          _mm_add_epi16(
              _mm_add_epi16(
                  _mm_mullo_epi16(
                      const_10_epi16,
                      _mm_sub_epi16(
                          _mm_srli_si128(_mm_and_si128(mask_hi, src_m1_0), 1),
                          _mm_srli_si128(_mm_and_si128(mask_hi, src_p1_0), 1))),
                  _mm_mullo_epi16(
                      const_3_epi16,
                      _mm_sub_epi16(
                          _mm_srli_si128(_mm_and_si128(mask_hi, src_m1_m1), 1),
                          _mm_srli_si128(_mm_and_si128(mask_hi, src_p1_m1),
                                         1)))),
              _mm_mullo_epi16(
                  const_3_epi16,
                  _mm_sub_epi16(
                      _mm_srli_si128(_mm_and_si128(mask_hi, src_m1_p1), 1),
                      _mm_srli_si128(_mm_and_si128(mask_hi, src_p1_p1), 1)))),
          3);

      // Calculate dxdx dxdy dydy - since we have technically still chars, we
      // only need the lo part.
      const __m128i i_lo_dx_dx = _mm_mulhi_epi16(dx_lo, dx_lo);
      const __m128i i_lo_dy_dy = _mm_mulhi_epi16(dy_lo, dy_lo);
      const __m128i i_lo_dx_dy = _mm_mulhi_epi16(dx_lo, dy_lo);
      const __m128i i_hi_dx_dx = _mm_mulhi_epi16(dx_hi, dx_hi);
      const __m128i i_hi_dy_dy = _mm_mulhi_epi16(dy_hi, dy_hi);
      const __m128i i_hi_dx_dy = _mm_mulhi_epi16(dx_hi, dy_hi);

      // Unpack - interleave, store.
      _mm_storeu_si128(reinterpret_cast<__m128i *>(DxDx1 + i * cols + j),
                       _mm_unpacklo_epi16(i_lo_dx_dx, i_hi_dx_dx));
      _mm_storeu_si128(reinterpret_cast<__m128i *>(DxDx1 + i * cols + j + 8),
                       _mm_unpackhi_epi16(i_lo_dx_dx, i_hi_dx_dx));
      _mm_storeu_si128(reinterpret_cast<__m128i *>(DxDy1 + i * cols + j),
                       _mm_unpacklo_epi16(i_lo_dx_dy, i_hi_dx_dy));
      _mm_storeu_si128(reinterpret_cast<__m128i *>(DxDy1 + i * cols + j + 8),
                       _mm_unpackhi_epi16(i_lo_dx_dy, i_hi_dx_dy));
      _mm_storeu_si128(reinterpret_cast<__m128i *>(DyDy1 + i * cols + j),
                       _mm_unpacklo_epi16(i_lo_dy_dy, i_hi_dy_dy));
      _mm_storeu_si128(reinterpret_cast<__m128i *>(DyDy1 + i * cols + j + 8),
                       _mm_unpackhi_epi16(i_lo_dy_dy, i_hi_dy_dy));

      j += 16;
      if (j > maxJ && !end) {
        j = cols - 1 - 16;
        end = true;
      }
    }
  }

  // Smooth gradient products and calculate score.
  for (int i = 2; i < rows - 2; ++i) {
    for (int j = 2; j < cols - 2; j++) {
      // Load.
      // Calculate dxdx.
      const int16_t dxdx_m1_m1 = DxDx1[(i - 1) * cols + j - 1];
      const int16_t dxdx_m1_0 = DxDx1[(i - 1) * cols + j];
      const int16_t dxdx_m1_p1 = DxDx1[(i - 1) * cols + j + 1];
      const int16_t dxdx_0_m1 = DxDx1[(i) * cols + j - 1];
      const int16_t dxdx_0_0 = DxDx1[(i) * cols + j];
      const int16_t dxdx_0_p1 = DxDx1[(i) * cols + j + 1];
      const int16_t dxdx_p1_m1 = DxDx1[(i + 1) * cols + j - 1];
      const int16_t dxdx_p1_0 = DxDx1[(i + 1) * cols + j];
      const int16_t dxdx_p1_p1 = DxDx1[(i + 1) * cols + j + 1];

      // Gaussian smoothing.
      int dxdx = ((4 * static_cast<int>(dxdx_0_0)
          + 2 * (static_cast<int>(dxdx_m1_0) + static_cast<int>(dxdx_p1_0) +
              static_cast<int>(dxdx_0_m1) + static_cast<int>(dxdx_0_p1)) +
              (static_cast<int>(dxdx_m1_m1) + static_cast<int>(dxdx_m1_p1) +
                  static_cast<int>(dxdx_p1_m1) + static_cast<int>(dxdx_p1_p1)))
          >> 4);

      // Calculate dxdy.
      const int16_t dxdy_m1_m1 = DxDy1[(i - 1) * cols + j - 1];
      const int16_t dxdy_m1_0 = DxDy1[(i - 1) * cols + j];
      const int16_t dxdy_m1_p1 = DxDy1[(i - 1) * cols + j + 1];
      const int16_t dxdy_0_m1 = DxDy1[(i) * cols + j - 1];
      const int16_t dxdy_0_0 = DxDy1[(i) * cols + j];
      const int16_t dxdy_0_p1 = DxDy1[(i) * cols + j + 1];
      const int16_t dxdy_p1_m1 = DxDy1[(i + 1) * cols + j - 1];
      const int16_t dxdy_p1_0 = DxDy1[(i + 1) * cols + j];
      const int16_t dxdy_p1_p1 = DxDy1[(i + 1) * cols + j + 1];

      // Gaussian smoothing.
      int dxdy = ((4 * static_cast<int>(dxdy_0_0)
          + 2 * (static_cast<int>(dxdy_m1_0) + static_cast<int>(dxdy_p1_0) +
                  static_cast<int>(dxdy_0_m1) + static_cast<int>(dxdy_0_p1)) +
                  (static_cast<int>(dxdy_m1_m1) + static_cast<int>(dxdy_m1_p1) +
                      static_cast<int>(dxdy_p1_m1) +
                      static_cast<int>(dxdy_p1_p1))) >> 4);

      // Calculate dydy.
      const int16_t dydy_m1_m1 = DyDy1[(i - 1) * cols + j - 1];
      const int16_t dydy_m1_0 = DyDy1[(i - 1) * cols + j];
      const int16_t dydy_m1_p1 = DyDy1[(i - 1) * cols + j + 1];
      const int16_t dydy_0_m1 = DyDy1[(i) * cols + j - 1];
      const int16_t dydy_0_0 = DyDy1[(i) * cols + j];
      const int16_t dydy_0_p1 = DyDy1[(i) * cols + j + 1];
      const int16_t dydy_p1_m1 = DyDy1[(i + 1) * cols + j - 1];
      const int16_t dydy_p1_0 = DyDy1[(i + 1) * cols + j];
      const int16_t dydy_p1_p1 = DyDy1[(i + 1) * cols + j + 1];

      // Gaussian smoothing.
      int dydy = ((4 * static_cast<int>(dydy_0_0)
          + 2 * (static_cast<int>(dydy_m1_0) + static_cast<int>(dydy_p1_0) +
                  static_cast<int>(dydy_0_m1) + static_cast<int>(dydy_0_p1))
          + (static_cast<int>(dydy_m1_m1) + static_cast<int>(dydy_m1_p1) +
              static_cast<int>(dydy_p1_m1) + static_cast<int>(dydy_p1_p1)))
          >> 4);

      int trace_div_by_2 = ((dxdx) + (dydy)) >> 1;
      *(reinterpret_cast<int*>(scores.data) + i * stride + j) =
          ((((dxdx) * (dydy))) - (((dxdy) * (dxdy))))
          - ((((trace_div_by_2) * (trace_div_by_2)) / 5));
    }
  }

  delete[] DxDx1;
  delete[] DxDy1;
  delete[] DyDy1;
}
}  // namespace brisk
