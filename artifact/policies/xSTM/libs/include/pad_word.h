/// pad_word.h defines padded 64-bit atomic ints.  It is useful for ensuring
/// that synchronization metadata lives on separate cache lines.

#pragma once

#include <atomic>

#include "platform.h"

/// pad_word_t is an atomic uintptr_t that is padded out to a cache line
struct pad_word_t {
  /// The value
  std::atomic<uintptr_t> val;

  /// Some padding
  char pad[CACHELINE_BYTES - sizeof(uintptr_t)];

  /// Construct by zeroing the value
  pad_word_t() { val = 0; }
};

/// pad_dword_t is an atomic uintptr_t that is padded out to two cache lines
struct pad_dword_t {
  /// The value
  std::atomic<uintptr_t> val;

  /// Some padding
  char pad[2 * CACHELINE_BYTES - sizeof(uintptr_t)];

  /// Construct by zeroing the value
  pad_dword_t() { val = 0; }
};