#ifndef PARMALLOC_H
#define PARMALLOC_H

#include <pthread.h>

void* opt_malloc(size_t bytes);
void opt_free(void* ptr);
void* opt_realloc(void* prev, size_t bytes);

void init_arenas();

typedef struct chunk {
  uint64_t map[2];
  chunk* next;
} chunk;

typedef struct bucket {
  size_t size;
  chunk* start;
} bucket;


typedef struct arena {
  pthread_mutex_t mutex;
  bucket buckets[9];
} arena;

#endif