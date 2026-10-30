#include "internal.h"
#include "gc/objects.h"
#include "gc/ropes.h"
#include "gc.h"
#include "pool.h"
#include "gc/strings.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

typedef enum {
  GC_ROPE_POOL_MISC,
  GC_ROPE_POOL_OLD,
  GC_ROPE_POOL_YOUNG,
} gc_rope_pool_kind_t;

typedef struct gc_rope_mark {
  uintptr_t base;
  uintptr_t end;
  ant_pool_block_t *block;
  ant_pool_t *pool;
  gc_rope_pool_kind_t kind;
  bool has_live;
} gc_rope_mark_t;

static int rope_mark_cmp(const void *a, const void *b) {
  const gc_rope_mark_t *ma = (const gc_rope_mark_t *)a;
  const gc_rope_mark_t *mb = (const gc_rope_mark_t *)b;
  if (ma->base < mb->base) return -1;
  if (ma->base > mb->base) return 1;
  return 0;
}

static bool rope_marks_reserve(ant_t *js, size_t needed) {
  if (needed <= js->rope_gc.mark_cap) return true;

  size_t cap = js->rope_gc.mark_cap ? js->rope_gc.mark_cap : 32u;
  while (cap < needed) {
    if (cap > SIZE_MAX / 2u) {
      cap = needed;
      break;
    }
    cap *= 2u;
  }
  if (cap > SIZE_MAX / sizeof(gc_rope_mark_t)) return false;

  gc_rope_mark_t *marks = (gc_rope_mark_t *)realloc(
    js->rope_gc.marks, cap * sizeof(*marks)
  );
  if (!marks && cap != needed) {
    cap = needed;
    marks = (gc_rope_mark_t *)realloc(
      js->rope_gc.marks, cap * sizeof(*marks)
    );
  }
  if (!marks) return false;
  js->rope_gc.marks = marks;
  js->rope_gc.mark_cap = cap;
  return true;
}

static bool rope_marks_count_pool(ant_pool_t *pool, size_t *count) {
  for (ant_pool_block_t *b = pool->head; b; b = b->next) {
    if (b->used == 0) continue;
    if (*count == SIZE_MAX) return false;
    (*count)++;
  }
  return true;
}

static void rope_marks_add_pool(
  ant_t *js, ant_pool_t *pool, gc_rope_pool_kind_t kind
) {
  for (ant_pool_block_t *b = pool->head; b; b = b->next) {
    if (b->used == 0) continue;
    gc_rope_mark_t *marks = js->rope_gc.marks;
    gc_rope_mark_t *m = &marks[js->rope_gc.mark_count++];
    m->base = (uintptr_t)b->data;
    m->end = m->base + b->used;
    m->block = b;
    m->pool = pool;
    m->kind = kind;
    m->has_live = false;
  }
}

static void rope_nodes_clear_epochs(ant_pool_t *pool) {
  for (ant_pool_block_t *b = pool->head; b; b = b->next) for (
    size_t off = 0; off + sizeof(ant_rope_heap_t) <= 
    b->used; off += sizeof(ant_rope_heap_t)
  ) {
    ant_rope_heap_t *rope = (ant_rope_heap_t *)(b->data + off);
    rope->mark_epoch = 0;
  }
}

gc_ropes_begin_result_t gc_ropes_begin(ant_t *js, bool minor) {
  js->rope_gc.last_mark = NULL;
  size_t needed = 0;
  if (!rope_marks_count_pool(&js->pool.rope, &needed) ||
      (!minor && !rope_marks_count_pool(&js->rope_gc.old, &needed)) ||
      !rope_marks_count_pool(&js->rope_gc.young, &needed) ||
      !rope_marks_reserve(js, needed)) {
    js->rope_gc.mark_count = 0;
    js->rope_gc.minor_marking = false;
    js->rope_gc.conservative_marking = !minor;
    return minor
      ? GC_ROPES_BEGIN_RETRY_MAJOR
      : GC_ROPES_BEGIN_CONSERVATIVE_MAJOR;
  }

  js->rope_gc.mark_count = 0;
  js->rope_gc.minor_marking = minor;
  js->rope_gc.conservative_marking = false;
  if (++js->rope_gc.mark_epoch == 0) {
    js->rope_gc.mark_epoch = 1;
    rope_nodes_clear_epochs(&js->rope_gc.old);
    rope_nodes_clear_epochs(&js->rope_gc.young);
  }

  rope_marks_add_pool(js, &js->pool.rope, GC_ROPE_POOL_MISC);
  if (!minor) rope_marks_add_pool(js, &js->rope_gc.old, GC_ROPE_POOL_OLD);
  rope_marks_add_pool(js, &js->rope_gc.young, GC_ROPE_POOL_YOUNG);

  if (js->rope_gc.mark_count > 1)
    qsort(js->rope_gc.marks, js->rope_gc.mark_count,
    sizeof(gc_rope_mark_t), rope_mark_cmp);
  return GC_ROPES_BEGIN_NORMAL;
}

