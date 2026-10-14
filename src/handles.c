#include "handles.h"
#include "cage.h"
#include "value.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#endif

enum {
  CFUNC_SLAB_SIZE = 64 * 1024,
  EXTERNAL_CHUNK_BITS = 10,
  EXTERNAL_CHUNK_SIZE = 1 << EXTERNAL_CHUNK_BITS,
  EXTERNAL_CHUNK_COUNT = 4096,
  REGISTRY_BUCKET_BITS = 11,
  REGISTRY_BUCKET_COUNT = 1 << REGISTRY_BUCKET_BITS,
};

typedef struct ant_cfunc_entry {
  ant_cfunc_meta_t meta;
  const ant_cfunc_meta_t *source;

  struct ant_cfunc_entry *source_next;
  struct ant_cfunc_entry *entrypoint_next;
} ant_cfunc_entry_t;

typedef struct ant_cfunc_slab {
  struct ant_cfunc_slab *next;
  size_t used;
} ant_cfunc_slab_t;

typedef struct ant_external_entry {
  void *ptr;
  uint64_t handle;
  struct ant_external_entry *next;
} ant_external_entry_t;

static uint64_t external_count = 0;
typedef _Atomic(void *) ant_external_slot_t;
static ant_cfunc_slab_t *cfunc_slabs = NULL;

static _Atomic(ant_external_slot_t *) external_chunks[EXTERNAL_CHUNK_COUNT];
static _Atomic(ant_cfunc_entry_t *) cfunc_source_buckets[REGISTRY_BUCKET_COUNT];
static _Atomic(ant_cfunc_entry_t *) cfunc_entrypoint_buckets[REGISTRY_BUCKET_COUNT];
static _Atomic(ant_external_entry_t *) external_buckets[REGISTRY_BUCKET_COUNT];

#ifdef _WIN32
static SRWLOCK handles_lock = SRWLOCK_INIT;
static void handles_lock_acquire(void) { AcquireSRWLockExclusive(&handles_lock); }
static void handles_lock_release(void) { ReleaseSRWLockExclusive(&handles_lock); }
#else
static pthread_mutex_t handles_lock = PTHREAD_MUTEX_INITIALIZER;
static void handles_lock_acquire(void) { pthread_mutex_lock(&handles_lock); }
static void handles_lock_release(void) { pthread_mutex_unlock(&handles_lock); }
#endif

static inline size_t registry_bucket(uintptr_t key) {
  return (size_t)(((uint64_t)key * 0x9E3779B97F4A7C15ull) >> (64 - REGISTRY_BUCKET_BITS));
}

static ant_cfunc_entry_t *cfunc_find_source(size_t bucket, const ant_cfunc_meta_t *meta) {
  ant_cfunc_entry_t *entry = atomic_load_explicit(&cfunc_source_buckets[bucket], memory_order_acquire);
  while (entry && entry->source != meta) entry = entry->source_next;
  return entry;
}

static ant_cfunc_entry_t *cfunc_find_entrypoint(size_t bucket, ant_cfunc_t fn) {
  ant_cfunc_entry_t *entry = atomic_load_explicit(&cfunc_entrypoint_buckets[bucket], memory_order_acquire);
  while (entry && entry->meta.fn != fn) entry = entry->entrypoint_next;
  return entry;
}

static ant_external_entry_t *external_find(size_t bucket, void *ptr) {
  ant_external_entry_t *entry = atomic_load_explicit(&external_buckets[bucket], memory_order_acquire);
  while (entry && entry->ptr != ptr) entry = entry->next;
  return entry;
}

static ant_cfunc_entry_t *cfunc_alloc_locked(void) {
  size_t align = _Alignof(ant_cfunc_entry_t);
  size_t offset = cfunc_slabs
    ? (cfunc_slabs->used + align - 1u) & ~(align - 1u)
    : CFUNC_SLAB_SIZE;

  if (offset > CFUNC_SLAB_SIZE - sizeof(ant_cfunc_entry_t)) {
    ant_cfunc_slab_t *slab = ant_cage_alloc(
      CFUNC_SLAB_SIZE, _Alignof(max_align_t));
    if (!slab) return NULL;
    slab->next = cfunc_slabs;
    slab->used = sizeof(*slab);
    cfunc_slabs = slab;
    offset = (slab->used + align - 1u) & ~(align - 1u);
  }

  ant_cfunc_entry_t *entry = (ant_cfunc_entry_t *)(
    (unsigned char *)cfunc_slabs + offset);
  cfunc_slabs->used = offset + sizeof(*entry);
  return entry;
}

