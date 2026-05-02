#pragma once

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <emmintrin.h>
#include <fcntl.h>
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <sched.h>
#include <stdexcept>
#include <string>
#include <sys/mman.h>
#include <unistd.h>
#include <variant>

#include "../common/generic.hpp"

namespace engine::allocators {

constexpr int buffer_name_length = 100;
enum class Lock : uint8_t { UNLOCK_CELL = 0x00, LOCK_CELL = 0x01 };

template <size_t name_size> struct alignas(64) MemoryEntry {

  using self = MemoryEntry<name_size>;
  using LockGuard = std::lock_guard<self>;

  std::atomic<Lock> lock_v;
  std::atomic<int> ref_count;
  std::size_t max_size;
  char name
      [name_size +
       1]; // use this instead of string as strings are not shared across processes

  MemoryEntry() { lock_v.store(Lock::UNLOCK_CELL, std::memory_order_release); }
  bool try_lock() {
    Lock to_try = Lock::UNLOCK_CELL;
    return lock_v.compare_exchange_strong(to_try, Lock::LOCK_CELL,
                                          std::memory_order_acquire,
                                          std::memory_order_relaxed);
  };
  void lock() {
    while (!try_lock())
      CPU_PAUSE();
  }
  void unlock() { lock_v.store(Lock::UNLOCK_CELL, std::memory_order_release); }
  bool is_free() {
    LockGuard lock(*this);
    return ref_count == 0;
  }
  void inc_ref_count() {
    LockGuard lock(*this);
    ref_count.fetch_add(1, std::memory_order_acquire);
  }
  void dec_ref_count() {
    LockGuard lock(*this);
    if (ref_count.fetch_sub(1, std::memory_order_release) <= 1) {
      shm_unlink(name);
    }
  }

  bool name_check(char other[100]) {
    LockGuard lock(*this);
    return (strcmp(other, name) == 0) && (max_size > 0);
  }
  size_t get_max_size() {
    LockGuard lock(*this);
    return max_size;
  }
};
template <size_t entry_number, size_t name_size>
struct alignas(64) MemoryEntryTable {
  using self = MemoryEntryTable<entry_number, name_size>;
  using LockGuard = std::lock_guard<self>;

  alignas(64) std::atomic<int> number_of_entries;
  std::atomic<int> ref_count;
  std::atomic<Lock> lock_entries;
  MemoryEntry<name_size> mem_entry[entry_number];

  static constexpr size_t max_name_length = name_size;
  static constexpr size_t max_entry_number = entry_number;

  MemoryEntryTable() {
    lock_entries.store(Lock::UNLOCK_CELL, std::memory_order_release);
  }
  std::optional<size_t> find_name(char name[max_name_length]) {
    for (size_t i = 0; i < max_entry_number; i++) {
      if (mem_entry[i].name_check(name)) {
        return i;
      }
    }
    return std::nullopt;
  }
  std::optional<size_t> get_size(char name[max_name_length]) {
    for (size_t i = 0; i < max_entry_number; i++) {
      if (mem_entry[i].name_check(name)) {
        return mem_entry[i].get_max_size();
      }
    }
    return std::nullopt;
  }
  std::optional<std::reference_wrapper<MemoryEntry<name_size>>>
  get_cell(char name[max_name_length]) {
    for (size_t i = 0; i < max_entry_number; i++) {
      if (mem_entry[i].name_check(name)) {
        return mem_entry[i];
      }
    }
    return std::nullopt;
  }
  std::optional<std::reference_wrapper<MemoryEntry<name_size>>>
  add_cell(char name[max_name_length], size_t size) {
    for (size_t i = 0; i < max_entry_number; i++) {
      if (mem_entry[i].is_free()) {
        mem_entry[i].max_size = size;
        strncpy(mem_entry[i].name, name, name_size + 1);
        mem_entry[i].ref_count.store(0, std::memory_order_relaxed);
        mem_entry[i].lock_v.store(Lock::UNLOCK_CELL, std::memory_order_release);
        return mem_entry[i];
      }
    }
    return std::nullopt;
  }
  void lock() {
    while (!try_lock())
      CPU_PAUSE();
  }
  bool try_lock() {
    Lock to_try = Lock::UNLOCK_CELL;
    return lock_entries.compare_exchange_strong(to_try, Lock::LOCK_CELL,
                                                std::memory_order_acquire,
                                                std::memory_order_relaxed);
  };
  void unlock() {
    lock_entries.store(Lock::UNLOCK_CELL, std::memory_order_release);
  }

