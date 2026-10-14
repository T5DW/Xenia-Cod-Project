/* Decompresses one XMem (Xbox 360 LZX) stream for t5menu's BO2 fastfile reader.
 * Built as xmem.dll (Windows) / libxmem.so (Linux) and loaded with ctypes.
 * Uses libmspack's LZX decoder (lzxd.c, LGPL 2.1, see COPYING.LIB). */
#include <system.h>
#include <lzx.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif

struct mem { struct mspack_file base; unsigned char *buf; int len, pos; };

static int m_read(struct mspack_file *f, void *b, int n) {
  struct mem *m = (struct mem *)f;
  int r = m->len - m->pos;
  if (r > n) r = n;
  if (r < 0) r = 0;
  memcpy(b, m->buf + m->pos, r);
  m->pos += r;
  return r;
}
static int m_write(struct mspack_file *f, void *b, int n) {
  struct mem *m = (struct mem *)f;
  if (m->pos + n > m->len) n = m->len - m->pos;
  memcpy(m->buf + m->pos, b, n);
  m->pos += n;
  return n;
}
static void *m_alloc(struct mspack_system *s, size_t n) { (void)s; return malloc(n); }
static void m_free(void *p) { free(p); }
static void m_copy(void *s, void *d, size_t n) { memmove(d, s, n); }
static void m_msg(struct mspack_file *f, const char *fmt, ...) { (void)f; (void)fmt; }
static struct mspack_system sys = {NULL, NULL, m_read, m_write, NULL, NULL, m_msg, m_alloc, m_free, m_copy, NULL};

/* in: one XMem stream (frames of [BE16 size] or [FF, BE16 frame size, BE16 size]).
 * Returns the number of bytes written to out, or a negative error. */
EXPORT int xmem_decompress(const unsigned char *in, int inlen, unsigned char *out, int outcap, int wbits) {
  unsigned char *raw = (unsigned char *)malloc(inlen > 0 ? inlen : 1);
  int rl = 0, total = 0, p = 0, err;
  struct mem mi, mo;
  struct lzxd_stream *s;
  if (!raw) return -1;
  while (p < inlen) {
    int fs, bs;
    if (in[p] == 0xFF) {
      if (p + 5 > inlen) break;
      fs = (in[p + 1] << 8) | in[p + 2];
      bs = (in[p + 3] << 8) | in[p + 4];
      p += 5;
    } else {
      if (p + 2 > inlen) break;
      fs = 0x8000;
      bs = (in[p] << 8) | in[p + 1];
      p += 2;
    }
    if (bs == 0 || fs == 0) break;
    if (p + bs > inlen) bs = inlen - p;
    memcpy(raw + rl, in + p, bs);
    rl += bs;
    p += bs;
    total += fs;
  }
  if (total > outcap) total = outcap;
  memset(&mi, 0, sizeof mi);
  memset(&mo, 0, sizeof mo);
  mi.buf = raw; mi.len = rl;
  mo.buf = out; mo.len = outcap;
  s = lzxd_init(&sys, (struct mspack_file *)&mi, (struct mspack_file *)&mo, wbits, 0, 0x8000, total, 0);
  err = s ? lzxd_decompress(s, total) : -2;
  if (s) lzxd_free(s);
  free(raw);
  return err ? -100 - err : mo.pos;
}
