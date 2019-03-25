

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <math.h>

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

  if (bytes <= 8192) {
    int target_bucket = log(bytes) / log(2);

    if (pow(2, target_bucket) < bytes) {
      target_bucket++;
    }

    bucket* last_bucket = 0;
    bucket* current_bucket = arenas[favorite_arena].buckets[target_bucket];
    assert(bytes <= current_bucket->size);

    void* ret = 0;

    while (current_bucket != 0) {
      ret = first_free_block(current_bucket->size, current_bucket + 1);

      if (ret == 0) {
        current_bucket = current_bucket->next_page;
      } else {
        return ret;
      }
    }

  } else {
    return malloc(bytes);
  }

  unlock_arena();
  return 0;
}

void* first_free_block(size_t block_size, chunk* start) {
  chunk* current = start;
  while (current != 0) {
    for (int ii = 0; ii < 7; ii++) {
      if (current->map[ii] < UINT64_MAX) {

      }
    }

    current = current->next;
  }

  return current;
}

int first_free(uint64_t* map) {
  for (int ii = 63; ii >= 0; ii--) {
    uint64_t mask = pow(2, ii);
    if (map & mask == 0) {
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
  int rv;
  int found = 0;
  for (int ii = 0; ii < 9; ii++){
      rv = pthread_mutex_lock(&(arenas[ii].mutex));
      assert(rv == 0);
      if (clear_arena(arenas[ii])) {
          found = 1;
      }
      rv = pthread_mutex_unlock(&(arenas[ii].mutex));
      assert(rv == 0);
  }

  if (!found) {
      free(ptr);
  }


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
    return 0;
}

void init_arenas() {
  for (int ii = 0; ii < 4; ii++) {
    pthread_mutex_init(&(arenas[ii].mutex), NULL);
    size_t bucket_size = 16;
    for (int jj = 0; jj < 9; jj++) {
      arenas[ii].buckets[jj] = mmap(0, 2 * PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
      create_pages(bucket_size, arenas[ii].buckets[jj], 2);
      bucket_size *= 2;
    }
  }

  arenas_init = 1;
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

void create_page(size_t block_size, void* start, void* next_page) {
  assert((size_t)start % 4096 == 0);
  void* page_end = start + PAGE_SIZE;

  // Create bucket
  bucket* page_bucket = (bucket*)start;
  page_bucket->size = block_size;
  page_bucket->next_page = next_page;

  chunk* current_chunk = start + sizeof(bucket);

  while (current_chunk < page_end - sizeof(chunk)) {
    void* mem_start = current_chunk + sizeof(chunk);

    if (mem_start + 256 < page_end) {
      for (int ii = 0; ii < 7; ii++) {
        current_chunk->map[ii] = 0;
      }
      current_chunk->next = mem_start + 256;
    } else {
      int blocks = (page_end - mem_start) / block_size;
      for (int ii = 0; ii < 7; ii++) {
        if (blocks >= 64) {
          current_chunk->map[ii] = 0;
          blocks -= 64;
        } else {
          current_chunk->map[ii] = UINT64_MAX >> blocks;
          if (blocks > 0) {
            blocks = 0;
          }
        }
      }

      current_chunk->next = 0;
    }

    current_chunk += (sizeof(chunk) + 256);
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
