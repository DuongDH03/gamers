#include "core/jobs.h"

namespace scraps {

JobSystem::JobSystem(unsigned workers) {
  m_threads.reserve(workers);
  for (unsigned i = 0; i < workers; ++i) m_threads.emplace_back(&JobSystem::worker_main, this);
}

JobSystem::~JobSystem() {
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_quit = true;
  }
  m_wake.notify_all();
  for (std::thread& thread : m_threads) thread.join();
}

unsigned JobSystem::default_worker_count() {
  const unsigned cores = std::thread::hardware_concurrency();  // 0 if unknown
  return cores > 1 ? cores - 1 : 0;
}

void JobSystem::parallel_for(std::size_t count, std::size_t grain, RangeFn fn, void* context) {
  if (count == 0 || fn == nullptr) return;
  if (grain == 0) grain = 1;
  const std::size_t chunks = (count + grain - 1) / grain;

  // Small jobs, or no workers: just run it here. Waking threads costs
  // microseconds, more than a handful of entities take.
  if (m_threads.empty() || chunks == 1) {
    fn(context, 0, count);
    return;
  }

  {
    // Everything a worker needs is written under the lock; a worker reads it
    // only after taking the same lock, so it always sees a complete job.
    std::lock_guard<std::mutex> lock(m_mutex);
    m_fn = fn;
    m_context = context;
    m_count = count;
    m_grain = grain;
    m_chunk_count = chunks;
    m_next_chunk.store(0, std::memory_order_relaxed);
    m_chunks_finished.store(0, std::memory_order_relaxed);
    ++m_job_generation;
  }
  m_wake.notify_all();

  run_chunks();  // the caller works too

  std::unique_lock<std::mutex> lock(m_mutex);
  m_done.wait(lock, [this] {
    return m_chunks_finished.load(std::memory_order_acquire) == m_chunk_count && m_active_workers == 0;
  });
  m_fn = nullptr;  // job over; workers that wake late find nothing to do
}

void JobSystem::run_chunks() {
  while (true) {
    // fetch_add is one indivisible read-modify-write: two threads can never
    // get the same chunk. A plain `next++` here would be the classic race.
    const std::size_t chunk = m_next_chunk.fetch_add(1, std::memory_order_relaxed);
    if (chunk >= m_chunk_count) return;
    const std::size_t begin = chunk * m_grain;
    const std::size_t end = begin + m_grain < m_count ? begin + m_grain : m_count;
    m_fn(m_context, begin, end);
    if (m_chunks_finished.fetch_add(1, std::memory_order_acq_rel) + 1 == m_chunk_count) {
      std::lock_guard<std::mutex> lock(m_mutex);  // pairs with the caller's wait
      m_done.notify_one();
    }
  }
}

void JobSystem::worker_main() {
  std::uint64_t seen_generation = 0;
  while (true) {
    {
      std::unique_lock<std::mutex> lock(m_mutex);
      m_wake.wait(lock, [&] { return m_quit || m_job_generation != seen_generation; });
      if (m_quit) return;
      seen_generation = m_job_generation;
      if (m_fn == nullptr) continue;  // that job already finished
      ++m_active_workers;             // registered: the caller now waits for us
    }
    run_chunks();
    {
      // Taking the lock also publishes this worker's writes to the caller.
      std::lock_guard<std::mutex> lock(m_mutex);
      --m_active_workers;
      if (m_active_workers == 0) m_done.notify_one();
    }
  }
}

}  // namespace scraps