static bool external_publish(uint64_t handle, void *ptr) {
  uint64_t index = handle - 1;
  
  size_t chunk_index = (size_t)(index >> EXTERNAL_CHUNK_BITS);
  size_t slot_index = (size_t)(index & (EXTERNAL_CHUNK_SIZE - 1u));
  if (chunk_index >= EXTERNAL_CHUNK_COUNT) return false;

  ant_external_slot_t *chunk = atomic_load_explicit(
    &external_chunks[chunk_index], 
    memory_order_acquire
  );
  
  if (!chunk) {
    chunk = calloc(EXTERNAL_CHUNK_SIZE, sizeof(*chunk));
    if (!chunk) return false;
    atomic_store_explicit(&external_chunks[chunk_index], chunk, memory_order_release);
  }
  
  atomic_store_explicit(&chunk[slot_index], ptr, memory_order_release);
  return true;
}

static ant_cfunc_entry_t *cfunc_create_locked(const ant_cfunc_meta_t *meta) {
  ant_cfunc_entry_t *entry = cfunc_alloc_locked();
  if (!entry) return NULL;

  entry->meta = *meta;
  entry->source = NULL;
  entry->source_next = NULL;
  entry->entrypoint_next = NULL;
  
  return entry;
}

const ant_cfunc_meta_t *ant_cfunc_meta_intern(const ant_cfunc_meta_t *meta) {
  if (!meta) return NULL;
  size_t bucket = registry_bucket((uintptr_t)meta);
  ant_cfunc_entry_t *entry = cfunc_find_source(bucket, meta);
  if (entry) return &entry->meta;
  
  handles_lock_acquire();
  entry = cfunc_find_source(bucket, meta);
  if (!entry && (entry = cfunc_create_locked(meta))) {
    entry->source = meta;
    entry->source_next = atomic_load_explicit(&cfunc_source_buckets[bucket], memory_order_relaxed);
    atomic_store_explicit(&cfunc_source_buckets[bucket], entry, memory_order_release);
  }
  handles_lock_release();
  
  return entry ? &entry->meta : NULL;
}

const ant_cfunc_meta_t *ant_cfunc_meta_source(const ant_cfunc_meta_t *stored) {
  return stored ? ((const ant_cfunc_entry_t *)stored)->source : NULL;
}

const ant_cfunc_meta_t *ant_cfunc_meta_derive(const ant_cfunc_meta_t *base, const ant_cfunc_meta_t *meta) {
  if (!meta) return NULL;
  handles_lock_acquire();
  ant_cfunc_entry_t *entry = cfunc_create_locked(meta);
  if (entry) entry->source = ant_cfunc_meta_source(base);
  handles_lock_release();
  return entry ? &entry->meta : NULL;
}

const ant_cfunc_meta_t *ant_cfunc_meta_for_entrypoint(ant_cfunc_t fn) {
  if (!fn) return NULL;
  size_t bucket = registry_bucket((uintptr_t)fn);
  ant_cfunc_entry_t *entry = cfunc_find_entrypoint(bucket, fn);
  if (entry) return &entry->meta;
  
  handles_lock_acquire();
  entry = cfunc_find_entrypoint(bucket, fn);
  if (!entry && (entry = cfunc_create_locked(&(ant_cfunc_meta_t){ .fn = fn }))) {
    entry->entrypoint_next = atomic_load_explicit(&cfunc_entrypoint_buckets[bucket], memory_order_relaxed);
    atomic_store_explicit(&cfunc_entrypoint_buckets[bucket], entry, memory_order_release);
  }
  handles_lock_release();
  
  return entry ? &entry->meta : NULL;
}

uint64_t ant_external_handle_intern(void *ptr) {
  if (!ptr) return 0;
  size_t bucket = registry_bucket((uintptr_t)ptr);
  ant_external_entry_t *entry = external_find(bucket, ptr);
  if (entry) return entry->handle;

  handles_lock_acquire();
  entry = external_find(bucket, ptr);
  if (entry) {
    handles_lock_release();
    return entry->handle;
  }

  entry = external_count < NANBOX_DATA_MASK ? malloc(sizeof(*entry)) : NULL;
  if (entry) {
    entry->ptr = ptr;
    entry->handle = external_count + 1;
  }
  
  if (!entry || !external_publish(entry->handle, ptr)) {
    free(entry);
    handles_lock_release();
    return 0;
  }

  external_count = entry->handle;
  entry->next = atomic_load_explicit(&external_buckets[bucket], memory_order_relaxed);
  atomic_store_explicit(&external_buckets[bucket], entry, memory_order_release);
  handles_lock_release();
  
  return entry->handle;
}

void *ant_external_handle_get(uint64_t handle) {
  if (!handle) return NULL;
  
  uint64_t index = handle - 1;
  size_t chunk_index = (size_t)(index >> EXTERNAL_CHUNK_BITS);
  size_t slot_index = (size_t)(index & (EXTERNAL_CHUNK_SIZE - 1u));
  if (chunk_index >= EXTERNAL_CHUNK_COUNT) return NULL;
  
  ant_external_slot_t *chunk = atomic_load_explicit(
    &external_chunks[chunk_index], 
    memory_order_acquire
  );
  
  return chunk ? atomic_load_explicit(&chunk[slot_index], memory_order_acquire) : NULL;
}
