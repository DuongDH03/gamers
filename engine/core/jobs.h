#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace scraps {

// A small job system (stage 8). Worker threads start once and sleep until
// there is work. parallel_for splits an index range into chunks; workers and
// the calling thread take chunks until none are left:
//
//   caller:  parallel_for(1000, 64, fn)
//            ├─ chunk  0: [  0,  64)  ─► whichever thread grabs it first
//            ├─ chunk  1: [ 64, 128)  ─► ...
//            └─ ...      each thread does "next = counter++" (one atomic step)
//   caller:  also runs chunks, then waits until all are finished
//
// Rules for the function you pass:
//   - it may run on any thread, many chunks at the same time;
//   - it must only write data that belongs to its own indices, and only
//     read data that nobody writes during the call.
// parallel_for is not reentrant: call it from one thread at a time.
class JobSystem {
 public:
  // `workers` extra threads; 0 runs everything on the calling thread.
  explicit JobSystem(unsigned workers);
  ~JobSystem();
  JobSystem(const JobSystem&) = delete;
  JobSystem& operator=(const JobSystem&) = delete;

  // A sensible worker count for this machine: one less than the core count.
  static unsigned default_worker_count();

  unsigned worker_count() const { return static_cast<unsigned>(m_threads.size()); }

  using RangeFn = void (*)(void* context, std::size_t begin, std::size_t end);

  // Calls fn(context, begin, end) for consecutive ranges covering [0, count),
  // each at most `grain` long, spread over all threads. Returns when all are done.
  void parallel_for(std::size_t count, std::size_t grain, RangeFn fn, void* context);

  // The same, for a lambda or any callable `f(begin, end)`. No allocation:
  // the callable stays on the caller's stack.
  template <typename F>
  void parallel_for(std::size_t count, std::size_t grain, F& f) {
    parallel_for(count, grain, &JobSystem::trampoline<F>, &f);
  }

 private:
  template <typename F>
  static void trampoline(void* context, std::size_t begin, std::size_t end) {
    (*static_cast<F*>(context))(begin, end);
  }

  void worker_main();
  // Runs chunks of the current job until none are left.
  void run_chunks();

  std::vector<std::thread> m_threads;
  std::mutex m_mutex;
  std::condition_variable m_wake;  // workers wait here for a new job
  std::condition_variable m_done;  // the caller waits here for the last chunk
  bool m_quit = false;
  std::uint64_t m_job_generation = 0;  // bumped for every parallel_for
  // Workers currently inside run_chunks() for the current job. The caller
  // does not return (and so cannot start the next job) until this is 0, so a
  // slow worker can never mix up two jobs. Guarded by m_mutex.
  unsigned m_active_workers = 0;

  // The current job. Written by the caller before waking workers.
  RangeFn m_fn = nullptr;
  void* m_context = nullptr;
  std::size_t m_count = 0;
  std::size_t m_grain = 1;
  std::size_t m_chunk_count = 0;
  std::atomic<std::size_t> m_next_chunk{0};
  std::atomic<std::size_t> m_chunks_finished{0};
};

}  // namespace scraps
