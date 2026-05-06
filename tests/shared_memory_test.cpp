#include "../src/allocators/distributed_dynamic_allocator.hpp"
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <sys/wait.h>
using engine::allocators::DistributedDynamicParentAllocator;
using engine::allocators::DistributedDynamicParentAllocatorManager;

// RAII patterning some useless file
template <size_t size> struct Celc {
  DistributedDynamicParentAllocator &alloc;
  char *file_name;
  void *mem_ptr = nullptr;
  bool succ;
  Celc(DistributedDynamicParentAllocator &alloc, char *file_name)
      : alloc(alloc), file_name(file_name) {
    auto out = alloc.allocate(file_name, size);
    succ = out.index() == 0;
    if (succ) {
      mem_ptr = std::get<0>(out);
    }
  }
  ~Celc() {
    if (succ) {
      alloc.deallocate(file_name);
    }
  }
};

int main() {
  DistributedDynamicParentAllocatorManager manager;
  const size_t array_size = 1000;
  struct alignas(64) ReadDataWriting {
    alignas(64) std::atomic<int> write_head;
  };
  struct alignas(64) DataWriting {
    alignas(64) std::atomic<int> write_head;
    int size;
    alignas(64) int nums[array_size];

    DataWriting() : write_head(0) {}
    DataWriting(const DataWriting &) = delete;
    DataWriting(DataWriting &&) = delete;
    DataWriting &operator=(const DataWriting &) = delete;
    DataWriting &operator=(DataWriting &&) = delete;
  };
  char file_name[100] = "/tmp_mem12_shm";
  char file_name_2[100] = "/tmp_mem12_shm_";

  int pid_1 = fork();
  if (pid_1 == -1) {
    throw std::runtime_error("Could not fork");
  } else if (pid_1 == 0) {
    // child 1
    DistributedDynamicParentAllocator alloc(manager);

    Celc<sizeof(ReadDataWriting)> celc(alloc, file_name);
    if (!celc.succ) {
      throw std::runtime_error("Could not fork");
    }

    auto mem_ptr = celc.mem_ptr;
    ReadDataWriting *array = new (mem_ptr) ReadDataWriting();
    array->write_head.store(2, std::memory_order_relaxed);
    while (array->write_head.load(std::memory_order_relaxed) != 1) {
      CPU_PAUSE();
    }

    /* REMAPPING WITH CHECK */
    auto out = celc.alloc.check(file_name);
    if (out.index() != 0) {
      throw std::runtime_error("Check failed");
    }
    mem_ptr = std::get<0>(out);
    DataWriting *data_array = reinterpret_cast<DataWriting *>(mem_ptr);

    // writing finished, maybe print?
    for (int i = 0; i < data_array->size; i++) {
      std::cout << data_array->nums[i] << " ";
      if (i % 20 == 19) {
        std::cout << std::endl;
      }
    }
    return 0;
  }

  int pid_2 = fork();
  if (pid_2 == -1) {
    throw std::runtime_error("Could not fork");
  } else if (pid_2 == 0) {
    // child 2 (writes data)

    DistributedDynamicParentAllocator alloc(std::move(manager));
    Celc<sizeof(DataWriting)> celc(alloc, file_name);
    if (!celc.succ) {
      // error occured
      perror("Writer: Returned an error instead");
    }

    auto mem_ptr = celc.mem_ptr;
    DataWriting *array =
        reinterpret_cast<DataWriting *>(mem_ptr); // no constructor here
    while (array->write_head.load(std::memory_order_acquire) != 2) {
      CPU_PAUSE();
    }
    for (int i = 0; i < array_size; i++) {
      array->nums[i] = i;
    }
    array->size = array_size;
    array->write_head.store(1, std::memory_order_release);
    return 0;
  }

  // parent
  DistributedDynamicParentAllocator alloc(std::move(manager));
  // do nothing basically. Wait for others.
  int status;
  waitpid(pid_1, &status, 0);
  if (status != 0)
    return -1;
  waitpid(pid_2, &status, 0);
  if (status != 0)
    return -1;
  std::cout << "Parent Exiting" << std::endl;

  return 0;
}
