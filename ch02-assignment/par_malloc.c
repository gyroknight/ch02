
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

const size_t PAGE_SIZE = 1024 * 1000;
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

  lock_arena();

  if (bytes <= 8192) {
    int target_bucket = log(bytes) / log(2);

    if (pow(2, target_bucket) < bytes) {
      target_bucket++;
    }

    target_bucket = target_bucket - 4 < 0 ? 0 : target_bucket - 4;

    bucket* last_bucket = 0;
    bucket* current_bucket = arenas[favorite_arena].buckets[target_bucket];
    assert(bytes <= current_bucket->size);

    void* ret = 0;

    while (current_bucket != 0) {
      ret = first_free_block(current_bucket);

      if (ret == 0) {
        last_bucket = current_bucket;
        current_bucket = current_bucket->next_page;
      } else {
        unlock_arena();
        return ret;
      }
    }

    current_bucket = mmap(0, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(current_bucket > 0);
    last_bucket->next_page = current_bucket;
    create_page(last_bucket->size, current_bucket, 0);
    *((uint64_t*)(current_bucket + 1)) = 1;

    unlock_arena();
    return (void*)(current_bucket + 1) + sizeof(chunk);
  } else {
    unlock_arena();
    return malloc(bytes);
  }
}

void* first_free_block(bucket* b) {
  chunk* current = (chunk*)(b + 1);
  size_t block_size = b->size;

  while (current != 0) {
    for (int ii = 0; ii < 4; ii++) {
      if (current->map[ii] < UINT64_MAX) {
        return (void*)(current + 1) + block_size * free_index(&current->map[ii]) + ii * 64 * block_size;
      }
    }

    current = current->next;
  }

  return 0;
}

// Gives you the index of the first free spot in the map and sets it as allocated
int free_index(uint64_t* map) {
  assert(*map < UINT64_MAX);
  for (int ii = 0; ii < 64; ii++) {
    uint64_t mask = pow(2, ii);
    if ((*map & mask) == 0) {
      *map = *map | mask;
      return ii;
    }
  }

  return -1;
}

void opt_free(void* ptr) {
  // Interesting implications here, since the pointer to free doesn't have a block size
  // How do you find the closest page?

  // lock_arena();
  //
  // unlock_arena();
  // int rv;
  // int found = 0;
  // for (int ii = 0; ii < 9; ii++){
  //     rv = pthread_mutex_lock(&(arenas[ii].mutex));
  //     assert(rv == 0);
  //     if (clear_arena(arenas[ii])) {
  //         found = 1;
  //     }
  //     rv = pthread_mutex_unlock(&(arenas[ii].mutex));
  //     assert(rv == 0);
  // }

  // if (!found) {
  //     free(ptr);
  // }
}

// runs through all buckets in an arena, returning 1 if it cleared the ptr to free
int clear_arena(arena* arena, void* ptr) {
    // TODO
    for (int ii = 0; ii < 9; ii++){
        bucket* current_bucket = arena->buckets[ii];
        if ((void*) current_bucket < ptr && ptr < (void*) current_bucket + PAGE_SIZE) {
            // within the boundaries of this page, able to be freed
            // TODO: change the bitmap
        }
    }
}

void* opt_realloc(void* prev, size_t bytes) {
    void* new_block = opt_malloc(bytes);
    size_t prev_size = get_block_size(prev);

    if (bytes <= prev_size) {
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
    for (int jj = 0; jj < 10; jj++) {
      bucket* cur_bucket = cur_arena.buckets[jj];
      while (cur_bucket != 0) {
        if (ptr > (void*)cur_bucket && ptr < (void*)cur_bucket + PAGE_SIZE) {
          return cur_bucket;
        }

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
      pthread_mutex_init(&(arenas[ii].mutex), NULL);
      size_t bucket_size = 16;
      for (int jj = 0; jj < 10; jj++) {
        arenas[ii].buckets[jj] = mmap(0, 2 * PAGE_SIZE, PROT_READ | PROT_WRITE,
                                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        create_pages(bucket_size, arenas[ii].buckets[jj], 2);
        bucket_size *= 2;
      }
    }

    arenas_init = 1;
  }

  rv = pthread_mutex_unlock(&init_mutex);
  assert(rv == 0);
}

void create_pages(size_t block_size, void* start, size_t pages) {
  for (int ii = 0; ii < pages; ii++) {
    void* page_start = start + ii * PAGE_SIZE;
    if (ii + 1 >= pages) {
      create_page(block_size, page_start, 0);
    } else {
      create_page(block_size, page_start, page_start + PAGE_SIZE);
    }
  }
}

void create_page(size_t block_size, void* start, bucket* next_page) {
  assert((size_t)start % 4096 == 0);
  void* page_end = start + PAGE_SIZE;

  // Create bucket
  bucket* page_bucket = (bucket*)start;
  page_bucket->size = block_size;
  page_bucket->next_page = next_page;

  chunk* current_chunk = start + sizeof(bucket);

  while ((void*)current_chunk < page_end - sizeof(chunk)) {
    void* mem_start = (void*)(current_chunk + 1);

    if (mem_start + 256 * block_size < page_end) {
      for (int ii = 0; ii < 4; ii++) {
        current_chunk->map[ii] = 0;
      }
      current_chunk->next = mem_start + 256 * block_size;
    } else {
      int blocks = (page_end - mem_start) / block_size;
      for (int ii = 0; ii < 4; ii++) {
        if (blocks >= 64) {
          current_chunk->map[ii] = 0;
          blocks -= 64;
        } else {
          current_chunk->map[ii] = UINT64_MAX << blocks;
          if (blocks > 0) {
            blocks = 0;
          }
        }
      }

      current_chunk->next = 0;
      return;
    }

    current_chunk = current_chunk->next; 
  }
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
