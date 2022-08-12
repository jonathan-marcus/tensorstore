// Copyright 2022 The TensorStore Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "tensorstore/internal/multi_barrier.h"

#include <cassert>

namespace tensorstore {
namespace internal {
namespace {

// Return whether int *arg is zero.
bool IsZero(void* arg) { return 0 == *reinterpret_cast<int*>(arg); }

}  // namespace

MultiBarrier::MultiBarrier(int num_threads)
    : num_threads_(num_threads << 1), blocking_{num_threads, 0} {
  assert(num_threads > 0);
}

bool MultiBarrier::Block() {
  // Arriving threads select one of the blocking variables based on the low-bit
  // of num_threads_.  Once that variable reaches 0, the low-bit is toggled
  // and the other variable becomes active.
  absl::MutexLock l(&lock_);
  int& num_to_block = blocking_[num_threads_ & 1];
  num_to_block--;
  assert(num_to_block >= 0);

  bool owner = (num_to_block == 0);
  if (owner) {
    num_threads_ ^= 1;
    blocking_[num_threads_ & 1] = num_threads_ >> 1;
  }
  lock_.Await(absl::Condition(IsZero, &num_to_block));
  return owner;
}

}  // namespace internal
}  // namespace tensorstore
