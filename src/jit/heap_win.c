#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <stdatomic.h>
#include <windows.h>
#include "heap.h"

size_t jit_heap_page_round(size_t size) {
  static _Atomic size_t cached = 0;
  size_t page = atomic_load_explicit(&cached, memory_order_relaxed);
  
  if (!page) {
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    page = info.dwPageSize ? (size_t)info.dwPageSize : 4096u;
    atomic_store_explicit(&cached, page, memory_order_relaxed);
  }
  
  return (size + page - 1) / page * page;
}

void *jit_heap_os_reserve(size_t size) {
  return VirtualAlloc(NULL, size, MEM_RESERVE, PAGE_NOACCESS);
}

bool jit_heap_os_commit(void *p, size_t size) {
  return VirtualAlloc(p, size, MEM_COMMIT, PAGE_READWRITE) != NULL;
}

void jit_heap_os_release(void *p, size_t size) {
  (void)size;
  VirtualFree(p, 0, MEM_RELEASE);
}

jit_heap_chunk_t *jit_heap_chunk_map(void) {
  char *p = jit_heap_os_reserve(2 * JIT_HEAP_CHUNK_SIZE);
  if (!p) return NULL;
  
  char *base = 
    (char *)(((uintptr_t)p + JIT_HEAP_CHUNK_SIZE - 1) & 
    ~(uintptr_t)(JIT_HEAP_CHUNK_SIZE - 1));
  
  if (!jit_heap_os_commit(base, JIT_HEAP_CHUNK_SIZE)) {
    jit_heap_os_release(p, 2 * JIT_HEAP_CHUNK_SIZE);
    return NULL;
  }
  
  jit_heap_chunk_t *c = (jit_heap_chunk_t *)base;
  c->reservation = p;
  c->reservation_size = 2 * JIT_HEAP_CHUNK_SIZE;
  return c;
}

#endif
