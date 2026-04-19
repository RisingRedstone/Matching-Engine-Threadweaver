
// Nah just document everything till now lil bro.
// TEST GOALS
// Static allocation achieves
// 8 writers
// 10240000 ints in 1.4 seconds
// 7300000 ints per second
// 2097152 int static buffer
//
// MORE INFO:
// Do batch writes on the entire cache line so like 64 byte write on my
// procesor. so if I am writing integers to the buffer, I must write 8 integers
// per write
//      and if it is an unsigned long long int, I must write 4 ulls per write.
// benefits, no cache collisions between writes. The reader gets cache locality.
// problem, I need to wait for those specific number of bytes to arrive every
// time. Solution, I can create sort of like a struct: template<class Uarr,
// size_t U_size, size_t cache_line_size> requires PowerOfTwo<sizeof(Uarr)>
// struct{
//
//  alignas(x := cache_line_size % sizeof(Uarr) if not 0 else sizeof(Uarr)) int
//  counter; Uarr data[(cache_line_size - x) / sizeof(Uarr)];
// } Cache_Private;
// This creates a structure that is always cache line secure. I can also upload
// dynamic data. if I get 1 data and I dont think I can wait for more, I
// straight up just set the counter to 1 and write the rest of the data.
// problem, complicated to implement (and to decide how much data to upload),
// can possibly make the logic more complex, but has basically all the wins.
//
//
// Either that, or we can do even better, make the values inside the cache
// distant. so the sequence goes like this:
//      0 -> 8 -> 16 -> 24 . . . size-8 -> 1 . . .
//      1 -> 9 -> 17 -> 25 . . .
// benefits of this, no cache problems for the writes. they all get to be on
// different cache lines problem is that there is no cache locality for the
// reader. it has to fetch a different cache line every time.
//
//
// lets see if my dynamic buffer can beat that.
// The thing is, increasing the buffer size is not doing much good. :)
// I will have to learn to use better tools and profilers
// check for cache hits and misses and stuff

// clang-format off
//
// The idea is to allocate a crazy amount of virtual memory (like 64 GB) on a little bit of physical memory.
//
// We can then just keep incrementing the counter without having to worry about modulus or anding to loop back for the ring buffer.
//
// When to update memory?
// The LocklessRingBufferManager has a function (e.g, check()) that checks if the buffer gets filled by a certain amount (e.g, 80%)
// Then it increases the memory by some specified amount (determined by another algorithm, could just double memory idk). 
//
// I can use PID style here btw for memory increase and decrease and to know how much to increase the memory by.
//
// But how to update memory?
// Very tricky thing.
// My array pointer will need to be atomic now. as I will have to change across threads.
// First copy how much ever you can copy between the read_head and commit_heads.
// Point to note here. The current atomic pointers of read_head, commit_head and write_head should make sense in the new array.
//
// for example:
// array_size = 35
// read_head = 100      = 30 on array
// commit_head = 120    = 15 on array
// write_head = 125     = 20 on array
// 
// so in the new array:
// array_size = 57
// read_head = 100      = 43 on array
// commit_head = 120    = 06 on array
// write_head = 125     = 11 on array
// 
// So when copying data from the old array to the new one, we will copy it from data from old[29:35] -> new[42:47] && old[0:10] -> new[47:57] && old[10:15] -> new[0:5]
// 
// Now, shift the write_head to 2 times the old_size to immobilize the write
// Now wait for the commit_head + 2 * (old_size) == write_head
// Then copy the rest of the data
// Then switch the atomic buffer pointers.
// Delete the old array buffer virtual memory and the memory.
//
//
// THINGS TO NOTE:
// use:
// #define likely(x)       __builtin_expect(!!(x), 1)
// #define unlikely(x)     __builtin_expect(!!(x), 0)
// to note unlikely branches
//
// Use HUGE_TLB mapping to reduce the TLB Thrashing and have the entire mapping
// fit in the TLB
//
// Maybe try memory locking
//
// Branch prediction when the atomics become too big
//
//
// Bruv so I need to somehow update the commit_head and the write_head
// simultaneously.
//
//
// Okay heres the final thing:
// I am going to have to do shm_open in the main DynamicRingBufferManager and allocate memory there, then I am going to have to allocate a big chunk k * alloc_size in the virtual address space.
// Now, how big should this k be also what algo should I use to increase the alloc_size in case of memory overreach?
//
// To learn:
// branch prediction
// prefetching
//
//
// NEW IDEA:
// The lock free buffer can store if a value is written on each buffer head.
// The reader will exchange the array value with a (not written value) on the read_head pointer as long as the read_head pointer is smaller than write_head
//  If the exchanged value is (not written, it gets skipped)
// The writers add 1, then check if the current head is bigger than the read_head, if it is, sub 1, else swap the new value (indicating written) in the array.
// If the swapped value in the array contains data that needs to be read, we need to tell the reader about updated value(described below) and then do this again but now with the swapped value. 
// Maybe the commit_head can be fetch_min
// There are many problems with this. The reader can get ahead of the writers. This can happen if the reader reads an array block, thinks there is nothing to be written there and just moves on.
// A possible solution to that is for the write_head to do a read_head = min(read_head, write_head-1)
//  if a reader is on write_head, it will be moved back, and then if the read does a read_head+1, we will end up at write_head so worst case scenario, we will have to do 1 more false read. big deal better thn having to do CAS every operation.
// Now, does this method ensure that every write gets a unique writable array?
// aight I checked, apparently xchg instruction exchanges memory and there is lock min that can do atomic min
//
// clang-format on

#include <atomic>
template <class Tptr, class Uarr> class DynamicRingBufferManager {
  using atomic_T = std::atomic<Tptr>;
  using Uptr = Uarr *;

private:
  atomic_T *read_head, *write_head, *commit_head;
  Uptr array;
  size_t array_size;

public:
  DynamicRingBufferManager(size_t array_size)
      : array_size(array_size), array(nullptr), read_head(nullptr),
        write_head(nullptr), commit_head(nullptr) {}
};
