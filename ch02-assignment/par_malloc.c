
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <math.h>
#include <inttypes.h>

#include "par_malloc.h"
#include "xmalloc.h"

const size_t PAGE_SIZE = 1024000;
static int arenas_init = 0;
static arena arenas[4];
static pthread_mutex_t init_mutex = PTHREAD_MUTEX_INITIALIZER;
__thread int favorite_arena = 0;

void* xmalloc(size_t bytes) {
  return opt_malloc(bytes);
}

void xfree(void* ptr) { 
  opt_free(ptr);
}

void* xrealloc(void* prev, size_t bytes) {
  return opt_realloc(prev, bytes);
}

void* opt_malloc(size_t bytes) {
  if (!arenas_init) {
    init_arenas();
  }

  if (bytes == 0)
    return 0;

  if (bytes <= 2048) {
    size_t mask = 0x800;
    int target_bucket = 7;

    while (!(mask & bytes) && target_bucket > 0) {
      target_bucket--;
      mask >>= 1;
    }

    target_bucket = bytes > mask ? target_bucket + 1 : target_bucket;

    lock_arena();

    bucket* bucket_found = arenas[favorite_arena].buckets[target_bucket];
    void* ret = first_free_block(bucket_found);

    unlock_arena();
    return ret;
  } else {
    unlock_arena();
    size_t* largeMem = mmap(0, bytes + sizeof(size_t), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(largeMem > 0);
    *largeMem = bytes;
    return (void*)(largeMem + 1);
  }
}

void* first_free_block(bucket* b) {
  int rv = pthread_mutex_lock(&b->mutex);
  assert(rv == 0);
  size_t block_size = b->size;
  size_t blockIdx = 0;
  uint8_t* ret = 0;

  while (block_size != 16) {
    blockIdx++;
    block_size >>= 1;
  }

  while (!ret) {
    uint64_t* mapStart = (uint64_t*)(b + 1);

    for (int ii = 0; ii < pageMaps[blockIdx]; ii++) {
      if (*(mapStart + ii) != UINT64_MAX) {
        int bitIdx = 0;
        uint64_t mask = 1;

        while (*(mapStart + ii) & mask) {
          bitIdx++;
          mask <<= 1;
        }

        ret = (uint8_t*)(mapStart + pageMaps[blockIdx]) + bitIdx * b->size + ii * 64 * b->size;
        *(mapStart + ii) |= mask;
        break;
      }
    }

    if (!ret) {
      if (b->next_page) {
        bucket* nextPage = b->next_page;
        rv = pthread_mutex_lock(&nextPage->mutex);
        assert(rv == 0);
        rv = pthread_mutex_unlock(&b->mutex);
        assert(rv == 0);
        b = nextPage;
      } else {
        bucket* newBucket = mmap(0, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        assert(newBucket > 0);
        init_page(b->size, newBucket);
        b->next_page = newBucket;
        *((uint64_t*)(newBucket + 1)) = 1;
        ret = (uint8_t*)((uint64_t*)(newBucket + 1) + pageMaps[blockIdx]);
      }
    }
  }

  rv = pthread_mutex_unlock(&(b->mutex));
  assert(rv == 0);

  return (void*)ret;
}

void opt_free(void* ptr) {
  // bucket* closest = closest_bucket(ptr);

  // if (closest != 0) {
  //   void* mem_start = (void*)(closest + 1);
  //   size_t index = (ptr - mem_start) / closest->size;
  //   int offset = index / 64;
  //   uint64_t mask = ~(uint64_t)(pow(2, (index % 64)));
  //   // printf("%p %p %ld %d\n", ptr, mem_start, index, offset);
  //   int rv = pthread_mutex_lock(&(closest->mutex));
  //   assert(rv == 0);

  //   closest->map[offset] = closest->map[offset] & mask; 

  //   rv = pthread_mutex_unlock(&(closest->mutex));
  //   assert(rv == 0);
  // } else {
  //   free(ptr);
  // }
}

void* opt_realloc(void* prev, size_t bytes) {
    void* new_block = opt_malloc(bytes);
    size_t prev_size = get_block_size(prev);

    if (bytes <= prev_size || prev_size == 0) {
      memcpy(new_block, prev, bytes);
    } else {
      memcpy(new_block, prev, prev_size);
    }

    opt_free(prev);
    return new_block;
}

size_t get_block_size(void* ptr) {
  return closest_bucket(ptr)->size;
}

bucket* closest_bucket(void* ptr) {
  for (int ii = 0; ii < 4; ii++) {
    arena cur_arena = arenas[ii];
    for (int jj = 0; jj < 8; jj++) {
      bucket* cur_bucket = cur_arena.buckets[jj];
      while (cur_bucket != 0) {
        int rv = pthread_mutex_lock(&cur_bucket->mutex);
        assert(rv == 0);
        if ((uint8_t*)ptr > (uint8_t*)cur_bucket && (uint8_t*)ptr < (uint8_t*)cur_bucket + PAGE_SIZE) {
          rv = pthread_mutex_unlock(&cur_bucket->mutex);
          assert(rv == 0);
          return cur_bucket;
        }

        rv = pthread_mutex_unlock(&cur_bucket->mutex);
        assert(rv == 0);
        cur_bucket = cur_bucket->next_page;
      }
    }
  }

  return 0;
}

void init_arenas() {
  int rv = pthread_mutex_lock(&init_mutex);
  assert(rv == 0);

  if (!arenas_init) {
    for (int ii = 0; ii < 4; ii++) {
      rv = pthread_mutex_init(&(arenas[ii].mutex), NULL);
      assert(rv == 0);
      size_t bucket_size = 16;
      for (int jj = 0; jj < 8; jj++) {
        arenas[ii].buckets[jj] = mmap(0, PAGE_SIZE, PROT_READ | PROT_WRITE,
                                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        assert(arenas[ii].buckets[jj] > 0);
        init_page(bucket_size, arenas[ii].buckets[jj]);
        bucket_size <<= 1;
      }
    }

    arenas_init = 1;
  }

  rv = pthread_mutex_unlock(&init_mutex);
  assert(rv == 0);
}

// block size should be a power of 2 above 16
void init_page(size_t block_size, void* start) {
  bucket* header = (bucket*)start;
  header->size = block_size;
  header->next_page = 0;
  int rv = pthread_mutex_init(&header->mutex, NULL);
  assert(rv == 0);

  size_t bucketIdx = 0;

  while (block_size != 16) {
    block_size >>= 1;
    bucketIdx++;
  }

  uint64_t* mapStart = (uint64_t*)(header + 1);
  memset(mapStart, 0, (pageMaps[bucketIdx] - 1) * sizeof(uint64_t));
  *(mapStart + pageMaps[bucketIdx] - 1) = lastMap[bucketIdx];
}

void lock_arena() {
  int rv = pthread_mutex_trylock(&(arenas[favorite_arena].mutex));
  if (rv != 0) {
    favorite_arena = (favorite_arena + 1) % 4;
    rv = pthread_mutex_lock(&(arenas[favorite_arena].mutex));
    assert(rv == 0);
  }
}

void unlock_arena() {
  int rv = pthread_mutex_unlock(&(arenas[favorite_arena].mutex));
  assert(rv == 0);
}