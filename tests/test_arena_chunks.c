#include "arena.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static ant_fixed_arena_t first;
static ant_fixed_arena_t second;
static ant_fixed_arena_t capped;

static constexpr size_t ELEM = 168;

static bool marks(const ant_fixed_arena_t *a, void *p) {
  return fixed_arena_holds_offset(a, (uint64_t)((uintptr_t)p - ant_cage_base_address), p);
}

static void check_slot(const ant_fixed_arena_t *a, void *p) {
  assert(fixed_arena_contains(a, p));
  assert(fixed_arena_owns(a, p));
  uint32_t slot = fixed_arena_slot_index(a, p);
  assert(slot != UINT32_MAX);
  assert(fixed_arena_slot_at(a, slot) == p);
  assert(!fixed_arena_contains(a, (uint8_t *)p + 8));
}

static void test_many_chunks(void) {
  assert(fixed_arena_init(&first, ELEM, 0, 64ULL * 1024 * 1024));
  assert(fixed_arena_init(&second, ELEM, 0, 64ULL * 1024 * 1024));
  assert(first.chunk_count == 1 && second.chunk_count == 1);

  size_t per_chunk = fixed_arena_slots_per_chunk(&first);
  size_t count = per_chunk * 3 + 17;
  void **items = calloc(count, sizeof(*items));
  void **others = calloc(count, sizeof(*others));
  assert(items && others);

  for (size_t i = 0; i < count; i++) {
    items[i] = fixed_arena_alloc(&first);
    others[i] = fixed_arena_alloc(&second);
    assert(items[i] && others[i]);
    memset(items[i], 0xa5, ELEM);
    memset(others[i], 0x5a, ELEM);
  }
  assert(first.chunk_count == 4 && first.live_count == count);
  assert(fixed_arena_extent(&first) == 3 * first.chunk_bytes + 17 * ELEM);

  for (size_t i = 0; i < count; i++) {
    check_slot(&first, items[i]);
    check_slot(&second, others[i]);
    assert(!fixed_arena_owns(&second, items[i]) && !fixed_arena_contains(&second, items[i]));
    assert(!fixed_arena_owns(&first, others[i]) && !fixed_arena_contains(&first, others[i]));
    assert(((uint8_t *)items[i])[ELEM - 1] == 0xa5);
  }

  void *tail = first.cursor;
  assert(!fixed_arena_contains(&first, tail));
  assert(fixed_arena_slot_at(&first, (uint32_t)count) == NULL);

  int local = 0;
  assert(!fixed_arena_contains(&first, NULL));
  assert(!fixed_arena_contains(&first, (void *)UINTPTR_MAX));
  assert(!fixed_arena_contains(&first, &local));
  assert(!fixed_arena_owns(&first, &local));

  size_t committed = first.committed;
  void *released = items[count - 1];
  for (size_t i = per_chunk; i < count; i++) fixed_arena_free_elem(&first, items[i]);
  first.free_list = NULL;
  fixed_arena_trim(&first, 0, first.chunk_bytes);
  assert(first.chunk_count == 1);
  assert(first.committed < committed);
  assert(!fixed_arena_owns(&first, released) && !fixed_arena_contains(&first, released));
  for (size_t i = 0; i < per_chunk; i++) check_slot(&first, items[i]);

  fixed_arena_trim(&first, 0, 5 * ELEM);
  assert(fixed_arena_contains(&first, items[4]) && !fixed_arena_contains(&first, items[5]));
  assert(fixed_arena_owns(&first, items[per_chunk - 1]) && fixed_arena_past_cursor(&first, items[per_chunk - 1]));
  assert(fixed_arena_past_cursor(&first, items[5]) && !fixed_arena_past_cursor(&first, items[4]));
  assert(marks(&first, items[4]) && !marks(&first, items[5]) && !marks(&first, items[per_chunk - 1]));
  assert(!marks(&first, released));
  assert(fixed_arena_extent(&first) == 5 * ELEM);

  for (size_t i = 0; i < per_chunk * 2; i++) {
    void *p = fixed_arena_alloc(&first);
    assert(p);
    memset(p, 0x3c, ELEM);
    check_slot(&first, p);
  }
  assert(first.chunk_count == 3);
  assert(marks(&first, items[per_chunk - 1]) && marks(&first, items[0]));

  void *kept = others[0];
  fixed_arena_destroy(&second);
  assert(!fixed_arena_owns(&second, kept) && !fixed_arena_contains(&second, kept));
  fixed_arena_destroy(&first);
  free(items);
  free(others);
}

static void test_limit_and_exhaustion(void) {
  size_t limit = ANT_ARENA_CHUNK_SIZE * 2 + 3 * ant_arena_page_size();
  assert(fixed_arena_init(&capped, ELEM, 0, limit));

  size_t allocated = 0;
  void *last = NULL;
  while ((last = fixed_arena_alloc(&capped))) {
    memset(last, 1, ELEM);
    allocated++;
    last = NULL;
  }
  assert(capped.committed <= capped.limit);
  assert(allocated * ELEM <= limit);
  assert(capped.committed - allocated * ELEM < 3 * ELEM);
  assert(capped.chunk_count == 3);

  void *again = fixed_arena_slot_at(&capped, 0);
  assert(again);
  fixed_arena_free_elem(&capped, again);
  assert(fixed_arena_alloc(&capped) == again);
  assert(!fixed_arena_alloc(&capped));
  fixed_arena_destroy(&capped);

  assert(fixed_arena_init(&capped, ELEM, 0, 64ULL * 1024 * 1024));
  for (int i = 0; i < 100; i++) assert(fixed_arena_alloc(&capped));
  ant_fixed_arena_t saved = capped;
  capped.limit = capped.committed;
  capped.free_list = NULL;
  while (fixed_arena_alloc_uninit(&capped));
  assert(!fixed_arena_alloc_uninit(&capped));
  capped = saved;
  assert(fixed_arena_alloc(&capped));
  fixed_arena_destroy(&capped);
}

int main(void) {
  test_many_chunks();
  test_limit_and_exhaustion();
  puts("arena chunks ok");
  return 0;
}
