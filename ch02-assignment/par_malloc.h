#ifndef PARMALLOC_H
#define PARMALLOC_H

#include <pthread.h>

void* opt_malloc(size_t bytes);
void opt_free(void* ptr);
void* opt_realloc(void* prev, size_t bytes);

typedef struct bucket {
  size_t size;
  struct bucket* next_page;
  pthread_mutex_t mutex;
} bucket;


typedef struct arena {
  pthread_mutex_t mutex;
  bucket* buckets[8];
} arena;

// How many 64 bit maps are needed to represent the bucket in a page and what the last map should be
const size_t pageMaps[8] = {993, 499, 250, 125, 63, 32, 16, 8};
const size_t lastMap[8] = {~0UL << 12,
                           ~0UL << 1,
                           ~0UL << 31,
                           ~0UL << 55,
                           ~0UL << 29,
                           ~0UL << 15,
                           ~0UL << 39,
                           ~0UL << 51};

void init_arenas();
void lock_arena();
void unlock_arena();
void* first_free_block(bucket* b);
void init_page(size_t block_size, void* start);
size_t get_block_size(void* ptr);
bucket* closest_bucket(void* ptr);

#endif