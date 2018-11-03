//
// Created by vout on 18/3/2018.
//

#ifndef KMLIB_KMTHREAD_H
#define KMLIB_KMTHREAD_H

#include <cstddef>
#include <vector>
#include <thread>
#include <atomic>

namespace kmlib {

namespace internal {
/*!
 * @brief a wrapper for std::Atomic. std::Atomic do not support copy and move
 * constructor, so this wrapper is used to make suitable to std::vector
 * @tparam T the underlying type of the atomic struct
 */
template<class T>
struct KmforAtomic {
  std::atomic<T> v;
  KmforAtomic(T a = T()) : v(a) {}
  KmforAtomic(const KmforAtomic &rhs) : v(rhs.v.load()) {}
  KmforAtomic &operator=(const KmforAtomic &rhs) {
    v.store(rhs.v.load());
    return *this;
  }
};
}

template<class SizeType, class Function, int Mode = 1>
void kmfor(size_t n_threads, const Function &func, SizeType begin, SizeType end) {
  if (n_threads > 1) {
    std::vector<std::thread> thread_pool(n_threads);
    std::vector<internal::KmforAtomic<SizeType>> next_task(n_threads);
    std::vector<std::pair<SizeType, SizeType>> task_range(n_threads);

    auto tasks_per_thread = Mode == 0 ? (end - begin) / n_threads : 0;
    auto task_begin = begin;
    for (size_t tid = 0; tid < n_threads; ++tid) {
      auto task_end = task_begin + tasks_per_thread;
      if (tid == n_threads - 1) {
        task_end = end;
      }
      next_task[tid] = task_begin;
      task_range[tid] = std::make_pair(task_begin, task_end);
      task_begin = task_end;
    }

    for (size_t tid = 0; tid < n_threads; ++tid) {
      auto worker = [tid, &n_threads, &end, &func, &next_task, &task_range]() {
        // pre-assigned tasks
        while (Mode == 0) {
          auto task = next_task[tid].v.fetch_add(1);
          if (task >= task_range[tid].second) {
            break;
          }
          func(task, tid);
        }
        // stolen tasks
        while (true) {
          size_t thread_to_steal = tid;
          if (Mode == 0) {
            SizeType min_processed = std::numeric_limits<SizeType>::max();

            for (size_t t = 0; t < n_threads; ++t) {
              auto processed = next_task[t].v.load() - task_range[t].first;
              if (t != tid && min_processed > processed) {
                min_processed = processed;
                thread_to_steal = t;
              }
            }
          } else {
            thread_to_steal = n_threads - 1;
          }
          auto stolen_task = next_task[thread_to_steal].v.fetch_add(1);
          if (stolen_task >= task_range[thread_to_steal].second) {
            break;
          }
          func(stolen_task, tid);
        }
      };
      thread_pool[tid] = std::thread(worker);
    }

    for (auto &t : thread_pool) {
      t.join();
    }
  } else {
    for (SizeType task = begin; task < end; ++task) {
      func(task, 0);
    }
  }
};

} // namespace kmlib