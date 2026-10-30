#ifdef ISOLATE_ARENA
ISOLATE_ARENA(obj_arena,       ANT_OBJECT_SIZE,             offsetof(ant_object_t, mark_epoch), 1)
ISOLATE_ARENA(obj_wide_arena,  ANT_OBJECT_WIDE_SIZE,        offsetof(ant_object_t, mark_epoch), 4)
ISOLATE_ARENA(obj_large_arena, ANT_OBJECT_LARGE_SIZE,       offsetof(ant_object_t, mark_epoch), 8)
ISOLATE_ARENA(closure_arena,   sizeof(sv_closure_t),        offsetof(sv_closure_t, gc_epoch),   ANT_ARENA_MAX / ANT_CLOSURE_ARENA_MAX)
ISOLATE_ARENA(upvalue_arena,   sizeof(sv_upvalue_t),        offsetof(sv_upvalue_t, gc_epoch),   ANT_ARENA_MAX / ANT_UPVALUE_ARENA_MAX)
ISOLATE_ARENA(promise_arena,   sizeof(ant_promise_state_t), 0,                                  ANT_ARENA_MAX / ANT_PROMISE_ARENA_MAX)
#undef ISOLATE_ARENA
#endif
