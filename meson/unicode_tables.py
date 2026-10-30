import re
import sys
from pathlib import Path

CP_COUNT = 0x110000
FLAG_CANON, FLAG_COMPAT, FLAG_COMP_FIRST, FLAG_COMP_SECOND = 1, 2, 4, 8
FLAG_NFC_UNSTABLE, FLAG_NFD_UNSTABLE, FLAG_NFKC_UNSTABLE, FLAG_NFKD_UNSTABLE = 16, 32, 64, 128
UNSTABLE = (FLAG_NFC_UNSTABLE, FLAG_NFD_UNSTABLE, FLAG_NFKC_UNSTABLE, FLAG_NFKD_UNSTABLE)
SBASE, LBASE, VBASE, TBASE = 0xAC00, 0x1100, 0x1161, 0x11A7
LCOUNT, VCOUNT, TCOUNT = 19, 21, 28
NCOUNT = VCOUNT * TCOUNT
SCOUNT = LCOUNT * NCOUNT
NONE = 0xFFFF


def c_array(text, name):
  m = re.search(r'\b' + name + r'\[\] = \{(.*?)\};', text, re.S)
  return m.group(1)


def number(token, enums):
  token = token.strip()
  if token in ('true', 'false'): return int(token == 'true')
  if token == 'UINT16_MAX': return NONE
  if token in enums: return enums[token]
  return int(token, 0)


