

#include "../src/common/memory/cache_line.hpp"
#include <atomic>
#include <csignal>
#include <cstdio>
#include <iostream>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <x86intrin.h>

#define PROFILING 1
#define PREFETCHING 1
#define TEST_CHECK 1

// TRACEPOINT_DEFINE must appear in exactly one .cpp that uses tracepoints.
// TRACEPOINT_CREATE_PROBES lives in tp_provider.cpp.
#define TRACEPOINT_DEFINE
#include "../src/static_lowcas_lockless_ring_buffer.hpp"

using ull = unsigned long long int;
using atomic_ull = std::atomic<ull>;

const int num_of_writers = 8;
const unsigned int data_size = 65536 * 128;
const int write_numbers = 65536 * 128;
using buffer_data_type = int;
using cache_data_type =
    LockLessRingBufferLowCAS<ull, buffer_data_type, data_size>::Uarr_t;

void ReaderChildProcess(
    LockLessRingBufferLowCASRead<ull, buffer_data_type, data_size>);
void WriterChildProcess(
    LockLessRingBufferLowCASWrite<ull, buffer_data_type, data_size>, int);

// std::optional<LockLessRingBufferLowCAS<ull, buffer_data_type, data_size>>
//     r_buffer_opt;

int main() {
  // make the memory
  auto r_buffer_opt =
      LockLessRingBufferLowCAS<ull, buffer_data_type, data_size>::create();
  if (!r_buffer_opt.has_value()) {
    perror("Could Not allocate memory for the buffer");
    return -1;
  }
  // auto r_buffer = r_buffer_opt.value();
  auto r_buffer = std::move(*r_buffer_opt);

  using PRINT_BUFFER =
      LockLessRingBufferLowCAS<ull, buffer_data_type, data_size>;
  PRINT_BUFFER::print_instance = &r_buffer;
  std::signal(SIGUSR1, r_buffer.display_memory);

  int reader_pid;

  // get the read lockless buffer
  auto r_buffer_read_opt = r_buffer.create_consumer();
  if (!r_buffer_read_opt.has_value()) {
    perror("Could not create a read buffer extension :(");
    return -1; // yeah I cant jump to main_thread_end here
  }
  auto r_buffer_read = std::move(r_buffer_read_opt.value());

  // start reading thread
  reader_pid = fork();
  if (reader_pid == -1) {
    perror("Could not create children. Gonna die alone :(");
    return -1;
  } else if (reader_pid == 0) {
    // Child process :)
    ReaderChildProcess(std::move(r_buffer_read));
    goto children_program_end;
  }

  int writer_pids[num_of_writers];
  for (int i = 0; i < num_of_writers; i++) {
    auto r_buffer_writer = r_buffer.create_producer();

    writer_pids[i] = fork();
    if (writer_pids[i] == -1) {
      perror("Could not create children. Gonna die alone :(");
      return -1;
    } else if (writer_pids[i] == 0) {
      // Start the write thread number i
      WriterChildProcess(std::move(r_buffer_writer), i);
      goto children_program_end;
    }
    // The parent continues the loop
  }

  int status;
  waitpid(reader_pid, &status, 0);
  if (status != 0) {
    perror("I guess the reader failed\n");
    return -1;
  }
  for (int i = 0; i < num_of_writers; i++)
    waitpid(writer_pids[i], &status, 0);

main_thread_end:
children_program_end:
  return 0;
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

const int take_numbers = write_numbers * num_of_writers;
static int *show_module = nullptr;

int test_show_module(int sig = 0) {
  if (show_module == nullptr)
    return -1;
  std::cout << "Checking numbers" << std::endl;
  int *check_numbers = new int[take_numbers](0);
  for (int i = 0; i < take_numbers; i++) {
    check_numbers[show_module[i]] += 1;
  }

  int not_founds = 0;
  for (int i = 0; i < take_numbers; i++) {
    if (check_numbers[i] != 1) {
      // print the error
      int thread = i / write_numbers;
      std::cout << "This number " << i << "\t got " << check_numbers[i]
                << " values given to thread " << thread << std::endl;
      not_founds += 1;
    }
  }
  std::cout << "Total Not Found: " << not_founds << std::endl;
  delete[] check_numbers;
  return not_founds;
}

void ReaderChildProcess(
    LockLessRingBufferLowCASRead<ull, buffer_data_type, data_size> r_buffer) {

  // try locking to the 0th core
  cpu_set_t set;
  CPU_ZERO(&set); // Initialize to zero
  CPU_SET(0, &set);
  if (sched_setaffinity(0, sizeof(set), &set) == -1) {
    std::cout << "Could not lock" << std::endl;
  }

  std::signal(SIGUSR1, [](int) { test_show_module(); });

  show_module = new int[take_numbers](0);
  int i = 0;
  int attempts = 0;
  ull failed_attempts = 0;
  std::cout << "Starting Read" << std::endl;

#if PROFILING == 1
  unsigned int aux;
  ull start = __rdtscp(&aux);
  ull total_null_time = 0;
  // int mem_test[num_of_writers] = {0};
  // for (int i = 0; i < num_of_writers; i++) {
  //   mem_test[i] = i * write_numbers-1;
  // }
#elif PROFILING == 2
  prctl(PR_TASK_PERF_EVENTS_ENABLE, 0, 0, 0, 0);
#endif

  while (i < take_numbers) {
#if PREFETCHING == 1
    __builtin_prefetch(&show_module[i + 16], 1, 3);
#endif
    std::optional<cache_data_type> cons = r_buffer.read();
    if (cons.has_value()) {

      failed_attempts += attempts;
      attempts = 0;

      cache_data_type val = cons.value();
      for (int j = 0; j < val.atom.counter.get_length(); j++) {
        // Use vector instructions later here
        show_module[i] = val.atom.data[j];

#if PROFILING == 1
        // int thread = show_module[i] % write_numbers;
        // if (show_module[i] - mem_test[thread] != 1) {
        //   std::cout << "Reader: Skip Error by thread " << thread << ": "
        //             << mem_test[thread] << " -> " << show_module[i]
        //             << std::endl;
        // }
        // mem_test[thread] = show_module[i];
#endif

        i++;
      }
      // std::cout << "Reader: " << (int)val.atom.counter.get_length() << " Got
      // "
      //           << i << " values out of " << take_numbers << std::endl;
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
  int not_founds = test_show_module();
  if (not_founds > 0) {
    std::cout << not_founds << " Not Found" << std::endl;
    exit(1);
  } else {
    std::cout << "All Found" << std::endl;
  }
#endif
  delete[] show_module;
}
void WriterChildProcess(
    LockLessRingBufferLowCASWrite<ull, buffer_data_type, data_size> w_buffer,
    int n) {
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
  cache_data_type next;
  next.atom.counter.set_lock_bit_unlock();
  next.atom.counter.set_length(
      std::min(write_numbers - i, next.atom.max_elems));
  for (int c = 0; c < next.atom.counter.get_length(); c++) {
    next.atom.data[c] = index(i + c);
  }

  while (i < write_numbers) {
    if (w_buffer.write(next)) {
#if PROFILING == 1
      writer_failed_attempts += attempts;
#endif
      // std::cout << "Writer " << n << " Wrote " << (int)i << " values out of "
      //           << write_numbers << std::endl;
      attempts = 0;
      i += next.atom.counter.get_length();
      next.atom.counter.set_lock_bit_unlock();
      next.atom.counter.set_length(
          std::min(write_numbers - i, next.atom.max_elems));
      for (int c = 0; c < next.atom.counter.get_length(); c++) {
        next.atom.data[c] = index(i + c);
      }
    } else {
#if PROFILING == 1
      // std::cout << "Writer " << n << " total attempts " << attempts
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