static void rope_mark_conservative_pool(ant_t *js, ant_pool_t *pool) {
  for (ant_pool_block_t *b = pool->head; b; b = b->next)
    if (b->used) gc_mark_conservative_range(js, b->data, b->used);
}

void gc_ropes_mark_conservative_roots(ant_t *js) {
  if (!js || !js->rope_gc.conservative_marking) return;
  rope_mark_conservative_pool(js, &js->pool.rope);
  rope_mark_conservative_pool(js, &js->rope_gc.old);
  rope_mark_conservative_pool(js, &js->rope_gc.young);
}

static gc_rope_mark_t *rope_mark_find(ant_t *js, const void *ptr) {
  if (!js || !ptr) return NULL;
  uintptr_t p = (uintptr_t)ptr;
  
  gc_rope_mark_t *marks = js->rope_gc.marks;
  gc_rope_mark_t *last = js->rope_gc.last_mark;
  
  if (last && p >= last->base && p < last->end) return last;
  size_t lo = 0, hi = js->rope_gc.mark_count;
  
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2u;
    gc_rope_mark_t *m = &marks[mid];
    if (p < m->base) hi = mid;
    else if (p >= m->end) lo = mid + 1u;
    else { js->rope_gc.last_mark = m; return m; }
  }
  
  return NULL;
}

gc_rope_mark_result_t gc_ropes_mark(
  ant_t *js, const void *ptr, size_t size, size_t align
) {
  if (!js || !ptr || size == 0) return GC_ROPE_MARK_INVALID;
  if (align > 1 && (align & (align - 1u)) != 0) return GC_ROPE_MARK_INVALID;
  
  uintptr_t p = (uintptr_t)ptr;
  if (align > 1 && (p & (align - 1u)) != 0) return GC_ROPE_MARK_INVALID;
  
  gc_rope_mark_t *m = rope_mark_find(js, ptr);
  if (!m || size > m->end - p) return GC_ROPE_MARK_INVALID;

  if (m->kind == GC_ROPE_POOL_MISC) {
    m->has_live = true;
    return GC_ROPE_MARK_TRACE;
  }

  if ((p - m->base) % sizeof(ant_rope_heap_t) != 0 || sizeof(ant_rope_heap_t) > m->end - p) 
    return GC_ROPE_MARK_INVALID;

  if (js->rope_gc.minor_marking && m->kind == GC_ROPE_POOL_OLD) return GC_ROPE_MARK_SKIP;
  ant_rope_heap_t *rope = (ant_rope_heap_t *)ptr;
  
  if (rope->mark_epoch == js->rope_gc.mark_epoch) return GC_ROPE_MARK_SKIP;
  rope->mark_epoch = js->rope_gc.mark_epoch;
  m->has_live = true;
  
  return GC_ROPE_MARK_TRACE;
}

static void unlink_rope_block(ant_pool_t *pool, ant_pool_block_t *block) {
  if (block->prev) block->prev->next = block->next;
  else pool->head = block->next;
  if (block->next) block->next->prev = block->prev;
  block->next = NULL;
  block->prev = NULL;
}

static void recycle_rope_block(ant_pool_t *pool, ant_pool_block_t *block) {
  block->used = 0;
  pool_free_set_next(block, pool->free_head);
  pool->free_head = block;
}

