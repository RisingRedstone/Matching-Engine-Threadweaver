#include <atomic>
#include <cstdio>
#include <iostream>
#include <linux/prctl.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <x86intrin.h>

#include "../src/allocators/one_time_static_allocator.hpp"
#include "../src/buffer/buffer.hpp"
#include "../src/buffer/layouts/lockless_ring_buffer_layout.hpp"
#include "../src/buffer/ring/cell_lockable_approach.hpp"
#include "../src/buffer/ring/cell_lockable_approach_opt_1.hpp"
#include "../src/buffer/ring/three_pointer_approach.hpp"
#include "../src/buffer/scmp.hpp"
#include "../src/common/memory/cache_line.hpp"

#define PROFILING 0
#define PREFETCHING 1
#define TEST_CHECK 0

#ifndef NUM_OF_WRITERS
#define NUM_OF_WRITERS 8
#endif
#ifndef BUFFER_SIZE
#define BUFFER_SIZE 65536 * 8
#endif
#ifndef WRITE_NUMBERS
#define WRITE_NUMBERS 80000 * 2048
#endif
#ifndef APPROACH
#define APPROACH 2
#endif

static const int num_of_writers = NUM_OF_WRITERS;
static const size_t buffer_size = BUFFER_SIZE;
static const int write_numbers = WRITE_NUMBERS;

using ull = unsigned long long;

template <typename Layout>
using Allocator =
    engine::allocators::OneTimeStaticSharedMemoryAllocator<Layout>;

#if APPROACH == 1
using data_type = engine::memory::CacheLinePacked<int, uint8_t>;
using Layout = engine::buffer::layout::StaticLockLessRingBufferLayout<
    data_type, buffer_size, Allocator>;
template <typename Layout>
using ProducerFactory = engine::buffer::scmp::Factory::ProducerFactory<
    engine::buffer::ring::three_pointer_approach::Producer, Layout>;
template <typename Layout>
using ConsumerFactory = engine::buffer::scmp::Factory::ConsumerFactory<
    engine::buffer::ring::three_pointer_approach::Consumer, Layout>;
#elif APPROACH == 2
// using data_type = engine::memory::CacheLineUint8LengthHeaderPacked<int>;
using data_type = engine::memory::CacheAlignedHeaderLine<int>;
using Layout =
    engine::buffer::layout::StaticLockLessRingBufferCellLockableLayout<
        data_type, buffer_size, Allocator>;

template <typename Layout>
using ProducerFactory = engine::buffer::scmp::Factory::ProducerFactory<
    engine::buffer::ring::cell_lockable_approach::ProducerConsumer, Layout>;
template <typename Layout>
using ConsumerFactory = engine::buffer::scmp::Factory::ConsumerFactory<
    engine::buffer::ring::cell_lockable_approach::ProducerConsumer, Layout>;
#endif

// Does not work yet
// template <typename Layout>
// using ProducerFactory = engine::buffer::scmp::Factory::ProducerFactory<
//     engine::buffer::ring::cell_lockable_approack_opt_1::ProducerConsumer,
//     Layout>;
// template <typename Layout>
// using ConsumerFactory = engine::buffer::scmp::Factory::ConsumerFactory<
//     engine::buffer::ring::cell_lockable_approack_opt_1::ProducerConsumer,
//     Layout>;

using Buffer = engine::buffer::Buffer<ProducerFactory, ConsumerFactory, Layout>;
using Consumer = ConsumerFactory<Layout>::Consumer;
using Producer = ProducerFactory<Layout>::Producer;
using buffer_data_type = Consumer::data_type;

void perf_enable() {
  // asm volatile("" ::: "memory");
  // if (prctl(PR_TASK_PERF_EVENTS_ENABLE, 0, 0, 0, 0) == -1) {
  //   perror("prctl enable failed");
  // }else{
  //     perror("prctl enable succeeded");
  // }
  // asm volatile("" ::: "memory");
}
void perf_disable() {
  // asm volatile("" ::: "memory");
  // if (prctl(PR_TASK_PERF_EVENTS_DISABLE, 0, 0, 0, 0) == -1) {
  //   perror("prctl disable failed");
  // }
  // asm volatile("" ::: "memory");
}

void thread_yield_waiter(int attempt) {
  if (attempt < 10) {
    asm volatile("pause" ::: "memory");
  } else if (attempt < 100) {
    std::this_thread::yield();
  } else {
    std::this_thread::sleep_for(std::chrono::microseconds(10));
  }
}

