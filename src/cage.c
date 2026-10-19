#include "cage.h"
#include "value.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif !defined(ANT_WASM_EMBED)
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

uintptr_t ant_cage_base_address = 0;
size_t ant_cage_reserved_size = 0;

#ifdef ANT_WASM_EMBED

static void *ant_wasm_aligned_alloc(size_t size, size_t align) {
  if (align < sizeof(void *)) align = sizeof(void *);
  size_t remainder = size % align;
  if (remainder) size += align - remainder;
  return aligned_alloc(align, size);
}

void *ant_cage_alloc(size_t size, size_t align) {
  return size ? ant_wasm_aligned_alloc(size, align) : NULL;
}

void *ant_cage_reserve(size_t size, size_t align, size_t keep) {
  (void)keep;
  return size ? ant_wasm_aligned_alloc(size, align) : NULL;
}

void ant_cage_free(void *ptr, size_t size) {
  (void)size;
  free(ptr);
}

bool ant_cage_set_size(size_t size) {
  (void)size;
  return false;
}

_Noreturn void ant_cage_reject_pointer(const void *ptr) {
  (void)ptr;
  __builtin_trap();
}

#else

typedef struct ant_cage_range {
  size_t offset;
  size_t size;
  struct ant_cage_range *next;
} ant_cage_range_t;

static ant_cage_range_t *cage_free_ranges = NULL;

static size_t cage_free_bytes = 0;
static size_t cage_requested_size = ANT_CAGE_MAX_SIZE;

#ifdef _WIN32
static SRWLOCK cage_lock = SRWLOCK_INIT;
static void cage_lock_acquire(void) { AcquireSRWLockExclusive(&cage_lock); }
static void cage_lock_release(void) { ReleaseSRWLockExclusive(&cage_lock); }
#else
static pthread_mutex_t cage_lock = PTHREAD_MUTEX_INITIALIZER;
static void cage_lock_acquire(void) { pthread_mutex_lock(&cage_lock); }
static void cage_lock_release(void) { pthread_mutex_unlock(&cage_lock); }
#endif

static size_t cage_page_size(void) {
  static _Atomic size_t cached = 0;
  size_t value = atomic_load_explicit(&cached, memory_order_relaxed);
  if (value) return value;
#ifdef _WIN32
  SYSTEM_INFO info;
  GetSystemInfo(&info);
  value = info.dwPageSize ? (size_t)info.dwPageSize : 4096u;
#else
  long page = sysconf(_SC_PAGESIZE);
  value = page > 0 ? (size_t)page : 4096u;
#endif
  atomic_store_explicit(&cached, value, memory_order_relaxed);
  return value;
}

static bool size_align_up(size_t value, size_t align, size_t *out) {
  size_t remainder = value % align;
  size_t add = remainder ? align - remainder : 0;
  if (value > SIZE_MAX - add) return false;
  *out = value + add;
  return true;
}

