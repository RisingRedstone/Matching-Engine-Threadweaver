#include "../src/allocators/distributed_dynamic_allocator.hpp"
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <sys/wait.h>
using engine::allocators::DistributedDynamicParentAllocator;
using engine::allocators::DistributedDynamicParentAllocatorManager;

int main() {
  DistributedDynamicParentAllocatorManager manager;
  const size_t array_size = 1000;
  struct DataWriting {
    alignas(64) std::atomic<int> write_head;
    int nums[array_size];

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

    auto out = alloc.allocate(file_name, sizeof(DataWriting));
    if (out.index() != 0) {
      perror("Reader: Returned an error instead");
    } else {
      auto mem_ptr = std::get<0>(out);
      DataWriting *array = new (mem_ptr) DataWriting();
      array->write_head.store(2, std::memory_order_relaxed);
      while (array->write_head.load(std::memory_order_relaxed) != 1) {
        CPU_PAUSE();
      }
      // writing finished, maybe print?
      for (int i = 0; i < array_size; i++) {
        std::cout << array->nums[i] << " ";
        if (i % 20 == 19) {
          std::cout << std::endl;
        }
      }
      alloc.deallocate(file_name);
    }
  } else {
    int pid_2 = fork();
    if (pid_2 == -1) {
      throw std::runtime_error("Could not fork");
    } else if (pid_2 == 0) {
      // child 2 (writes data)

      // RAII patterning some useless file
      struct Celc {
        DistributedDynamicParentAllocator &alloc;
        char *file_name;
        bool succ;
        Celc(DistributedDynamicParentAllocator &alloc, char *file_name)
            : alloc(alloc), file_name(file_name) {
          auto out = alloc.allocate(file_name, 1000);
          succ = out.index() == 0;
        }
        ~Celc() {
          if (succ) {
            alloc.deallocate(file_name);
          }
        }
      };

      DistributedDynamicParentAllocator alloc(std::move(manager));
      Celc celc(alloc, file_name_2);
      auto out = alloc.allocate(file_name, sizeof(DataWriting));
      if (out.index() != 0) {
        // error occured
        perror("Writer: Returned an error instead");
      } else {
        auto mem_ptr = std::get<0>(out);
        DataWriting *array =
            reinterpret_cast<DataWriting *>(mem_ptr); // no constructor here
        while (array->write_head.load(std::memory_order_acquire) != 2) {
          CPU_PAUSE();
        }
        for (int i = 0; i < array_size; i++) {
          array->nums[i] = i;
        }
        array->write_head.store(1, std::memory_order_release);
        alloc.deallocate(file_name);
      }
    } else {
      // parent
      DistributedDynamicParentAllocator alloc(std::move(manager));
      // do nothing basically. Wait for others.
      int status;
      waitpid(pid_1, &status, 0);
      waitpid(pid_2, &status, 0);
      std::cout << "Parent Exiting" << std::endl;
    }
  }

  return 0;
}
