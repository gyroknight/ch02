#ifndef HMALLOC_H
#define HMALLOC_H

// Husky Malloc Interface
// cs3650 Starter Code

typedef struct hm_stats {
    long pages_mapped;
    long pages_unmapped;
    long chunks_allocated;
    long chunks_freed;
    long free_length;
} hm_stats;

hm_stats* hgetstats();
void hprintstats();

void* hmalloc(size_t size);
void hfree(void* item);
void* hrealloc(void* item, size_t size);

void* first_free(size_t size);
void free_list_add(size_t* block);
void free_list_remove(size_t* block, size_t new_size);
int join_free(size_t* first, size_t* next);
size_t* resize_free(size_t* block, size_t new_size);

#endif