/*
 Copyright (C) 2011  The Autonomous Systems Lab, ETH Zurich,
 Stefan Leutenegger, Simon Lynen and Margarita Chli.

 Copyright (C) 2013  The Autonomous Systems Lab, ETH Zurich,
 Stefan Leutenegger and Simon Lynen.

 Copyright (C) 2020  Smart Robotics Lab, Imperial College London
 Stefan Leutenegger

 Copyright (C) 2024  Smart Robotics Lab, Technical University of Munich
 Stefan Leutenegger

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

#ifndef INTERNAL_HAMMING_INL_H_
#define INTERNAL_HAMMING_INL_H_

#include <stdexcept>
#include <brisk/internal/neon-helpers.h>

namespace brisk {

#ifdef __GNUC__
static const char __attribute__((aligned(16))) MASK_4bit[16] =
  {0xf, 0xf, 0xf,  0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf,
   0xf};
static const uint8_t __attribute__((aligned(16))) POPCOUNT_4bit[16] = {0, 1, 1,
  2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4};
#endif  // __GNUC__
#ifdef _MSC_VER
__declspec(align(16)) static const char MASK_4bit[16] = {0xf, 0xf, 0xf, 0xf,
  0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf, 0xf};
__declspec(align(16)) static const uint8_t POPCOUNT_4bit[16] = {0, 1, 1, 2,
  1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4};
static const __m128i shiftval = _mm_set_epi32(0, 0, 0, 4);
#endif  // _MSC_VER

#ifdef __ARM_NEON__
__inline__ uint32_t Hamming::NEONPopcntofXORed(const uint8x16_t* signature1,
                                               const uint8x16_t* signature2,
                                               const int numberOf128BitWords) {
  if(signature1 == nullptr || signature2 == nullptr){
    throw std::runtime_error("signature NULL");
  }
  uint32_t result = 0;

  for (int i = 0; i < numberOf128BitWords; ++i) {
    uint8x16_t xor_result = veorq_u8(signature1[i], signature2[i]);
    uint64_t buf[2];
    vst1q_u64(buf, xor_result);
    result += __builtin_popcountll(buf[0]);
    result += __builtin_popcountll(buf[1]);
  }
  return result;
}
#else
// - SSSE3 - better alorithm, minimized psadbw usage -
// adapted from http://wm.ite.pl/articles/sse-popcount.html
__inline__ uint32_t Hamming::SSSE3PopcntofXORed(const __m128i* signature1,
const __m128i*signature2,
const int numberOf128BitWords) {
  if(signature1 == nullptr || signature2 == nullptr){
    throw std::runtime_error("signature NULL");
  }

  uint32_t result = 0;

  __m128i xmm0;
  __m128i xmm1;
  __m128i xmm2;
  __m128i xmm3;
  __m128i xmm4;
  __m128i xmm5;
  __m128i xmm6;
  __m128i xmm7;

  xmm7 = _mm_load_si128(reinterpret_cast<const __m128i*>(POPCOUNT_4bit));
  xmm6 = _mm_load_si128(reinterpret_cast<const __m128i*>(MASK_4bit));
  xmm5 = _mm_setzero_si128();  // xmm5 -- global accumulator.

  const __m128i* end = signature1 + numberOf128BitWords;
  xmm4 = xmm5;  // xmm4 -- local accumulator.
  const __m128i shiftval = _mm_set_epi32(0, 0, 0, 4);
  do {
    xmm0 = _mm_xor_si128(*signature1++, *signature2++);
    xmm1 = xmm0;
    xmm1 = _mm_srl_epi16(xmm1, shiftval);
    xmm0 = _mm_and_si128(xmm0, xmm6);  // xmm0 := lower nibbles.
    xmm1 = _mm_and_si128(xmm1, xmm6);  // xmm1 := higher nibbles.
    xmm2 = xmm7;
    xmm3 = xmm7;  // Get popcount.
    xmm2 = _mm_shuffle_epi8(xmm2, xmm0);  // For all nibbles.
    xmm3 = _mm_shuffle_epi8(xmm3, xmm1);  // Using PSHUFB.
    xmm4 = _mm_add_epi8(xmm4, xmm2);  // Update local.
    xmm4 = _mm_add_epi8(xmm4, xmm3);  // Accumulator.
  }while (signature1 < end);
  // Update global accumulator(two 32-bits counters).
  xmm4 = _mm_sad_epu8(xmm4, xmm5);
  xmm5 = _mm_add_epi32(xmm5, xmm4);
  // finally add together 32-bits counters stored in global accumulator.
  // __asm__ volatile(
  //  "movhlps  %%xmm5, %%xmm0 \n"
  // TODO(slynen): fix with appropriate intrinsic
  xmm0 = _mm_cvtps_epi32(_mm_movehl_ps(_mm_cvtepi32_ps(xmm0),
      _mm_cvtepi32_ps(xmm5)));
  xmm0 = _mm_add_epi32(xmm0, xmm5);
  result = _mm_cvtsi128_si32(xmm0);
  return result;
}
#endif  // __ARM_NEON__

}  // namespace brisk
#endif  // INTERNAL_HAMMING_INL_H_
