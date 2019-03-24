

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "par_malloc.h"
#include "xmalloc.h"

const size_t PAGE_SIZE = 1024 * 1000;
static int arenas_init = 0;
static arena arenas[4];
__thread int favorite_arena = -1;


void* xmalloc(size_t bytes) {
  return opt_malloc(bytes);
}

void xfree(void* ptr) { opt_free(ptr); }

void* xrealloc(void* prev, size_t bytes) {
  return opt_realloc(prev, bytes);
}

void* opt_malloc(size_t bytes) {
  if (!arenas_init) {
    init_arenas();
  }

  lock_arena();
  // Actual allocation goes here
  unlock_arena();
  return 0;
}

void opt_free(void* ptr) {
  // Interesting implications here, since the pointer to free doesn't have a block size
  // How do you find the closest page?
  lock_arena();

  unlock_arena();

  return 0;
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
    return 0;
}

void init_arenas() {
  for (int ii = 0; ii < 4; ii++) {
    pthread_mutex_init(&(arenas[ii].mutex), NULL);
    size_t bucket_size = 16;
    for (int jj = 0; jj < 9; jj++) {
      bucket current_bucket = arenas[ii].buckets[jj];
      if (jj < 8) {
        current_bucket.size = bucket_size;
        bucket_size *= 2;
      } else {
        current_bucket.size = 8192;
      }
      current_bucket.start = mmap(0, 2 * PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
      map_pages(current_bucket.size, current_bucket.start, 2);
    }
  }

  arenas_init = 1;
}

void map_pages(size_t block_size, void* start, size_t pages) {
    chunk* current_chunk = start;
    void* mem_start = start + sizeof(chunk);
    if (mem_start + 128 * block_size <=)
}

void map_page(size_t block_size, void* start, void* next_page) {
  assert((size_t)start % 4096 == 0);

  void* page_end = start + PAGE_SIZE;
  chunk* current_chunk = start;

  while (current_chunk < page_end - sizeof(chunk)) {
    void* mem_start = current_chunk + sizeof(chunk);

    if (mem_start + 128 < page_end) {
      current_chunk->map[0] = 0;
      current_chunk->map[1] = 0;
      current_chunk->next = mem_start + 128;
    } else {
      int blocks = page_end - mem_start;
      current_chunk->map[1] = UINT64_MAX;
      if (blocks > 64) {
        current_chunk->map[0] = 0;
        current_chunk->map[1] >>= blocks - 64; 
      } else {
        current_chunk->map[0] = UINT64_MAX;
        current_chunk->map[0] >>= blocks;
      }
    }

    // TODO: Set next
  }
}

void lock_arena() {
  int rv = -1;
  if (favorite_arena < 0) {
    while (rv != 0) {
      for (int ii = 0; ii < 4; ii++) {
        rv = pthread_mutex_trylock(&(arenas[ii].mutex));
        if (rv == 0) {
          favorite_arena = ii;
        }
      }
    }
  } else {
    rv = pthread_mutex_lock(&(arenas[favorite_arena].mutex));
    assert(rv == 0);
  }
}

void unlock_arena() {
  int rv;
  rv = pthread_mutex_unlock(&(arenas[favorite_arena].mutex));
  assert(rv == 0);
}