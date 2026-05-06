#pragma once

/* Imports */
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <emmintrin.h>
#include <fcntl.h>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sched.h>
#include <stdexcept>
#include <string>
#include <sys/mman.h>
#include <unistd.h>
#include <variant>

#include "../common/generic.hpp"
#include "common/memory/guards.hpp"

namespace engine::allocators {

constexpr int buffer_name_length = 100;
enum class Lock : uint8_t { UNLOCK_CELL = 0x00, LOCK_CELL = 0x01 };

template <size_t name_size, bool Locked = false>
struct alignas(64) MemoryEntry {

  using self = MemoryEntry<name_size, Locked>;
  using self_inv = MemoryEntry<name_size, !Locked>;

  template <bool L> using MemoryEntryTemplate = MemoryEntry<name_size, L>;
  using LockGuard =
      memory::guards::TemplateLockedLockGuard<MemoryEntryTemplate>;

  std::atomic<Lock> lock_v;
  std::atomic<int> ref_count;
  std::size_t max_size;
  char name
      [name_size +
       1]; // use this instead of string as strings are not shared across processes

private:
  MemoryEntry() {} // disable constructor for Unlocked class

public:
  MemoryEntry()
    requires(!Locked)
  {
    lock_v.store(Lock::UNLOCK_CELL, std::memory_order_release);
  }
  std::optional<std::reference_wrapper<self_inv>> try_lock()
    requires(!Locked)
  {
    Lock to_try = Lock::UNLOCK_CELL;
    if (lock_v.compare_exchange_strong(to_try, Lock::LOCK_CELL,
                                       std::memory_order_acquire,
                                       std::memory_order_relaxed)) {
      return std::ref(*reinterpret_cast<self_inv *>(this));
    } else {
      return std::nullopt;
    }
  }
  self_inv &lock()
    requires(!Locked)
  {
    while (true) {
      if (auto ret = try_lock()) {
        return ret.value().get();
      }
      CPU_PAUSE();
    }
  }
  void unlock()
    requires Locked
  {
    lock_v.store(Lock::UNLOCK_CELL, std::memory_order_release);
  }
  bool is_free()
    requires Locked
  {
    return ref_count == 0;
  }
  void inc_ref_count()
    requires Locked
  {
    ref_count.fetch_add(1, std::memory_order_acquire);
  }
  void dec_ref_count()
    requires Locked
  {
    if (ref_count.fetch_sub(1, std::memory_order_release) <= 1) {
      shm_unlink(name);
    }
  }

  bool name_check(char other[100])
    requires Locked
  {
    return (strcmp(other, name) == 0) && (max_size > 0);
  }
  size_t get_max_size()
    requires Locked
  {
    return max_size;
  }
  void set_max_size(size_t s)
    requires Locked
  {
    max_size = s;
  }
};

template <size_t entry_number, size_t name_size>
struct alignas(64) MemoryEntryTable {
  using self = MemoryEntryTable<entry_number, name_size>;
  using element = MemoryEntry<name_size>;
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
  std::optional<typename element::LockGuard>
  get_cell(char name[max_name_length]) {
    for (size_t i = 0; i < max_entry_number; i++) {
      typename element::LockGuard lock(mem_entry[i]);
      if (lock->name_check(name)) {
        return std::move(lock);
      }
    }
    return std::nullopt;
  }
  std::optional<typename element::LockGuard>
  add_cell(char name[max_name_length], size_t size) {
    for (size_t i = 0; i < max_entry_number; i++) {
      typename element::LockGuard lock(mem_entry[i]);
      if (lock->is_free()) {
        lock->max_size = size;
        strncpy(lock->name, name, name_size + 1);
        lock->ref_count.store(0, std::memory_order_relaxed);
        lock->lock_v.store(Lock::UNLOCK_CELL, std::memory_order_release);
        return std::move(lock);
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
    int fd;
    {
      MemTableLockGuard lock(*manager.table);
      auto cell = manager.table->get_cell(name);
      if (!cell.has_value()) {
        fd = shm_open(name, O_CREAT | O_RDWR | O_EXCL, 0600);
        if (fd == -1) {
          return AllocatorError::SharedMemoryError;
        }
        if (ftruncate(fd, size) == -1) {
          shm_unlink(name);
          return AllocatorError::FTruncateFailed;
        }

        cell = manager.table->add_cell(name, size);
        if (!cell.has_value()) {
          return AllocatorError::CouldNotAllocate;
        }

        cell.value()->inc_ref_count();
      } else {
        fd = shm_open(name, O_RDWR, 0600);
        if (fd == -1) {
          return AllocatorError::SharedMemoryError;
        }
        auto m_size = cell.value()->get_max_size();
        if (m_size < size) {
          if (ftruncate(fd, size) == -1) {
            return AllocatorError::FTruncateFailed;
          }
          // also set the value in the cell
          cell.value()->set_max_size(size);
        } else {
          size = m_size;
        }

        cell.value()->inc_ref_count();
      }
    } //cell and manager lock guard dropped

    void *mem_ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    names[name] = {mem_ptr, size};
    if (mem_ptr == nullptr) {
      //error state. Undo everything before
      deallocate(name);
      return AllocatorError::MMapFailed;
    }
    return mem_ptr;
  }

  std::variant<size_t, DeallocatorError>
  get_size(char name[max_name_length]) noexcept {
    if (names.contains(name) == 0) {
      return DeallocatorError::NameDoesNotExist;
    }
    return names[name].second;
  }

  std::variant<void *, AllocatorError, DeallocatorError>
  check(char name[max_name_length]) noexcept {
    if (names.contains(name) == 0) {
      return DeallocatorError::NameDoesNotExist;
    }
    size_t max_size = 0;
    {
      MemTableLockGuard lock(*manager.table);
      auto cell = manager.table->get_cell(name);
      if (!cell.has_value()) {
        return DeallocatorError::NameDoesNotExist;
      }
      max_size = cell.value()->get_max_size();
    } // cell and manager lock guard dropped

    void *mem_ptr = names[name].first;
    size_t curr_max_size = names[name].second;

    // check if there is a size mismatch
    if (curr_max_size < max_size) {
      // expand local mem address
      mem_ptr = mremap(mem_ptr, curr_max_size, max_size, MREMAP_MAYMOVE);
      if (mem_ptr == MAP_FAILED) {
        return AllocatorError::MMapFailed;
      }
      curr_max_size = max_size;
      names[name] = {mem_ptr, curr_max_size};
    } else if (curr_max_size > max_size) {

      curr_max_size = max_size;
      names[name] = {mem_ptr, curr_max_size};
    }

    return mem_ptr;
  }

  std::optional<DeallocatorError>
  deallocate(char name[max_name_length]) noexcept {
    if (names.contains(name) == 0) {
      return DeallocatorError::NameDoesNotExist;
    }
    {
      MemTableLockGuard lock(*manager.table);
      auto cell = manager.table->get_cell(name);
      if (!cell.has_value()) {
        return DeallocatorError::NameDoesNotExist;
      }

      cell.value()->dec_ref_count();
    } // cell and manager lock guard dropped
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