static void promote_rope_block(ant_t *js, ant_pool_block_t *block) {
  for (size_t off = 0; off + sizeof(ant_rope_heap_t) <= block->used; off += sizeof(ant_rope_heap_t)) {
    ant_rope_heap_t *rope = (ant_rope_heap_t *)(block->data + off);
    rope->flags &= (uint16_t)~ANT_ROPE_FLAG_YOUNG;
  }

  ant_pool_t *old = &js->rope_gc.old;
  block->next = old->head;
  
  if (old->head) old->head->prev = block;
  old->head = block;
}

static void trim_rope_free_blocks(ant_pool_t *pool, size_t keep, size_t budget, bool release) {
  ant_pool_block_t *f = pool->free_head;
  ant_pool_block_t *last = NULL;
  
  size_t kept = 0;
  while (f && kept < keep) {
    size_t bytes = sizeof(ant_pool_block_t) + f->cap;
    if (bytes > budget) break;
    
    budget -= bytes;
    if (release) pool_block_madvise_free(f);
    
    last = f;
    f = pool_free_next(f);
    kept++;
  }
  
  if (last) pool_free_set_next(last, NULL);
  else pool->free_head = NULL;
  
  while (f) {
    ant_pool_block_t *next = pool_free_next(f);
    pool_block_free(f);
    f = next;
  }
}

static size_t rope_young_keep(ant_t *js, bool minor, uint32_t young_blocks) {
  uint32_t *prev = minor ? &js->rope_gc.young_blocks_minor : &js->rope_gc.young_blocks_major;
  uint32_t *keep = minor ? &js->rope_gc.young_keep_minor : &js->rope_gc.young_keep_major;
  
  *keep = young_blocks < *prev ? young_blocks : *prev;
  *prev = young_blocks;
  
  uint32_t both = 
    js->rope_gc.young_keep_major > js->rope_gc.young_keep_minor
    ? js->rope_gc.young_keep_major : js->rope_gc.young_keep_minor;
  
  return both > 2 ? both : 2;
}

void gc_ropes_sweep(ant_t *js, bool minor) {
  uint32_t young_blocks = 0;
  
  if (js->rope_gc.conservative_marking) {
    ANT_ASSERT(!minor, "conservative rope sweep must be a major");
    
    for (ant_pool_block_t *b = js->rope_gc.young.head; b;) {
      ant_pool_block_t *next = b->next;
      young_blocks++;
      unlink_rope_block(&js->rope_gc.young, b);
      
      if (b->used) promote_rope_block(js, b);
      else recycle_rope_block(&js->rope_gc.young, b);
      b = next;
    }
  }

  gc_rope_mark_t *marks = js->rope_gc.marks;
  size_t count = js->rope_gc.mark_count;
  for (size_t i = 0; i < count; i++) {
    gc_rope_mark_t *m = &marks[i];
    if (minor && m->kind != GC_ROPE_POOL_YOUNG) continue;

    if (m->kind == GC_ROPE_POOL_YOUNG) {
      young_blocks++;
      unlink_rope_block(m->pool, m->block);
      if (m->has_live) promote_rope_block(js, m->block);
      else recycle_rope_block(&js->rope_gc.young, m->block);
      continue;
    }

    if (!m->has_live) {
      unlink_rope_block(m->pool, m->block);
      ant_pool_t *recycle = m->kind == GC_ROPE_POOL_OLD
        ? &js->rope_gc.young : &js->pool.rope;
      recycle_rope_block(recycle, m->block);
    }
  }

  trim_rope_free_blocks(&js->pool.rope, 2, SIZE_MAX, true);
  
  trim_rope_free_blocks(
    &js->rope_gc.young, rope_young_keep(js, minor, young_blocks),
    gc_pool_major_threshold(js), false
  );
  
  js->rope_gc.young_alloc = 0;
  js->rope_gc.mark_count = 0;
  js->rope_gc.last_mark = NULL;
  js->rope_gc.minor_marking = false;
  js->rope_gc.conservative_marking = false;
}

ant_offset_t assert_flat_string_len(ant_value_t value, const char **out_ptr) {
  ant_flat_string_t *flat = ant_str_flat_ptr(value);
  assert(flat != NULL);
  ant_offset_t len = flat->len;
  if (out_ptr) *out_ptr = flat->bytes;
  return len;
}

ant_rope_heap_t *assert_rope_ptr(ant_value_t value) {
  assert(vtype(value) == kTypeString);
  assert(str_is_heap_rope(value));
  ant_rope_heap_t *ptr = ant_str_rope_ptr(value);
  assert(ptr != NULL);
  return ptr;
}

