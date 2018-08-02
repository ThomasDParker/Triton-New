// Copyright (c) 2014-2018, The Monero Project
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without modification, are
// permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this list of
//    conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice, this list
//    of conditions and the following disclaimer in the documentation and/or other
//    materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its contributors may be
//    used to endorse or promote products derived from this software without specific
//    prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
// THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
// THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Parts of this file are originally copyright (c) 2012-2013 The Cryptonote developers

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "common/int-util.h"
#include "crypto/hash.h"
#include "cryptonote_config.h"
#include "difficulty.h"
#include <boost/math/special_functions/round.hpp>

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "difficulty"

namespace cryptonote {

  using std::size_t;
  using std::uint64_t;
  using std::vector;

#if defined(__x86_64__)
  static inline void mul(uint64_t a, uint64_t b, uint64_t &low, uint64_t &high) {
    low = mul128(a, b, &high);
  }

#else

  static inline void mul(uint64_t a, uint64_t b, uint64_t &low, uint64_t &high) {
    // __int128 isn't part of the standard, so the previous function wasn't portable. mul128() in Windows is fine,
    // but this portable function should be used elsewhere. Credit for this function goes to latexi95.

    uint64_t aLow = a & 0xFFFFFFFF;
    uint64_t aHigh = a >> 32;
    uint64_t bLow = b & 0xFFFFFFFF;
    uint64_t bHigh = b >> 32;

    uint64_t res = aLow * bLow;
    uint64_t lowRes1 = res & 0xFFFFFFFF;
    uint64_t carry = res >> 32;

    res = aHigh * bLow + carry;
    uint64_t highResHigh1 = res >> 32;
    uint64_t highResLow1 = res & 0xFFFFFFFF;

    res = aLow * bHigh;
    uint64_t lowRes2 = res & 0xFFFFFFFF;
    carry = res >> 32;

    res = aHigh * bHigh + carry;
    uint64_t highResHigh2 = res >> 32;
    uint64_t highResLow2 = res & 0xFFFFFFFF;

    //Addition

    uint64_t r = highResLow1 + lowRes2;
    carry = r >> 32;
    low = (r << 32) | lowRes1;
    r = highResHigh1 + highResLow2 + carry;
    uint64_t d3 = r & 0xFFFFFFFF;
    carry = r >> 32;
    r = highResHigh2 + carry;
    high = d3 | (r << 32);
  }

#endif

  static inline bool cadd(uint64_t a, uint64_t b) {
    return a + b < a;
  }

  static inline bool cadc(uint64_t a, uint64_t b, bool c) {
    return a + b < a || (c && a + b == (uint64_t) -1);
  }

  bool check_hash(const crypto::hash &hash, difficulty_type difficulty) {
    uint64_t low, high, top, cur;
    // First check the highest word, this will most likely fail for a random hash.
    mul(swap64le(((const uint64_t *) &hash)[3]), difficulty, top, high);
    if (high != 0) {
      return false;
    }
    mul(swap64le(((const uint64_t *) &hash)[0]), difficulty, low, cur);
    mul(swap64le(((const uint64_t *) &hash)[1]), difficulty, low, high);
    bool carry = cadd(cur, low);
    cur = high;
    mul(swap64le(((const uint64_t *) &hash)[2]), difficulty, low, high);
    carry = cadc(cur, low, carry);
    carry = cadc(high, top, carry);
    return !carry;
  }

