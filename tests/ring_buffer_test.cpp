
#include "../src/common/memory/cache_line.hpp"
#include <atomic>
#include <cstdio>
#include <iostream>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <x86intrin.h>

#define PROFILING 0
#define PREFETCHING 1
#define TEST_CHECK 0
#define MEM_CTRL 0

#if MEM_CTRL == 0
#include "../src/static_memctrl_lockless_ring_buffer.hpp"
#else
#include "../src/static_lockless_ring_buffer.hpp"
#endif

using ull = unsigned long long int;
using atomic_ull = std::atomic<ull>;

const int num_of_writers = 8;
const unsigned int data_size = 65536 * 64;
const int write_numbers = 80000 * 128;
using buffer_data_type = engine::memory::CacheLinePacked<int>;

#if MEM_CTRL == 1
typedef struct {
  alignas(64) atomic_ull read_head;
  alignas(64) atomic_ull commit_head;
  alignas(64) atomic_ull write_head;

  int data[data_size];
} RingBufferAllocation;
#endif

void ReaderChildProcess(
    LockLessRingBufferRead<ull, buffer_data_type, data_size>);
void WriterChildProcess(
    LockLessRingBufferWrite<ull, buffer_data_type, data_size>, int);

int main() {
  // make the memory
#if MEM_CTRL == 0
  auto r_buffer_opt =
      LockLessRingBufferMemInit<ull, buffer_data_type, data_size>::create();
  if (!r_buffer_opt.has_value()) {
    perror("Could Not allocate memory for the buffer");
    return -1;
  }
  // auto r_buffer = r_buffer_opt.value();
  auto r_buffer = std::move(*r_buffer_opt);
#else
  void *ptr = mmap(NULL, sizeof(RingBufferAllocation), PROT_READ | PROT_WRITE,
                   MAP_SHARED | MAP_ANON, -1, 0);
  if (ptr == MAP_FAILED) {
    perror("Error has successfully occured while mmaping :)");
    return -1;
  }

  // assign the atomic pointers
  RingBufferAllocation *buff_alloc =
      reinterpret_cast<RingBufferAllocation *>(ptr);
  LockLessRingBufferInit r_buffer = LockLessRingBufferInit<ull, int, data_size>(
      &buff_alloc->read_head, &buff_alloc->write_head, &buff_alloc->commit_head,
      buff_alloc->data);
#endif
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
#if MEM_CTRL == 1
  munmap(ptr, sizeof(RingBufferAllocation));
#endif
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

void ReaderChildProcess(
    LockLessRingBufferRead<ull, buffer_data_type, data_size> r_buffer) {

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
    std::optional<buffer_data_type> cons = r_buffer.read();
    if (cons.has_value()) {

      failed_attempts += attempts;
      attempts = 0;

      buffer_data_type val = cons.value();
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
void WriterChildProcess(
    LockLessRingBufferWrite<ull, buffer_data_type, data_size> w_buffer, int n) {
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
    if (w_buffer.write(next)) {
#if PROFILING == 1
      writer_failed_attempts += attempts;
#endif
      attempts = 0;
      i += next.atom.counter;
      next.atom.counter = std::min(write_numbers - i, next.atom.max_elems);
      for (int c = 0; c < next.atom.counter; c++) {
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