ant_offset_t rope_len(ant_value_t value) {
  ant_rope_heap_t *ptr = assert_rope_ptr(value);
  return ptr->len;
}

uint16_t rope_depth(ant_value_t value) {
  ant_rope_heap_t *ptr = assert_rope_ptr(value);
  return ptr->depth;
}

static inline ant_value_t rope_left(ant_value_t value) {
  ant_rope_heap_t *ptr = assert_rope_ptr(value);
  return ptr->left;
}

static inline ant_value_t rope_right(ant_value_t value) {
  ant_rope_heap_t *ptr = assert_rope_ptr(value);
  return ptr->right;
}

ant_value_t rope_cached_flat(ant_value_t value) {
  ant_rope_heap_t *ptr = assert_rope_ptr(value);
  return ptr->cached;
}

static inline void rope_canonicalize_flat(ant_value_t rope, ant_value_t flat) {
  ant_rope_heap_t *ptr = assert_rope_ptr(rope);
  ptr->cached = flat;
  ptr->left = js_mkundef();
  ptr->right = js_mkundef();
  ptr->depth = 0;
  ptr->len = ant_str_flat_ptr(flat)->len;
}

ant_string_builder_t *assert_builder_ptr(ant_value_t value) {
  assert(vtype(value) == kTypeString);
  assert(str_is_heap_builder(value));
  
  ant_string_builder_t *ptr = ant_str_builder_ptr(value);
  assert(ptr != NULL);
  
  return ptr;
}

ant_offset_t builder_len(ant_value_t value) {
  ant_string_builder_t *ptr = assert_builder_ptr(value);
  return ptr->len;
}

static inline ant_value_t builder_cached_flat(ant_value_t value) {
  ant_string_builder_t *ptr = assert_builder_ptr(value);
  return ptr->cached;
}

static inline void builder_set_cached_flat(ant_value_t builder, ant_value_t flat) {
  ant_string_builder_t *ptr = assert_builder_ptr(builder);
  ptr->cached = flat;
}

static bool rope_flatten_into(ant_value_t str, char *dest, ant_offset_t total_len, ant_offset_t *start) {
  assert(vtype(str) == kTypeString);

  ant_value_t local[32];
  ant_value_t *stack = local;
  
  size_t sp = 0, cap = 32;
  ant_offset_t pos = total_len;
  ant_value_t current = str;

  for (;;) {
    while (str_is_heap_rope(current)) {
      ant_value_t cached = rope_cached_flat(current);
      if (vtype(cached) == kTypeString && !str_is_heap_rope(cached)) {
        current = cached;
        break;
      }

      ant_value_t left = rope_left(current);
      current = rope_right(current);
      if (!ant_value_stack_push_with_spill(&stack, &sp, &cap, local, left)) {
        if (stack != local) free(stack);
        return false;
      }
    }

    const char *src;
    ant_offset_t len = assert_flat_string_len(current, &src);
    if (len > pos) {
      if (stack != local) free(stack);
      return false;
    }
    pos -= len;
    memcpy(dest + pos, src, (size_t)len);

    if (sp == 0) break;
    current = stack[--sp];
  }

  if (stack != local) free(stack);
  *start = pos;
  
  return true;
}

ant_value_t rope_flatten(ant_t *js, ant_value_t rope) {
  assert(vtype(rope) == kTypeString);
  if (!str_is_heap_rope(rope)) return rope;

  GC_ROOT_SAVE(root_mark, js);
  GC_ROOT_PIN(js, rope);
  
  ant_value_t cached = rope_cached_flat(rope);
  GC_ROOT_PIN(js, cached);
  if (vtype(cached) == kTypeString && !str_is_heap_rope(cached)) {
    GC_ROOT_RESTORE(js, root_mark);
    return cached;
  }
  
  ant_offset_t total_len = rope_len(rope);
  ant_value_t flat = js_mkstr(js, NULL, total_len);
  GC_ROOT_PIN(js, flat);

  if (is_err(flat)) {
    GC_ROOT_RESTORE(js, root_mark);
    return flat;
  }

  ant_flat_string_t *flat_ptr = (ant_flat_string_t *)vptr(flat);
  ant_offset_t start = 0;
  
  if (!rope_flatten_into(rope, flat_ptr->bytes, total_len, &start)) {
    GC_ROOT_RESTORE(js, root_mark);
    return js_mkerr(js, "string flatten failed");
  }
  
  if (start > 0) memmove(flat_ptr->bytes, flat_ptr->bytes + start, (size_t)(total_len - start));
  flat_ptr->len = total_len - start;
  
  str_flat_finish(flat_ptr, str_detect_ascii_bytes(flat_ptr->bytes, (size_t)flat_ptr->len));
  if (vtype(cached) == kTypeNumber) str_flat_set_utf16_len(flat_ptr, (ant_offset_t)Ant_Value_AsNumberUnchecked(cached));

  rope_canonicalize_flat(rope, flat);
  GC_ROOT_RESTORE(js, root_mark);
  
  return flat;
}