  static void destroy_entries(MemoryEntryTable *mem_table, std::size_t size) {
    while (!mem_table->try_lock()) {
      CPU_PAUSE();
    }
    for (std::size_t i = 0; i < entry_number; i++) {
      if (mem_table->mem_entry[i].ref_count != 0) {
        // throw std::runtime_error("Expected an empty state.");
        std::cout << "Ref Count: " << mem_table->mem_entry[i].ref_count
                  << std::endl;
        perror("Someone is using this but I'm going to delete anyway");
      }
      if (strcmp(mem_table->mem_entry[i].name, "") != 0) {
        shm_unlink(mem_table->mem_entry[i].name);
      }
    }
    if (munmap(mem_table, size) != 0) {
      throw std::runtime_error("Could not munmap properly.");
    }
  }
};
class DistributedDynamicParentAllocatorManager {
  // See I dont want to make this very complicated so for now, I will keep a hard limit of 100 entries
  //  in the memory entry table.
public:
  using MemTable = MemoryEntryTable<100, 100>;

private:
  pid_t owner;

public:
  MemTable *table;
  explicit DistributedDynamicParentAllocatorManager() : table(nullptr) {
    owner = getpid();
    void *table_ptr = mmap(NULL, sizeof(MemTable), PROT_WRITE | PROT_READ,
                           MAP_SHARED | MAP_ANON, -1, 0);
    if (table_ptr == nullptr) {
      throw std::runtime_error(
          "Could not allocate memory for table of memory entries.");
    }
    table = new (table_ptr) MemTable();
    table->ref_count.store(1, std::memory_order_release);
  }
  DistributedDynamicParentAllocatorManager(
      DistributedDynamicParentAllocatorManager &other)
      : owner(getpid()), table(other.table) {
    if (table != nullptr)
      table->ref_count.fetch_add(1, std::memory_order_acquire);
  }
  DistributedDynamicParentAllocatorManager &
  operator=(DistributedDynamicParentAllocatorManager &other) {
    owner = getpid();
    table = other.table;
    if (table != nullptr)
      table->ref_count.fetch_add(1, std::memory_order_acquire);
    return *this;
  }
  DistributedDynamicParentAllocatorManager(
      DistributedDynamicParentAllocatorManager &&r_value)
      : owner(getpid()), table(r_value.table) {
    r_value.table = nullptr;
  }
  DistributedDynamicParentAllocatorManager &
  operator=(DistributedDynamicParentAllocatorManager &&r_value) {
    owner = getpid();
    table = r_value.table;
    r_value.table = nullptr;
    return *this;
  }
  ~DistributedDynamicParentAllocatorManager() {
    if (getpid() != owner)
      return;
    if (table != nullptr) {
      int num_remaining =
          table->ref_count.fetch_sub(1, std::memory_order_acquire) - 1;
      if (num_remaining <= 0) {
        // cleanup
        MemTable::destroy_entries(table, sizeof(MemTable));
      }
    }
  }
};
enum class AllocatorError : int {
  CouldNotAllocate,
  SharedMemoryError,
  MMapFailed,
  FTruncateFailed,
};
enum class DeallocatorError : int {
  NameDoesNotExist,
  MunMapFailed,
};
class DistributedDynamicParentAllocator {
  using Manager = DistributedDynamicParentAllocatorManager;
  using MemTableLockGuard = std::lock_guard<Manager::MemTable>;
  static constexpr std::size_t max_name_length =
      Manager::MemTable::max_name_length;
  static constexpr std::size_t max_entry_number =
      Manager::MemTable::max_entry_number;

private:
  pid_t owner;
  Manager manager;
  std::unordered_map<std::string, std::pair<void *, size_t>> names;

public:
  explicit DistributedDynamicParentAllocator(Manager &manager)
      : manager(manager), names() {
    owner = getpid();
  }
  explicit DistributedDynamicParentAllocator(Manager &&manager)
      : manager(manager), names() {
    owner = getpid();
  }
  [[nodiscard]] std::variant<void *, AllocatorError>
  allocate(char name[max_name_length], size_t size) noexcept {
    if (names.count(name) > 0) {
      return names[name].first;
    }
    MemTableLockGuard lock(*manager.table);
    auto res = manager.table->get_cell(name);
    int fd;
    if (!res.has_value()) {
      fd = shm_open(name, O_CREAT | O_RDWR | O_EXCL, 0600);
      if (fd == -1) {
        return AllocatorError::SharedMemoryError;
      }
      if (ftruncate(fd, size) == -1) {
        shm_unlink(name);
        return AllocatorError::FTruncateFailed;
      }

      res = manager.table->add_cell(name, size);
      if (!res.has_value()) {
        return AllocatorError::CouldNotAllocate;
      }

      res->get().inc_ref_count();
    } else {
      fd = shm_open(name, O_RDWR, 0600);
      if (fd == -1) {
        return AllocatorError::SharedMemoryError;
      }
      auto m_size = res.value().get().get_max_size();
      if (m_size < size) {
        if (ftruncate(fd, size) == -1) {
          return AllocatorError::FTruncateFailed;
        }
      } else {
        size = m_size;
      }

      res->get().inc_ref_count();
    }

    void *mem_ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    names[name] = {mem_ptr, size};
    if (mem_ptr == nullptr) {
      //error state. Undo everything before
      deallocate(name);
      return AllocatorError::MMapFailed;
    }
    return mem_ptr;
  }
  std::optional<DeallocatorError>
  deallocate(char name[max_name_length]) noexcept {
    if (names.contains(name) == 0) {
      return DeallocatorError::NameDoesNotExist;
    }
    MemTableLockGuard lock(*manager.table);
    auto cell = manager.table->get_cell(name);
    if (!cell.has_value()) {
      return DeallocatorError::NameDoesNotExist;
    }

    cell->get().dec_ref_count();
    if (munmap(names[name].first, names[name].second) != 0) {
      return DeallocatorError::MunMapFailed;
    }
    return std::nullopt;
  }
  ~DistributedDynamicParentAllocator() {
    if (getpid() != owner)
      return;
  }
};
} // namespace engine::allocators
