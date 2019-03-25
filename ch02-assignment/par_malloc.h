#ifndef PARMALLOC_H
#define PARMALLOC_H

#include <pthread.h>

void* opt_malloc(size_t bytes);
void opt_free(void* ptr);
void* opt_realloc(void* prev, size_t bytes);

typedef struct chunk {
  uint64_t map[4];
  struct chunk* next;
} chunk;

typedef struct bucket {
  size_t size;
  struct bucket* next_page;
} bucket;


typedef struct arena {
  pthread_mutex_t mutex;
  bucket* buckets[10];
} arena;

void init_arenas();
void lock_arena();
void unlock_arena();
void* first_free_block(bucket* b);
int free_index(uint64_t* map);
void create_pages(size_t block_size, void* start, size_t pages);
void create_page(size_t block_size, void* start, bucket* next_page);
size_t get_block_size(void* ptr);
bucket* closest_bucket(void* ptr);

#endif