
#include <atomic>
#include <cstdio>
#include <iostream>
#include <sys/mman.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#define MEM_CTRL 0

#if MEM_CTRL == 0
#include "../src/static_memctrl_lockless_ring_buffer.hpp"
#else
#include "../src/static_lockless_ring_buffer.hpp"
#endif
using ull = unsigned long long int;
using atomic_ull = std::atomic<ull>;

const int num_of_writers = 4;
const unsigned int data_size = 1024;

#if MEM_CTRL == 1
typedef struct {
  alignas(64) atomic_ull read_head;
  alignas(64) atomic_ull commit_head;
  alignas(64) atomic_ull write_head;

  int data[data_size];
} RingBufferAllocation;
#endif

void ReaderChildProcess(LockLessRingBufferRead<ull, int, data_size>);
void WriterChildProcess(LockLessRingBufferWrite<ull, int, data_size>, int);

int main() {
  // make the memory
#if MEM_CTRL == 0
  auto r_buffer =
      LockLessRingBufferMemInit<ull, int, data_size>::create().value();
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
void ReaderChildProcess(LockLessRingBufferRead<ull, int, data_size> r_buffer) {
  const int take_numbers = 6096;
  int show_module[take_numbers];
  int i = 0;
  int attempts = 0;
  while (i < take_numbers) {
    std::optional<int> cons = r_buffer.read();
    if (cons.has_value()) {
      attempts = 0;

      int val = cons.value();
      show_module[i] = val;
      i++;
    } else {
      if (i > 0) {
        std::cout << "Reader: Till Now, got " << i << " numbers" << std::endl;
      }
      attempts++;
      thread_yield_waiter(attempts);
    }
  }
}
void WriterChildProcess(LockLessRingBufferWrite<ull, int, data_size> w_buffer,
                        int n) {
  const int write_numbers = 1524;
  int i = 0;
  int attempts = 0;
  while (i < write_numbers) {
    if (w_buffer.write(i + (n * write_numbers))) {
      attempts = 0;
      i++;
    } else {
      attempts++;
      thread_yield_waiter(attempts);
    }
  }
}