  difficulty_type next_difficulty(std::vector<std::uint64_t> timestamps, std::vector<difficulty_type> cumulative_difficulties, size_t target_seconds, size_t height) {
       assert(DIFFICULTY_WINDOW >= 2);

       if (timestamps.size() > DIFFICULTY_WINDOW) {
         timestamps.resize(DIFFICULTY_WINDOW);
         cumulative_difficulties.resize(DIFFICULTY_WINDOW);
       }

       size_t length = timestamps.size();
       assert(length == cumulative_difficulties.size());
       assert(length <= DIFFICULTY_WINDOW);
       if (length <= 1) {
         return 1;
       }

       sort(timestamps.begin(), timestamps.end());

       size_t cutBegin, cutEnd;
       assert(2 * DIFFICULTY_CUT <= DIFFICULTY_WINDOW - 2);
       if (length <= DIFFICULTY_WINDOW - 2 * DIFFICULTY_CUT) {
         cutBegin = 0;
         cutEnd = length;
       } else {
         cutBegin = (length - (DIFFICULTY_WINDOW - 2 * DIFFICULTY_CUT) + 1) / 2;
         cutEnd = cutBegin + (DIFFICULTY_WINDOW - 2 * DIFFICULTY_CUT);
       }

       assert(/*cut_begin >= 0 &&*/ cutBegin + 2 <= cutEnd && cutEnd <= length);
       uint64_t timeSpan = timestamps[cutEnd - 1] - timestamps[cutBegin];
       if (timeSpan == 0) {
         timeSpan = 1;
       }

       difficulty_type totalWork = cumulative_difficulties[cutEnd - 1] - cumulative_difficulties[cutBegin];
       assert(totalWork > 0);

       uint64_t low, high;
       mul(total_work, target_seconds, low, high);
   // blockchain errors "difficulty overhead" if this function returns zero.
   // TODO: consider throwing an exception instead
      if (high != 0 || low + timeSpan - 1 < low) {
        return 0;
      }


       if (height >= 2) {
         if (high != 0) {
           return 0;
         }
          }
         uint64_t nextDiffZ = low / timeSpan;

         return nextDiffZ;

     }
    difficulty_type next_difficulty_v2(std::vector<std::uint64_t> timestamps, std::vector<difficulty_type> cumulative_difficulties, size_t target_seconds, size_t height) {
      if (height >= (24861-1) && height <= (24861+60) ) {return 10000000;}

      int64_t T = target_seconds;

  	printf("size ts:%lu\n",timestamps.size());

      size_t length = timestamps.size();
      assert(length == cumulative_difficulties.size());

      uint64_t  t = 0,d=0;

  	int64_t solvetime=0;
  	int64_t diff=0;

      for (size_t i = 1; i < length; i++) {
          solvetime = timestamps[i] - timestamps[i-1];
  	diff = cumulative_difficulties[i] - cumulative_difficulties[i-1];
  	//printf("%lu: TS:%lu    solvetime:%d,  diff:%d\n",i,timestamps[i],solvetime,diff);

  	//cap crazy  values
      if (solvetime < 0) { solvetime = 0; }

          t +=  solvetime ;
  		d+=diff;


      }


  	long unsigned int avgtime=t/length;
  	long unsigned int avgdiff=d/length;
  	long unsigned int adj=(T*1000/avgtime);
  	long unsigned int nextDiffZ = (avgdiff*adj)/1000;
  	printf("avgdiff:%lu, avgtime:%lu   adj:%lu   nextdiff:%lu\n",avgdiff,avgtime,adj,nextDiffZ);

      if (nextDiffZ <= 1) {
        nextDiffZ = 1;
      }


      return nextDiffZ;
    }
    difficulty_type next_difficulty_v3(std::vector<std::uint64_t> timestamps, std::vector<difficulty_type> cumulative_difficulties, size_t target_seconds, size_t height) {
      // Copyright (c) 2017-2018 Zawy
  		// MIT license http://www.opensource.org/licenses/mit-license.php.
  		// This is an improved version of Tom Harding's (Deger8) "WT-144"
  		// Karbowanec, Masari, Bitcoin Gold, and Bitcoin Cash have contributed.
  		// See https://github.com/zawy12/difficulty-algorithms/issues/3 for other algos.
  		// Do not use "if solvetime < 0 then solvetime = 1" which allows a catastrophic exploit.
  		// T= target_solvetime;
  		// N=45, 55, 70, 90, 120 for T=600, 240, 120, 90, and 60
      if(height <= 70090 && height >= 70000){return 1000000;}
  		const int64_t T = static_cast<int64_t>(target_seconds);
  		size_t N = DIFFICULTY_WINDOW_V2;

  		if (timestamps.size() > N) {
  			timestamps.resize(N + 1);
  			cumulative_difficulties.resize(N + 1);
  		}
  		size_t n = timestamps.size();
  		assert(n == cumulative_difficulties.size());
  		assert(n <= DIFFICULTY_WINDOW_V2);
      // If new coin, just "give away" first 5 blocks at low difficulty
      if ( n < 6 ) { return  1; }
      // If height "n" is from 6 to N, then reset N to n-1.
      else if (n < N+1) { N=n-1; }

  		// To get an average solvetime to within +/- ~0.1%, use an adjustment factor.
      // adjust=0.99 for 90 < N < 130
  		const double adjust = 0.998;
  		// The divisor k normalizes LWMA.
  		const double k = N * (N + 1) / 2;

  		double LWMA(0), sum_inverse_D(0), harmonic_mean_D(0), nextDifficulty(0);
  		int64_t solveTime(0);
  		uint64_t difficulty(0), next_difficulty(0);

  		// Loop through N most recent blocks.
  		for (size_t i = 1; i <= N; i++) {
  			solveTime = static_cast<int64_t>(timestamps[i]) - static_cast<int64_t>(timestamps[i - 1]);
  			solveTime = std::min<int64_t>((T * 7), std::max<int64_t>(solveTime, (-7 * T)));
  			difficulty = cumulative_difficulties[i] - cumulative_difficulties[i - 1];
  			LWMA += (int64_t)(solveTime * i) / k;
  			sum_inverse_D += 1 / static_cast<double>(difficulty);
  		}

  		// Keep LWMA sane in case something unforeseen occurs.
  		if (static_cast<int64_t>(boost::math::round(LWMA)) < T / 20)
  			LWMA = static_cast<double>(T / 20);

  		harmonic_mean_D = N / sum_inverse_D * adjust;
  		nextDifficulty = harmonic_mean_D * T / LWMA;
  		next_difficulty = static_cast<uint64_t>(nextDifficulty);

      return next_difficulty;
    }
}
