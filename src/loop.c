#include <stdatomic.h>
#include <stdlib.h>

#include "internal.h"
#include "loop.h"

static atomic_flag default_loop_claimed = ATOMIC_FLAG_INIT;

bool Ant_Loop_Init(ant_t *js) {
  if (!atomic_flag_test_and_set(&default_loop_claimed)) js->loop = uv_default_loop();
  else {
    uv_loop_t *loop = malloc(sizeof(*loop));
    if (!loop || uv_loop_init(loop) != 0) {
      free(loop);
      return false;
    }
    js->loop = loop;
    js->owns_loop = true;
  }
  
  js->loop->data = js;
  return true;
}

static void loop_close_handle(uv_handle_t *handle, void *arg) {
  if (!uv_is_closing(handle)) uv_close(handle, NULL);
}

void Ant_Loop_Destroy(ant_t *js) {
  uv_loop_t *loop = js->loop;
  if (!loop) return;
  
  loop->data = NULL;
  js->loop = NULL;
  
  if (!js->owns_loop) {
    atomic_flag_clear(&default_loop_claimed);
    return;
  }
  
  uv_walk(loop, loop_close_handle, NULL);
  while (uv_loop_close(loop) == UV_EBUSY) uv_run(loop, UV_RUN_NOWAIT);
  free(loop);
  js->owns_loop = false;
}