void ConsumerProcess(Consumer consumer) {

  // try locking to the 0th core
  cpu_set_t set;
  CPU_ZERO(&set); // Initialize to zero
  CPU_SET(0, &set);
  if (sched_setaffinity(0, sizeof(set), &set) == -1) {
    std::cout << "Could not lock" << std::endl;
  }

  const int take_numbers = write_numbers * num_of_writers;
#if TEST_CHECK == 1
  alignas(64) ull *show_module = new ull[take_numbers](0);
#else
  alignas(64) ull *show_module = new ull[buffer_data_type::max_elems](0);
#endif
  int i = 0;
  int attempts = 0;
  ull failed_attempts = 0;
  std::cout << "Starting Read" << std::endl;

  perf_enable();

#if PROFILING == 1
  unsigned int aux;
  ull start = __rdtscp(&aux);
  ull total_null_time = 0;
#elif PROFILING == 2
  prctl(PR_TASK_PERF_EVENTS_ENABLE, 0, 0, 0, 0);
#endif

  while (i < take_numbers) {
#if PREFETCHING == 1 && TEST_CHECK == 1
    __builtin_prefetch(&show_module[i + 16], 1, 3);
#endif
    auto cons = consumer.read_lock();
    if (cons.has_value()) {
      buffer_data_type val = *cons.value();
      cons.reset();

      failed_attempts += attempts;
      attempts = 0;

      for (int j = 0; j < buffer_data_type::max_elems; j++) {
        // Use vector instructions later here

#if TEST_CHECK == 1
        show_module[i] = val[j];
#else
        show_module[j] = val[j];
#endif
        i++;
      }
    } else {
#if PROFILING == 1
      // std::cout << "Reader: Got" << (int)i << " values out of " <<
      // take_numbers
      //           << std::endl;
      ull inner_start = __rdtscp(&aux);
      attempts++;
      thread_yield_waiter(attempts);
      total_null_time += __rdtscp(&aux) - inner_start;
#else
      attempts++;
      thread_yield_waiter(attempts);
#endif
    }
  }

#if PROFILING == 1
  std::cout << "Reader: Failed attempts: " << failed_attempts
            << "\tTotal i: " << i << std::endl;
  ull total_time = __rdtscp(&aux) - start - total_null_time;
  std::cout << "Reader: Total cycles: " << total_time
            << "\tLoops: " << take_numbers << "\tcycles per loop: "
            << (double)total_time / (double)take_numbers << std::endl;
#elif PROFILING == 2
  prctl(PR_TASK_PERF_EVENTS_DISABLE, 0, 0, 0, 0);
#endif

  perf_disable();

#if TEST_CHECK == 1
  // Check if all numbers are there
  std::cout << "Checking numbers" << std::endl;
  int *check_numbers = new int[take_numbers](0);
  for (int i = 0; i < take_numbers; i++) {
    check_numbers[show_module[i]] += 1;
  }

  int not_founds = 0;
  for (int i = 0; i < take_numbers; i++) {
    // not_founds += !check_numbers[i];
    if (check_numbers[i] != 1) {
      // print the error
      std::cout << "This number " << i << "\t got " << check_numbers[i]
                << " values" << std::endl;
      not_founds += 1;
    }
  }
  if (not_founds > 0) {
    std::cout << not_founds << " Not Found" << std::endl;
    exit(1);
  } else {
    std::cout << "All Found" << std::endl;
  }
  delete[] check_numbers;
#endif
  delete[] show_module;
}

void ProducerProcess(Producer producer, int n) {
#if PROFILING == 2
  prctl(PR_TASK_PERF_EVENTS_DISABLE, 0, 0, 0, 0);
#endif
  size_t i = 0;
  int attempts = 0;
  ull writer_failed_attempts = 0;
#if PROFILING == 1
  unsigned int aux;
  ull start = __rdtscp(&aux);
  ull total_null_time = 0;
#endif

  perf_enable();

  const auto index = [&](const int &a) { return a + (n * write_numbers); };
  buffer_data_type next;
  // next.header() = 1;
  next.get_length() = std::min((size_t)write_numbers - i, next.max_elems);
  for (int c = 0; c < next.get_length(); c++) {
    next[c] = index(i + c);
  }

  while (i < write_numbers) {
    if (producer.write(next)) {
#if PROFILING == 1
      writer_failed_attempts += attempts;
#endif
      attempts = 0;
      i += next.get_length();

#if PROFILING == 1
      // std::cout << "Writer " << n << ": sent " << (int)next.get_length()
      //           << " values" << std::endl;
#endif
      // next.header() = 1;
      next.get_length() = std::min(write_numbers - i, next.max_elems);
      for (int c = 0; c < next.max_elems; c++) {
        next[c] = index(i + c);
      }
    } else {
#if PROFILING == 1
      ull inner_start = __rdtscp(&aux);
      attempts++;
      thread_yield_waiter(attempts);
      total_null_time += __rdtscp(&aux) - inner_start;
#else
      attempts++;
      thread_yield_waiter(attempts);
#endif
    }
  }

  perf_disable();

#if PROFILING == 1
  std::cout << "Writer " << n << ": "
            << "Failed attempts: " << writer_failed_attempts
            << "\tTotal i: " << i << std::endl;
  ull total_time = __rdtscp(&aux) - start - total_null_time;
  std::cout << "Writer " << n << ": "
            << "Total cycles: " << total_time << "\tLoops: " << write_numbers
            << "\tcycles per loop: "
            << (double)total_time / (double)write_numbers << std::endl;
#endif
}

int main() {
  // prctl(PR_TASK_PERF_EVENTS_DISABLE, 0, 0, 0, 0);
  Buffer buffer = Buffer();

  perf_enable();

  auto consumer_opt = buffer.create_consumer();
  if (!consumer_opt.has_value()) {
    perror("Could not create consumer");
    exit(1);
  }
  Consumer consumer = std::move(consumer_opt.value());
  int readerpid = fork();
  if (readerpid == -1) {
    perror("Could not create Children.");
    exit(1);
  } else if (readerpid == 0) {
    // in child
    // run the reader process
    ConsumerProcess(std::move(consumer));
    goto program_end;
  }

  int writerpid[num_of_writers];
  for (int i = 0; i < num_of_writers; i++) {
    // Producer producer = std::move(producer_opt.value());
    writerpid[i] = fork();
    if (writerpid[i] == -1) {
      perror("Could not create Children.");
      exit(1);
    } else if (writerpid[i] == 0) {
      // run the writer process
      auto producer_opt = buffer.create_producer();
      if (!producer_opt.has_value()) {
        perror("Could not create producer");
        exit(1);
      }
      ProducerProcess(std::move(producer_opt.value()), i);
      goto program_end;
    }
  }

  int status;
  waitpid(readerpid, &status, 0);
  if (status != 0) {
    perror("I guess the writer failed\n");
    return -1;
  }
  for (int i = 0; i < num_of_writers; i++) {
    waitpid(writerpid[i], &status, 0);
  }

  perf_disable();

program_end:
  return 0;
}