ant_value_t builder_flatten(ant_t *js, ant_value_t builder) {
  assert(vtype(builder) == kTypeString);
  if (!str_is_heap_builder(builder)) return builder;

  GC_ROOT_SAVE(root_mark, js);
  GC_ROOT_PIN(js, builder);

  ant_value_t cached = builder_cached_flat(builder);
  GC_ROOT_PIN(js, cached);
  
  if (vtype(cached) == kTypeString && !str_is_heap_rope(cached) && !str_is_heap_builder(cached)) {
    GC_ROOT_RESTORE(js, root_mark);
    return cached;
  }

  ant_string_builder_t *ptr = assert_builder_ptr(builder);
  ant_value_t snapshot = ptr->snapshot;
  GC_ROOT_PIN(js, snapshot);
  ant_value_t flat = js_mkstr(js, NULL, (size_t)ptr->len);
  
  GC_ROOT_PIN(js, flat);
  if (is_err(flat)) {
    GC_ROOT_RESTORE(js, root_mark);
    return flat;
  }

  ant_flat_string_t *flat_ptr = (ant_flat_string_t *)vptr(flat);
  size_t cursor = 0;

  if (vtype(snapshot) == kTypeString) {
    snapshot = str_materialize(js, snapshot);
    if (is_err(snapshot)) {
      GC_ROOT_RESTORE(js, root_mark);
      return snapshot;
    }

    ant_flat_string_t *snapshot_flat = ant_str_flat_ptr(snapshot);
    if (snapshot_flat && snapshot_flat->len > 0) {
      memcpy(flat_ptr->bytes, snapshot_flat->bytes, (size_t)snapshot_flat->len);
      cursor = (size_t)snapshot_flat->len;
    }
  }
  
  for (ant_builder_chunk_t *chunk = ptr->head; chunk; chunk = chunk->next) {
    ant_value_t chunk_value = chunk->value;
    if (str_is_heap_rope(chunk_value) || str_is_heap_builder(chunk_value)) {
    chunk_value = str_materialize(js, chunk_value);
    if (is_err(chunk_value)) {
      GC_ROOT_RESTORE(js, root_mark);
      return chunk_value;
    }}
    
    ant_flat_string_t *chunk_flat = ant_str_flat_ptr(chunk_value);
    if (!chunk_flat || chunk_flat->len == 0) continue;
    memcpy(flat_ptr->bytes + cursor, chunk_flat->bytes, (size_t)chunk_flat->len);
    cursor += (size_t)chunk_flat->len;
  }
  
  if (ptr->tail_len > 0) {
    memcpy(flat_ptr->bytes + cursor, ptr->tail, ptr->tail_len);
    cursor += ptr->tail_len;
  }
  
  flat_ptr->len = (ant_offset_t)cursor;
  str_flat_finish(flat_ptr, ptr->ascii_state);
  ptr->len = flat_ptr->len;
  
  if (vtype(cached) == kTypeNumber)
    str_flat_set_utf16_len(flat_ptr, (ant_offset_t)Ant_Value_AsNumberUnchecked(cached));
  
  ptr->snapshot = flat;
  ptr->head = NULL;
  ptr->chunk_tail = NULL;
  ptr->tail_len = 0;
  
  builder_set_cached_flat(builder, flat);
  GC_ROOT_RESTORE(js, root_mark);
  
  return flat;
}