static bool cage_initialize_locked(void) {
  if (ant_cage_base_address) return true;

  void *raw = NULL;
  size_t reserved = cage_requested_size;
  while (reserved >= ANT_CAGE_MIN_SIZE) {
#ifdef _WIN32
    raw = VirtualAlloc(NULL, reserved + ANT_CAGE_ALIGNMENT, MEM_RESERVE, PAGE_NOACCESS);
#else
    raw = mmap(NULL, reserved + ANT_CAGE_ALIGNMENT, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (raw == MAP_FAILED) raw = NULL;
#endif
    if (raw) break;
    reserved >>= 1;
  }
  if (!raw) return false;

  uintptr_t aligned = ((uintptr_t)raw + ANT_CAGE_ALIGNMENT - 1) & ~(uintptr_t)(ANT_CAGE_ALIGNMENT - 1);
  void *base = (void *)aligned;
#ifndef _WIN32
  size_t head = aligned - (uintptr_t)raw;
  if (head) munmap(raw, head);
  if (ANT_CAGE_ALIGNMENT - head) munmap((void *)(aligned + reserved), ANT_CAGE_ALIGNMENT - head);
#endif

  size_t first = cage_page_size();
#ifdef _WIN32
  bool guarded = VirtualAlloc(base, first, MEM_COMMIT, PAGE_READONLY) != NULL;
#else
  bool guarded = mprotect(base, first, PROT_READ) == 0;
#endif
  ant_cage_range_t *range = guarded ? malloc(sizeof(*range)) : NULL;
  if (!range) {
#ifdef _WIN32
    VirtualFree(raw, 0, MEM_RELEASE);
#else
    munmap(base, reserved);
#endif
    return false;
  }

  range->offset = first;
  range->size = reserved - first;
  range->next = NULL;
  cage_free_ranges = range;
  cage_free_bytes = range->size;
  ant_cage_base_address = (uintptr_t)base;
  ant_cage_reserved_size = reserved;
  return true;
}

static void *cage_take_locked(size_t size, size_t align) {
  ant_cage_range_t **link = &cage_free_ranges;

  while (*link) {
    ant_cage_range_t *range = *link;
    size_t address;
    if (
      ant_cage_base_address > SIZE_MAX - range->offset ||
      !size_align_up(ant_cage_base_address + range->offset, align, &address)
    ) return NULL;
    size_t start = address - ant_cage_base_address;
    if (start < range->offset || start - range->offset > range->size) {
      link = &range->next;
      continue;
    }

    size_t prefix = start - range->offset;
    if (size > range->size - prefix) {
      link = &range->next;
      continue;
    }

    size_t suffix = range->size - prefix - size;
    if (prefix && suffix) {
      ant_cage_range_t *tail = malloc(sizeof(*tail));
      if (!tail) return NULL;
      tail->offset = start + size;
      tail->size = suffix;
      tail->next = range->next;
      range->size = prefix;
      range->next = tail;
    } else if (prefix) {
      range->size = prefix;
    } else if (suffix) {
      range->offset = start + size;
      range->size = suffix;
    } else {
      *link = range->next;
      free(range);
    }

    cage_free_bytes -= size;
    return (void *)(ant_cage_base_address + start);
  }

  return NULL;
}

static void cage_put_locked(size_t offset, size_t size) {
  cage_free_bytes += size;
  
  ant_cage_range_t *prev = NULL;
  ant_cage_range_t *next = cage_free_ranges;
  
  while (next && next->offset < offset) {
    prev = next;
    next = next->next;
  }

  if (prev && prev->offset + prev->size == offset) {
    prev->size += size;
    if (next && prev->offset + prev->size == next->offset) {
      prev->size += next->size;
      prev->next = next->next;
      free(next);
    }
    return;
  }

  if (next && offset + size == next->offset) {
    next->offset = offset;
    next->size += size;
    return;
  }

  ant_cage_range_t *range = malloc(sizeof(*range));
  if (!range) {
    cage_free_bytes -= size;
    return;
  }
  range->offset = offset;
  range->size = size;
  range->next = next;
  if (prev) prev->next = range;
  else cage_free_ranges = range;
}

static bool cage_commit(void *ptr, size_t size) {
#ifdef _WIN32
  return VirtualAlloc(ptr, size, MEM_COMMIT, PAGE_READWRITE) != NULL;
#else
  return mprotect(ptr, size, PROT_READ | PROT_WRITE) == 0;
#endif
}

static bool cage_decommit(void *ptr, size_t size) {
#ifdef _WIN32
  return VirtualFree(ptr, size, MEM_DECOMMIT) != 0;
#else
  void *result = mmap(
    ptr, size, PROT_NONE, MAP_FIXED | MAP_PRIVATE | MAP_ANON, -1, 0
  );
  return result != MAP_FAILED;
#endif
}

static void *cage_take(size_t size, size_t align, bool commit, size_t keep) {
  if (!size) return NULL;

  size_t page = cage_page_size();
  size_t rounded;
  if (!size_align_up(size, page, &rounded)) return NULL;
  if (align < page) align = page;
  if ((align & (align - 1u)) != 0) return NULL;

  cage_lock_acquire();
  if (!cage_initialize_locked()) {
    cage_lock_release();
    return NULL;
  }

  void *ptr = cage_free_bytes >= keep && cage_free_bytes - keep >= rounded
    ? cage_take_locked(rounded, align)
    : NULL;
  if (ptr && commit && !cage_commit(ptr, rounded)) {
    cage_put_locked((uintptr_t)ptr - ant_cage_base_address, rounded);
    ptr = NULL;
  }
  cage_lock_release();
  return ptr;
}

void *ant_cage_alloc(size_t size, size_t align) {
  return cage_take(size, align, true, 0);
}

void *ant_cage_reserve(size_t size, size_t align, size_t keep) {
  return cage_take(size, align, false, keep);
}

void ant_cage_free(void *ptr, size_t size) {
  if (!ptr || !size || !ant_cage_contains(ptr)) return;

  size_t rounded;
  if (!size_align_up(size, cage_page_size(), &rounded)) return;
  size_t offset = (uintptr_t)ptr - ant_cage_base_address;
  if (offset > ant_cage_reserved_size || rounded > ant_cage_reserved_size - offset)
    return;

  cage_lock_acquire();
  if (cage_decommit(ptr, rounded)) cage_put_locked(offset, rounded);
  cage_lock_release();
}

bool ant_cage_set_size(size_t size) {
  if (size < ANT_CAGE_MIN_SIZE || size - 1 > NANBOX_DATA_MASK || size % cage_page_size()) return false;
  
  cage_lock_acquire();
  bool ok = ant_cage_base_address == 0;
  
  if (ok) cage_requested_size = size;
  cage_lock_release();
  
  return ok;
}

_Noreturn void ant_cage_reject_pointer(const void *ptr) {
  fprintf(stderr, "ANT FATAL: pointer %p is outside the managed cage\n", ptr);
  abort();
}

#endif
