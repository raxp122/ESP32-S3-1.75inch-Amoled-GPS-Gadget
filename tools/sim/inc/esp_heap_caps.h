#pragma once
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_INTERNAL 2
#define MALLOC_CAP_DMA 4
static inline void heap_caps_free(void *p) { free(p); }
static inline void *heap_caps_malloc(size_t n, int c) { (void)c; return malloc(n); }
static inline size_t heap_caps_get_free_size(int c) { return c == MALLOC_CAP_SPIRAM ? 7600000 : 180000; }
