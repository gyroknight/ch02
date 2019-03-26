#ifndef PARMALLOC_H
#define PARMALLOC_H

#include <pthread.h>

void* opt_malloc(size_t bytes);
void opt_free(void* ptr);
void* opt_realloc(void* prev, size_t bytes);

typedef struct free_cell {
  size_t size;
  struct free_cell* next;
} free_cell;

typedef struct bucket {
  size_t size;
  struct bucket* next_page;
  pthread_mutex_t lock;
  free_cell* free;
} bucket;


typedef struct arena {
  pthread_mutex_t mutex;
  bucket* buckets[7];
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
void remove_free(bucket* b);
int join_free(free_cell* first, free_cell* next);

#endif