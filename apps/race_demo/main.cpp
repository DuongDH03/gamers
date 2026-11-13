// scraps_race_demo: the stage 8 lesson, made visible.
//
//   count = count + 1    is three machine steps:  LOAD  ADD  STORE
//
// Two cores doing it at once can interleave like this:
//
//   core A: LOAD count (5)                      ADD (6)  STORE 6
//   core B:          LOAD count (5)  ADD (6)                      STORE 6
//                                                         → two increments, count went up by 1
//
// This program runs that on every core through the engine's JobSystem:
//   scraps_race_demo            atomic counter (fetch_add: one indivisible step) → always right
//   scraps_race_demo --unsafe   plain counter → loses updates, exits with failure
//
// The unsafe version is a data race, which C++ calls undefined behaviour:
// it is here to be watched, never copied. CTest runs it expecting failure.

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>

#include "core/jobs.h"

namespace {

constexpr std::uint32_t kIncrementsPerChunk = 2000000;
constexpr int kSkipped = 77;  // CTest's "skipped" code (see apps/CMakeLists.txt)
constexpr int kUnsafeAttempts = 5;  // runs of the plain counter before concluding nothing was lost

}  // namespace

int main(int argc, char** argv) {
  const bool unsafe = argc > 1 && std::strcmp(argv[1], "--unsafe") == 0;

  const unsigned cores = std::thread::hardware_concurrency();
  if (cores < 2) {
    std::printf("only one core: no two threads ever run at the same instant, so there is no race to show\n");
    return kSkipped;
  }

  scraps::JobSystem jobs(cores - 1);
  const std::size_t chunks = cores;  // one chunk per thread, all running at once
  const std::uint64_t expected = static_cast<std::uint64_t>(chunks) * kIncrementsPerChunk;
  std::uint64_t result = 0;

  if (unsafe) {
    // volatile stops the compiler from turning the loop into one big addition,
    // so every increment really is LOAD, ADD, STORE in memory.
    // On a busy machine (other programs, or other tests running in
    // parallel) the threads may take turns for a whole run and lose
    // nothing by luck, so it tries a few times before saying so.
    for (int attempt = 0; attempt < kUnsafeAttempts && (attempt == 0 || result == expected); ++attempt) {
      volatile std::uint32_t counter = 0;
      auto work = [&counter](std::size_t begin, std::size_t end) {
        for (std::size_t chunk = begin; chunk < end; ++chunk) {
          for (std::uint32_t i = 0; i < kIncrementsPerChunk; ++i) counter = counter + 1;
        }
      };
      jobs.parallel_for(chunks, 1, work);
      result = counter;
    }
  } else {
    std::atomic<std::uint32_t> counter{0};
    auto work = [&counter](std::size_t begin, std::size_t end) {
      for (std::size_t chunk = begin; chunk < end; ++chunk) {
        for (std::uint32_t i = 0; i < kIncrementsPerChunk; ++i) counter.fetch_add(1, std::memory_order_relaxed);
      }
    };
    jobs.parallel_for(chunks, 1, work);
    result = counter.load();
  }

  const std::uint64_t lost = expected - result;
  std::printf("%s counter, %u threads x %u increments: expected %llu, got %llu, lost %llu (%.1f%%)\n",
              unsafe ? "plain" : "atomic", cores, kIncrementsPerChunk, static_cast<unsigned long long>(expected),
              static_cast<unsigned long long>(result), static_cast<unsigned long long>(lost),
              100.0 * static_cast<double>(lost) / static_cast<double>(expected));
  return lost == 0 ? 0 : 1;
}
