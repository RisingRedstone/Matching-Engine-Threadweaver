#include <atomic>
#include <cstdio>
#include <iostream>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <x86intrin.h>

#include "../src/allocators/one_time_static_allocator.hpp"
#include "../src/buffer/buffer.hpp"
#include "../src/buffer/layouts/lockless_ring_buffer_layout.hpp"
#include "../src/buffer/ring/three_pointer_approach.hpp"
#include "../src/buffer/scmp.hpp"
#include "../src/common/memory/cache_line.hpp"

#define PROFILING 1
#define PREFETCHING 1
#define TEST_CHECK 1
#define MEM_CTRL 0

static const size_t buffer_size = 65536 * 4;
static const int num_of_writers = 8;
static const int write_numbers = 80000 * 128;

using ull = unsigned long long;

template <typename Layout>
using Allocator =
    engine::allocators::OneTimeStaticSharedMemoryAllocator<Layout>;
using data_type = engine::memory::CacheLinePacked<int, uint8_t>;
using Layout = engine::buffer::layout::StaticLockLessRingBufferLayout<
    data_type, buffer_size, Allocator>;
template <typename Layout>
using ProducerFactory = engine::buffer::scmp::Factory::ProducerFactory<
    engine::buffer::ring::three_pointer_approach::Producer, Layout>;
template <typename Layout>
using ConsumerFactory = engine::buffer::scmp::Factory::ConsumerFactory<
    engine::buffer::ring::three_pointer_approach::Consumer, Layout>;
using Buffer = engine::buffer::Buffer<ProducerFactory, ConsumerFactory, Layout>;

using Consumer = ConsumerFactory<Layout>::Consumer;
using Producer = ProducerFactory<Layout>::Producer;
using buffer_data_type = Consumer::data_type;

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
  ull *show_module = new ull[take_numbers](0);
  int i = 0;
  int attempts = 0;
  ull failed_attempts = 0;
  std::cout << "Starting Read" << std::endl;

#if PROFILING == 1
  unsigned int aux;
  ull start = __rdtscp(&aux);
  ull total_null_time = 0;
#elif PROFILING == 2
  prctl(PR_TASK_PERF_EVENTS_ENABLE, 0, 0, 0, 0);
#endif

  while (i < take_numbers) {
#if PREFETCHING == 1
    __builtin_prefetch(&show_module[i + 16], 1, 3);
#endif
    std::optional<buffer_data_type> cons = consumer.read();
    if (cons.has_value()) {

      failed_attempts += attempts;
      attempts = 0;

      buffer_data_type val = std::move(cons.value());
      for (int j = 0; j < val.atom.counter; j++) {
        // Use vector instructions later here
        show_module[i] = val.atom.data[j];
        i++;
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

  const auto index = [&](const int &a) { return a + (n * write_numbers); };
  buffer_data_type next;
  next.atom.counter = std::min((size_t)write_numbers - i, next.atom.max_elems);
  for (int c = 0; c < next.atom.counter; c++) {
    next.atom.data[c] = index(i + c);
  }

  while (i < write_numbers) {
    if (producer.write(next)) {
#if PROFILING == 1
      writer_failed_attempts += attempts;
#endif
      attempts = 0;
      i += next.atom.counter;
      next.atom.counter = std::min(write_numbers - i, next.atom.max_elems);
      for (int c = 0; c < next.atom.max_elems; c++) {
        next.atom.data[c] = index(i + c);
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
  Buffer buffer = Buffer();
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

program_end:
  return 0;
}