class Utf8proc:
  def __init__(self, root):
    header = (root / 'utf8proc.h').read_text()
    data = (root / 'utf8proc_data.c').read_text()
    source = (root / 'utf8proc.c').read_text()
    self.version = re.search(r'utf8proc_unicode_version\(void\)\s*\{\s*return "([^"]+)"', source).group(1)
    enums = {k: int(v, 0) for k, v in re.findall(r'\b(UTF8PROC_[A-Z0-9_]+)\s*=\s*(0x[0-9A-Fa-f]+|\d+)', header)}
    ints = lambda name: [number(t, enums) for t in c_array(data, name).split(',') if t.strip()]
    self.sequences = ints('utf8proc_sequences')
    self.stage1 = ints('utf8proc_stage1table')
    self.stage2 = ints('utf8proc_stage2table')
    self.second = ints('utf8proc_combinations_second')
    self.combined = ints('utf8proc_combinations_combined')
    self.props = []
    for row in re.findall(r'\{([^{}]*)\}', c_array(data, 'utf8proc_properties')):
      f = [number(t, enums) for t in row.split(',')]
      self.props.append({
        'category': f[0], 'ccc': f[1], 'decomp_type': f[3], 'decomp': f[4], 'upper': f[6], 'lower': f[7],
        'comb_index': f[9], 'comb_length': f[10], 'comb_second': f[11], 'width': f[16],
      })

  def prop(self, cp):
    if cp < 0 or cp >= CP_COUNT: return self.props[0]
    return self.props[self.stage2[self.stage1[cp >> 8] + (cp & 0xFF)]]

  def entry(self, i):
    cp = self.sequences[i]
    if (cp & 0xF800) == 0xD800:
      return (((cp & 0x3FF) << 10) | (self.sequences[i + 1] & 0x3FF)) + 0x10000, i + 2
    return cp, i + 1

  def case(self, cp, key):
    index = self.prop(cp)[key]
    return cp if index == NONE else self.entry(index)[0]

  def decompose(self, cp, compat):
    if SBASE <= cp < SBASE + SCOUNT:
      s = cp - SBASE
      out = [LBASE + s // NCOUNT, VBASE + (s % NCOUNT) // TCOUNT]
      return out + [TBASE + s % TCOUNT] if s % TCOUNT else out
    p = self.prop(cp)
    if p['decomp'] == NONE or (p['decomp_type'] and not compat): return [cp]
    i, length = p['decomp'] & 0x3FFF, p['decomp'] >> 14
    if length >= 3:
      length, i = self.sequences[i], i + 1
    out = []
    for _ in range(length + 1):
      c, i = self.entry(i)
      out += self.decompose(c, compat)
    return out

  def normalize(self, cps, compose, compat):
    buf = [c for cp in cps for c in self.decompose(cp, compat)]
    pos = 0
    while pos < len(buf) - 1:
      a, b = self.prop(buf[pos])['ccc'], self.prop(buf[pos + 1])['ccc']
      if a > b > 0:
        buf[pos], buf[pos + 1] = buf[pos + 1], buf[pos]
        pos = pos - 1 if pos > 0 else pos + 1
      else:
        pos += 1
    if not compose: return buf
    out, starter, max_ccc = [], None, -1
    for cur in buf:
      p = self.prop(cur)
      if starter is not None and p['ccc'] > max_ccc:
        l, v = out[starter] - LBASE, cur - VBASE
        if 0 <= l < LCOUNT and 0 <= v < VCOUNT:
          out[starter] = SBASE + (l * VCOUNT + v) * TCOUNT
          continue
        s, t = out[starter] - SBASE, cur - TBASE
        if 0 <= s < SCOUNT and s % TCOUNT == 0 and 0 < t < TCOUNT:
          out[starter] += t
          continue
        sp = self.prop(out[starter])
        if sp['comb_index'] < 0x3FF and p['comb_second']:
          idx = sp['comb_index']
          pairs = range(idx, idx + sp['comb_length'])
          hit = next((k for k in pairs if self.second[k] == cur), None)
          if hit is not None:
            out[starter] = self.combined[hit]
            continue
      out.append(cur)
      if p['ccc']:
        max_ccc = max(max_ccc, p['ccc'])
      else:
        starter, max_ccc = len(out) - 1, -1
    return out


def dedupe(values, size):
  index, unique, ids = {}, [], []
  for start in range(0, len(values), size):
    block = tuple(values[start:start + size])
    if block not in index:
      index[block] = len(unique)
      unique.append(block)
    ids.append(index[block])
  return unique, ids


def emit(out, ctype, name, values):
  out.append(f'static const {ctype} {name}[{len(values)}] = {{')
  for i, v in enumerate(values):
    out.append(('\n  ' if i % 12 == 0 else ' ') + f'0x{v:x},')
  out.append('\n};\n\n')


def utf16(cps):
  out = []
  for c in cps:
    if c >= 0x10000:
      c -= 0x10000
      out += [0xD800 + (c >> 10), 0xDC00 + (c & 0x3FF)]
    else:
      out.append(c)
  return out


def main(root, path):
  u = Utf8proc(Path(root))
  records, record_ids, cp_record = [], {}, []
  canon, compat = [], []
  for cp in range(CP_COUNT):
    p = u.prop(cp)
    flags = (FLAG_COMP_FIRST if p['comb_index'] < 0x3FF else 0) | (FLAG_COMP_SECOND if p['comb_second'] else 0)
    surrogate = 0xD800 <= cp <= 0xDFFF
    if not surrogate and not (SBASE <= cp < SBASE + SCOUNT):
      full = u.decompose(cp, False)
      if full != [cp]:
        canon.append((cp, full))
        flags |= FLAG_CANON
      kfull = u.decompose(cp, True)
      if kfull != (full if flags & FLAG_CANON else [cp]):
        compat.append((cp, kfull))
        flags |= FLAG_COMPAT
    if not surrogate:
      jamo_vt = VBASE <= cp < VBASE + VCOUNT or TBASE < cp < TBASE + TCOUNT
      for f, (compose, k) in enumerate(((True, False), (False, False), (True, True), (False, True))):
        changes = u.normalize([cp], compose, k) != [cp]
        composes = compose and (p['comb_second'] or jamo_vt)
        if changes or composes or p['ccc']: flags |= UNSTABLE[f]
    record = (u.case(cp, 'upper') - cp, u.case(cp, 'lower') - cp, p['category'], p['width'], p['ccc'], flags)
    if record not in record_ids:
      record_ids[record] = len(records)
      records.append(record)
    cp_record.append(record_ids[record])

  best = None
  for a in range(3, 9):
    leaves, mid = dedupe(cp_record, 1 << a)
    leaf_bytes = len(leaves) * (1 << a) * (1 if len(records) < 256 else 2)
    for m in range(3, 11):
      if a + m > 20: break
      mids, top = dedupe(mid, 1 << m)
      size = leaf_bytes + len(mids) * (1 << m) * (1 if len(leaves) < 256 else 2) + len(top) * (1 if len(mids) < 256 else 2)
      if best is None or size < best[0]: best = (size, a, m)
  _, leaf_shift, mid_shift = best
  leaves, mid = dedupe(cp_record, 1 << leaf_shift)
  mids, top = dedupe(mid, 1 << mid_shift)

  pool, canon_off, compat_off = [], [], []
  for _, seq in canon:
    canon_off.append(len(pool)); pool += utf16(seq)
  canon_off.append(len(pool))
  for _, seq in compat:
    compat_off.append(len(pool)); pool += utf16(seq)
  compat_off.append(len(pool))

  firsts = [cp for cp in range(CP_COUNT) if u.prop(cp)['comb_index'] < 0x3FF]
  seconds = sorted({u.second[u.prop(cp)['comb_index'] + k] for cp in firsts for k in range(u.prop(cp)['comb_length'])})
  pairs = sorted(
    (cp << 8 | seconds.index(u.second[u.prop(cp)['comb_index'] + k]), u.combined[u.prop(cp)['comb_index'] + k])
    for cp in firsts for k in range(u.prop(cp)['comb_length'])
  )

  out = ['#ifndef ANT_UNICODE_DATA_H\n#define ANT_UNICODE_DATA_H\n\n', '#include <stdint.h>\n\n']
  out.append(f'#define UNICODE_DATA_VERSION "{u.version}"\n')
  out.append(f'#define UNICODE_LEAF_SHIFT {leaf_shift}\n#define UNICODE_MID_SHIFT {mid_shift}\n')
  out.append(f'#define UNICODE_FLAG_CANON {FLAG_CANON}\n#define UNICODE_FLAG_COMPAT {FLAG_COMPAT}\n')
  out.append(f'#define UNICODE_FLAG_COMP_FIRST {FLAG_COMP_FIRST}\n#define UNICODE_FLAG_COMP_SECOND {FLAG_COMP_SECOND}\n')
  out.append(f'#define UNICODE_FLAG_NFC_UNSTABLE {FLAG_NFC_UNSTABLE}\n#define UNICODE_FLAG_NFD_UNSTABLE {FLAG_NFD_UNSTABLE}\n')
  out.append(f'#define UNICODE_FLAG_NFKC_UNSTABLE {FLAG_NFKC_UNSTABLE}\n#define UNICODE_FLAG_NFKD_UNSTABLE {FLAG_NFKD_UNSTABLE}\n\n')
  out.append('typedef struct {\n  int32_t upper;\n  int32_t lower;\n  uint8_t category;\n  uint8_t width;\n  uint8_t ccc;\n  uint8_t flags;\n} unicode_record_t;\n\n')
  out.append(f'static const unicode_record_t unicode_records[{len(records)}] = {{')
  for r in records: out.append('\n  { %d, %d, %u, %u, %u, %u },' % r)
  out.append('\n};\n\n')
  emit(out, 'uint8_t' if len(mids) < 256 else 'uint16_t', 'unicode_top', top)
  emit(out, 'uint8_t' if len(leaves) < 256 else 'uint16_t', 'unicode_mid', [v for b in mids for v in b])
  emit(out, 'uint8_t' if len(records) < 256 else 'uint16_t', 'unicode_leaf', [v for b in leaves for v in b])
  emit(out, 'uint32_t', 'unicode_canon_cp', [cp for cp, _ in canon])
  emit(out, 'uint16_t', 'unicode_canon_off', canon_off)
  emit(out, 'uint32_t', 'unicode_compat_cp', [cp for cp, _ in compat])
  emit(out, 'uint16_t', 'unicode_compat_off', compat_off)
  emit(out, 'uint16_t', 'unicode_decomp_pool', pool)
  emit(out, 'uint32_t', 'unicode_comp_second', seconds)
  emit(out, 'uint32_t', 'unicode_comp_key', [k for k, _ in pairs])
  emit(out, 'uint32_t', 'unicode_comp_value', [v for _, v in pairs])
  out.append('#endif\n')
  Path(path).write_text(''.join(out))


if __name__ == '__main__':
  main(sys.argv[1], sys.argv[2])
