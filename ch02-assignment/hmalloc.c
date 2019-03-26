#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <pthread.h>
#include <string.h>

#include "hmalloc.h"

/*
  typedef struct hm_stats {
  long pages_mapped;
  long pages_unmapped;
  long chunks_allocated;
  long chunks_freed;
  long free_length;
  } hm_stats;
*/

const size_t PAGE_SIZE = 4096;
static hm_stats stats;  // This initializes the stats to 0.
static size_t* free_list = 0;
static pthread_mutex_t free_list_mutex = PTHREAD_MUTEX_INITIALIZER;

long free_list_length() {
  long length = 0;

  int rv = pthread_mutex_lock(&free_list_mutex);
  assert(rv == 0);

  size_t* next = free_list;
  while (next != 0) {
    length++;
    next = *((size_t**)next + 1);
  }
  
  rv = pthread_mutex_unlock(&free_list_mutex);
  assert(rv == 0);

  return length;
}

hm_stats* hgetstats() {
  stats.free_length = free_list_length();
  return &stats;
}

void hprintstats() {
  stats.free_length = free_list_length();

  int rv = pthread_mutex_lock(&free_list_mutex);
  assert(rv == 0);

  fprintf(stderr, "\n== husky malloc stats ==\n");
  fprintf(stderr, "Mapped:   %ld\n", stats.pages_mapped);
  fprintf(stderr, "Unmapped: %ld\n", stats.pages_unmapped);
  fprintf(stderr, "Allocs:   %ld\n", stats.chunks_allocated);
  fprintf(stderr, "Frees:    %ld\n", stats.chunks_freed);
  fprintf(stderr, "Freelen:  %ld\n", stats.free_length);

  rv = pthread_mutex_unlock(&free_list_mutex);
  assert(rv == 0);
}

static size_t div_up(size_t xx, size_t yy) {
  // This is useful to calculate # of pages
  // for large allocations.
  size_t zz = xx / yy;

  if (zz * yy == xx) {
    return zz;
  } else {
    return zz + 1;
  }
}

