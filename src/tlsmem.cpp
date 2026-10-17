// SPDX-License-Identifier: Apache-2.0
#include "tlsmem.h"
#include <esp_heap_caps.h>
#include <mbedtls/platform.h>
#include <multi_heap.h>

static multi_heap_handle_t pool = nullptr;
static uint8_t* arena = nullptr;
static size_t arenaSize = 0;
static SemaphoreHandle_t mu = nullptr;

static bool inArena(void* p) { return arena && (uint8_t*)p >= arena && (uint8_t*)p < arena + arenaSize; }

static void* poolCalloc(size_t n, size_t sz) {
  size_t total = n * sz;
  void* p = nullptr;
  if (pool && total) {
    xSemaphoreTake(mu, portMAX_DELAY);
    p = multi_heap_malloc(pool, total);
    xSemaphoreGive(mu);
  }
  if (p) {
    memset(p, 0, total);
    return p;
  }
  return heap_caps_calloc(n, sz, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);   // pool full: behave as before
}

static void poolFree(void* p) {
  if (!p) return;
  if (inArena(p)) {
    xSemaphoreTake(mu, portMAX_DELAY);
    multi_heap_free(pool, p);
    xSemaphoreGive(mu);
  } else {
    heap_caps_free(p);
  }
}

bool tlsmem::begin(size_t bytes) {
  mu = xSemaphoreCreateMutex();
  arena = (uint8_t*)heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!arena) return false;
  arenaSize = bytes;
  pool = multi_heap_register(arena, arenaSize);
  if (!pool) { heap_caps_free(arena); arena = nullptr; return false; }
  mbedtls_platform_set_calloc_free(poolCalloc, poolFree);
  return true;
}

size_t tlsmem::freeBytes() { return pool ? multi_heap_free_size(pool) : 0; }
size_t tlsmem::largestFree() {
  if (!pool) return 0;
  multi_heap_info_t info;
  multi_heap_get_info(pool, &info);
  return info.largest_free_block;
}
