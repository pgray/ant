#ifndef STRINGS_H
#define STRINGS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static inline uint8_t unhex(uint8_t c) {
  return (c & 0xF) + (c >> 6) * 9;
}

static inline bool is_octal_escape(const uint8_t *in, size_t pos) {
  uint8_t c = in[pos + 1];
  return c >= '0' && c <= '7' && !(c == '0' && !(in[pos + 2] >= '0' && in[pos + 2] <= '7'));
}

typedef enum: uint8_t {
  ESCAPE_LEGACY = 1 << 0,
  ESCAPE_BAD_HEX = 1 << 1,
  ESCAPE_BAD_UNICODE = 1 << 2,
  ESCAPE_BAD_CODEPOINT = 1 << 3,
  ESCAPE_MALFORMED = ESCAPE_BAD_HEX | ESCAPE_BAD_UNICODE | ESCAPE_BAD_CODEPOINT,
} escape_flags_t;

size_t decode_escape(
  const uint8_t *in, size_t pos, size_t end,
  uint8_t *out, size_t *out_pos, uint8_t quote, uint8_t *flags
);

#endif
