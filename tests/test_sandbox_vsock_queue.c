#include "../src/sandbox/backends/darwin/backend.h"

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

#if defined(__aarch64__) && defined(__APPLE__)

#define PRODUCER_COUNT 4
#define FRAMES_PER_PRODUCER 1000

typedef struct {
  ant_hvf_vm_t *vm;
  uint32_t producer;
} producer_ctx_t;

static void *producer_main(void *opaque) {
  producer_ctx_t *ctx = opaque;
  for (uint32_t i = 0; i < FRAMES_PER_PRODUCER; i++) {
    uint32_t payload[2] = { ctx->producer, i };
    assert(ant_hvf_vsock_queue_frame(ctx->vm, payload, sizeof(payload)) == 0);
  }
  return NULL;
}

static void put_desc(unsigned char *mem, uint16_t index, uint64_t addr, uint32_t len, uint16_t flags, uint16_t next) {
  unsigned char *raw = mem + (size_t)index * 16u;
  ant_hvf_store64(raw, addr);
  ant_hvf_store32(raw + 8, len);
  ant_hvf_store16(raw + 12, flags);
  ant_hvf_store16(raw + 14, next);
}

static void test_tx_packet_bounds(void) {
  enum { mem_size = 64 * 1024 };
  ant_hvf_vm_t vm;
  memset(&vm, 0, sizeof(vm));
  unsigned char *mem = calloc(1, mem_size);
  assert(mem);
  vm.host_mem = mem;
  vm.mem_size = mem_size;

  const uint64_t desc_base = ANT_HVF_GUEST_BASE;
  const uint64_t hdr_addr = ANT_HVF_GUEST_BASE + 0x1000u;
  const uint64_t body_addr = ANT_HVF_GUEST_BASE + 0x2000u;
  ant_virtio_vsock_hdr_t hdr = { .len = 4, .op = ANT_VIRTIO_VSOCK_OP_RW };
  ant_hvf_vsock_store_hdr(mem + 0x1000u, &hdr);
  memcpy(mem + 0x2000u, "ping", 4);

  ant_virtio_vsock_hdr_t out;
  unsigned char *payload = NULL;
  uint32_t used_len = 0;

  put_desc(mem, 0, hdr_addr, sizeof(ant_virtio_vsock_hdr_t), ANT_VRING_DESC_F_NEXT, 1);
  put_desc(mem, 1, body_addr, 4, 0, 0);
  assert(ant_hvf_vsock_read_tx_packet(&vm, desc_base, 0, &out, &payload, &used_len) == 0);
  assert(out.len == 4);
  assert(used_len == sizeof(ant_virtio_vsock_hdr_t) + 4u);
  assert(payload && memcmp(payload, "ping", 4) == 0);
  free(payload);
  payload = NULL;

  put_desc(mem, 1, body_addr, UINT32_MAX, 0, 0);
  assert(ant_hvf_vsock_read_tx_packet(&vm, desc_base, 0, &out, &payload, &used_len) == -E2BIG);
  assert(!payload);

  put_desc(mem, 1, body_addr, ANT_HVF_VSOCK_BUF_ALLOC + 1u, 0, 0);
  assert(ant_hvf_vsock_read_tx_packet(&vm, desc_base, 0, &out, &payload, &used_len) == -E2BIG);
  assert(!payload);

  free(mem);
}

int main(void) {
  test_tx_packet_bounds();

  ant_hvf_vm_t vm;
  memset(&vm, 0, sizeof(vm));
  assert(pthread_mutex_init(&vm.vsock_lock, NULL) == 0);
  vm.vsock_lock_init = true;

  pthread_t threads[PRODUCER_COUNT];
  producer_ctx_t contexts[PRODUCER_COUNT];
  for (uint32_t i = 0; i < PRODUCER_COUNT; i++) {
    contexts[i] = (producer_ctx_t){ .vm = &vm, .producer = i };
    assert(pthread_create(&threads[i], NULL, producer_main, &contexts[i]) == 0);
  }
  for (uint32_t i = 0; i < PRODUCER_COUNT; i++)
    assert(pthread_join(threads[i], NULL) == 0);

  size_t count = 0;
  for (ant_vsock_outgoing_frame_t *frame = vm.vsock.outgoing_head; frame; frame = frame->next) {
    assert(frame->len == sizeof(uint32_t) * 2);
    count++;
  }
  assert(count == PRODUCER_COUNT * FRAMES_PER_PRODUCER);
  assert(atomic_load_explicit(&vm.vsock_wake_pending, memory_order_acquire));

  ant_hvf_vsock_clear_frames(&vm);
  pthread_mutex_destroy(&vm.vsock_lock);
  return 0;
}

#else

int main(void) {
  return 0;
}

#endif
