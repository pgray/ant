#ifndef ANT_GC_OBJECT_ARENAS_H
#define ANT_GC_OBJECT_ARENAS_H

#include "isolate.h"

static inline bool ant_object_is_wide(const ant_object_t *obj) {
  return obj->inobj_limit > ANT_INOBJ_BASE_SLOTS;
}

static inline ant_fixed_arena_t *ant_object_arena_for(ant_t *js, uint32_t inobj_limit) {
  if (inobj_limit <= ANT_INOBJ_BASE_SLOTS) return &js->obj_arena;
  return inobj_limit <= ANT_INOBJ_WIDE_SLOTS ? &js->obj_wide_arena : &js->obj_large_arena;
}

static inline ant_object_t *ant_object_cell_alloc(ant_t *js, uint32_t inobj_limit) {
  if (inobj_limit <= ANT_INOBJ_BASE_SLOTS) return fixed_arena_alloc_uninit(&js->obj_arena);
  ant_object_t *obj = fixed_arena_alloc_uninit(ant_object_arena_for(js, inobj_limit));
  if (obj) js->obj_arena.live_count++;
  return obj;
}

static inline void ant_object_cell_free(ant_t *js, ant_object_t *obj) {
  if (!ant_object_is_wide(obj)) {
    fixed_arena_free_elem(&js->obj_arena, obj);
    return;
  }

  fixed_arena_free_elem(ant_object_arena_for(js, obj->inobj_limit), obj);
  if (js->obj_arena.live_count > 0) js->obj_arena.live_count--;
}

static inline bool ant_object_holds_offset(const ant_t *js, uint64_t offset, const void *ptr) {
  const ant_fixed_arena_t *base = &js->obj_arena, *wide = &js->obj_wide_arena, *large = &js->obj_large_arena;
  uint32_t owner = atomic_load_explicit(&base->owners[offset >> ANT_ARENA_CHUNK_SHIFT], memory_order_relaxed);

  if (__builtin_expect(owner == base->id, 1) || owner == wide->id || owner == large->id) return true;
  if (owner == (base->id | ANT_ARENA_PARTIAL_CHUNK)) return !fixed_arena_past_cursor(base, ptr);
  if (owner == (wide->id | ANT_ARENA_PARTIAL_CHUNK)) return !fixed_arena_past_cursor(wide, ptr);

  return owner == (large->id | ANT_ARENA_PARTIAL_CHUNK) && !fixed_arena_past_cursor(large, ptr);
}

static inline size_t ant_object_live_bytes(const ant_t *js) {
  return
    js->obj_arena.live_count * ANT_OBJECT_SIZE +
    js->obj_wide_arena.live_count * (ANT_OBJECT_WIDE_SIZE - ANT_OBJECT_SIZE) +
    js->obj_large_arena.live_count * (ANT_OBJECT_LARGE_SIZE - ANT_OBJECT_SIZE);
}

static inline size_t ant_object_committed(const ant_t *js) {
  return js->obj_arena.committed + js->obj_wide_arena.committed + js->obj_large_arena.committed;
}

static inline bool ant_object_arena_contains(const ant_t *js, const void *ptr) {
  return
    fixed_arena_contains(&js->obj_arena, ptr) || fixed_arena_contains(&js->obj_wide_arena, ptr) ||
    fixed_arena_contains(&js->obj_large_arena, ptr);
}

#endif