void* hmalloc(size_t size) {
  int rv = pthread_mutex_lock(&free_list_mutex);
  assert(rv == 0);

  stats.chunks_allocated += 1;
  size += sizeof(size_t);

  void* space;

  if (size < PAGE_SIZE) {
    // Allocating blocks smaller than a page (after accounting for the size header)
    if (size < 2 * sizeof(size_t)) {
      // Ensure block is at least minimum free cell size
      size = 2 * sizeof(size_t);
    }

    space = first_free(size);
    
    if (space == 0) {
      // No free cell available, allocate new page
      space = mmap(free_list, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
      assert(space != (void*)-1);
      stats.pages_mapped++;
      if (size <= PAGE_SIZE - 2 * sizeof(size_t)) {
        // Leftover is enough for a free cell, allocated block is size specfied
        size_t* free = (size_t*)((char*)space + size);
        *free = PAGE_SIZE - size;
        free_list_add(free);
      } else {
        // Leftover is not enough, allocated block is one page
        size = PAGE_SIZE;
      }
    } else {
      // Free cell available
      size_t free_size = *((size_t*)space);
      size_t new_free_size = free_size - size;
      if (new_free_size < 2 * sizeof(size_t)) {
        // If leftover free is not enough for block, allocated block is size of free cell
        size = free_size;
        free_list_remove(space, 0);
      } else {
        // Leftover free is enough for a new free block, allocated block is size specified
        free_list_remove(space, new_free_size);
      }
    }
  } else {
    // Allocating blocks 1 page and above in size
    size_t pages_needed = div_up(size, PAGE_SIZE);
    size = pages_needed * PAGE_SIZE;
    space = mmap(0, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(space != (void*)-1);
    stats.pages_mapped += pages_needed;
  }

  rv = pthread_mutex_unlock(&free_list_mutex);
  assert(rv == 0);

  *((size_t*)space) = size;

  return (void*)((size_t*)space + 1);
}

void hfree(void* item) {
  int rv = pthread_mutex_lock(&free_list_mutex);
  assert(rv == 0);

  stats.chunks_freed += 1;

  size_t* block_start = (size_t*)item - 1;
  size_t item_size = *(block_start);
  if (item_size <= PAGE_SIZE) {
    // If item is less than or equal to a page (after accounting for the size header), add block to free list
    free_list_add(block_start);
  } else {
    // If item is larger than a page (with size header), unmap corresponding pages
    int ret = munmap(block_start, item_size);
    assert(ret == 0);
    stats.pages_unmapped += item_size / PAGE_SIZE;
  }

  rv = pthread_mutex_unlock(&free_list_mutex);
  assert(rv == 0);
}

void* hrealloc(void* item, size_t size) {
  int rv = pthread_mutex_lock(&free_list_mutex);
  assert(rv == 0);

  size_t* block_start = (size_t*)item - 1;
  size_t item_size = *(block_start);
  size_t* new_item = item;
  size_t new_block_size = size + sizeof(size_t);

  if (new_block_size < item_size && item_size - new_block_size >= 2 * sizeof(size_t)) {
    *(block_start) = new_block_size;
    size_t* new_free = block_start + new_block_size;
    *(new_free) = item_size - new_block_size;
    free_list_add(new_free);

    rv = pthread_mutex_unlock(&free_list_mutex);
    assert(rv == 0);
  } else if (new_block_size > item_size) {
    rv = pthread_mutex_unlock(&free_list_mutex);
    assert(rv == 0);

    new_item = hmalloc(size);
    memcpy(new_item, item, item_size - sizeof(size_t));
    hfree(item);
  }

  return new_item;
}

// Returns the address of the first free block that is the size specified or greater
void* first_free(size_t size) {
  size_t* current_free = free_list;
  while (current_free != 0) {
    size_t free_size = *current_free;
    size_t* next = *((size_t**)current_free + 1);
    if (size <= free_size) {
      return (void*)current_free;
    } else {
      current_free = next;
    }
  }
  
  return (void*)current_free;
}

// Adds a block to the free list
void free_list_add(size_t* block) {
  size_t* current = free_list;
  while (current != 0) {
    size_t* next = *((size_t**)current + 1);
    if (current > block) {
      // Block is the earliest in the list
      free_list = block;
      join_free(block, current);
      return;
    } else if (current < block && next > block) {
      // Block goes in between the current and next blocks
      int ret = join_free(current, block);
      if (ret) {
        // Block was coalesced with current
        join_free(current, next);
      } else {
        // Block is separate
        join_free(block, next);
      }
      return;
    } else {
      current = next;
    }
  }

  // Default case, only used to insert first free cell
  free_list = block;
  *((size_t**)block + 1) = 0;
}


// Removes or resizes a free block
void free_list_remove(size_t* block, size_t new_size) {
  size_t* current = free_list;
  while (current != 0) {
    size_t* next = *((size_t**)current + 1);
    if (block == free_list) {
      // Block is the first in the free list
      free_list = resize_free(block, new_size);
      return;
    } else if (next == block) {
      // Block is next in the free list
      join_free(current, resize_free(block, new_size));
      return;
    } else {
      current = *((size_t**)current + 1);
    }
  }
}

// Joins two free blocks and coalesces them if possible
// Returns 1 if blocks were combined, 0 if they were just linked
int join_free(size_t* first, size_t* next) {
  size_t* first_end = (size_t*)((char*)first + *first);
  if (first_end == next) {
    // Combine blocks
    *first += *next;
    *((size_t**)first + 1) = *((size_t**)next + 1);
    return 1;
  } else {
    // Link blocks
    *((size_t**)first + 1) = next;
    return 0;
  }
}

// Resizes a free block to a given smaller size
// Returns the new block or the next block if the new size is 0
size_t* resize_free(size_t* block, size_t new_size) {
  // New size cannot be less than the size of a free cell
  assert(new_size >= 2 * sizeof(size_t) || new_size == 0);

  // New size must be smaller than the block's current size
  assert(*block > new_size);
  size_t* next = *((size_t**)block + 1);
  if (new_size) {
    // Shrink block
    size_t* new_block = (size_t*)((char*)block + (*block - new_size));
    *new_block = new_size;
    *((size_t**)new_block +  1) = next;
    return new_block;
  } else {
    // Ignore block, block size is now 0
    return next;
  }
}
