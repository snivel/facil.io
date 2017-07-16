/*
Copyright: Boaz segev, 2017
License: MIT

Feel free to copy, use and enjoy according to the license provided.
*/

#include "fiobj_types.h"

/* *****************************************************************************
Hashing (SipHash copy)
***************************************************************************** */

#if !defined(__BIG_ENDIAN__) && !defined(__LITTLE_ENDIAN__) &&                 \
    __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
/* the algorithm was designed as little endian... so, byte swap 64 bit. */
#define sip_local64(i)                                                         \
  (((i)&0xFFULL) << 56) | (((i)&0xFF00ULL) << 40) |                            \
      (((i)&0xFF0000ULL) << 24) | (((i)&0xFF000000ULL) << 8) |                 \
      (((i)&0xFF00000000ULL) >> 8) | (((i)&0xFF0000000000ULL) >> 24) |         \
      (((i)&0xFF000000000000ULL) >> 40) | (((i)&0xFF00000000000000ULL) >> 56)
#else
/* no need */
#define sip_local64(i) (i)
#endif

/* 64Bit left rotation, inlined. */
#define lrot64(i, bits)                                                        \
  (((uint64_t)(i) << (bits)) | ((uint64_t)(i) >> (64 - (bits))))

uint64_t fiobj_sym_hash(const void *data, size_t len) {
  /* initialize the 4 words */
  uint64_t v0 = (0x0706050403020100ULL ^ 0x736f6d6570736575ULL);
  uint64_t v1 = (0x0f0e0d0c0b0a0908ULL ^ 0x646f72616e646f6dULL);
  uint64_t v2 = (0x0706050403020100ULL ^ 0x6c7967656e657261ULL);
  uint64_t v3 = (0x0f0e0d0c0b0a0908ULL ^ 0x7465646279746573ULL);
  const uint64_t *w64 = data;
  uint8_t len_mod = len & 255;
  union {
    uint64_t i;
    uint8_t str[8];
  } word;

#define hash_map_SipRound                                                      \
  do {                                                                         \
    v2 += v3;                                                                  \
    v3 = lrot64(v3, 16) ^ v2;                                                  \
    v0 += v1;                                                                  \
    v1 = lrot64(v1, 13) ^ v0;                                                  \
    v0 = lrot64(v0, 32);                                                       \
    v2 += v1;                                                                  \
    v0 += v3;                                                                  \
    v1 = lrot64(v1, 17) ^ v2;                                                  \
    v3 = lrot64(v3, 21) ^ v0;                                                  \
    v2 = lrot64(v2, 32);                                                       \
  } while (0);

  while (len >= 8) {
    word.i = sip_local64(*w64);
    v3 ^= word.i;
    /* Sip Rounds */
    hash_map_SipRound;
    hash_map_SipRound;
    v0 ^= word.i;
    w64 += 1;
    len -= 8;
  }
  word.i = 0;
  uint8_t *pos = word.str;
  uint8_t *w8 = (void *)w64;
  switch (len) { /* fallthrough is intentional */
  case 7:
    pos[6] = w8[6];
  case 6:
    pos[5] = w8[5];
  case 5:
    pos[4] = w8[4];
  case 4:
    pos[3] = w8[3];
  case 3:
    pos[2] = w8[2];
  case 2:
    pos[1] = w8[1];
  case 1:
    pos[0] = w8[0];
  }
  word.str[7] = len_mod;

  /* last round */
  v3 ^= word.i;
  hash_map_SipRound;
  hash_map_SipRound;
  v0 ^= word.i;
  /* Finalization */
  v2 ^= 0xff;
  /* d iterations of SipRound */
  hash_map_SipRound;
  hash_map_SipRound;
  hash_map_SipRound;
  hash_map_SipRound;
  /* XOR it all together */
  v0 ^= v1 ^ v2 ^ v3;
#undef hash_map_SipRound
  return v0;
}

/* *****************************************************************************
Symbol API
***************************************************************************** */

/** Creates a Symbol object. Use `fiobj_free`. */
fiobj_s *fiobj_sym_new(const char *str, size_t len) {
  fiobj_s *sym = fiobj_alloc(FIOBJ_T_SYMBOL, len, (void *)str);
  ((fio_sym_s *)(sym))->hash = (uintptr_t)fiobj_sym_hash(str, len);
  return sym;
}

/** Creates a Symbol object using a printf like interface. */
__attribute__((format(printf, 1, 0))) fiobj_s *
fiobj_symvprintf(const char *restrict format, va_list argv) {
  fiobj_s *sym = NULL;
  va_list argv_cpy;
  va_copy(argv_cpy, argv);
  int len = vsnprintf(NULL, 0, format, argv_cpy);
  va_end(argv_cpy);
  if (len == 0) {
    sym = fiobj_alloc(FIOBJ_T_SYMBOL, 0, (void *)"");
    ((fio_sym_s *)(sym))->hash = fiobj_sym_hash(NULL, 0);
  }
  if (len <= 0)
    return sym;
  sym = fiobj_alloc(FIOBJ_T_SYMBOL, len, NULL); /* adds 1 to len, for NUL */
  vsnprintf(((fio_sym_s *)(sym))->str, len + 1, format, argv);
  ((fio_sym_s *)(sym))->hash =
      (uintptr_t)fiobj_sym_hash(((fio_sym_s *)(sym))->str, len);
  return sym;
}
__attribute__((format(printf, 1, 2))) fiobj_s *
fiobj_symprintf(const char *restrict format, ...) {
  va_list argv;
  va_start(argv, format);
  fiobj_s *sym = fiobj_symvprintf(format, argv);
  va_end(argv);
  return sym;
}

/** Returns 1 if both Symbols are equal and 0 if not. */
int fiobj_sym_iseql(fiobj_s *sym1, fiobj_s *sym2) {
  if (sym1->type != FIOBJ_T_SYMBOL || sym2->type != FIOBJ_T_SYMBOL)
    return 0;
  return (((fio_sym_s *)sym1)->hash == ((fio_sym_s *)sym2)->hash);
}

/**
 * Returns a symbol's identifier.
 *
 * The unique identifier is calculated using SipHash and is equal for all Symbol
 * objects that were created using the same data.
 */
uintptr_t fiobj_sym_id(fiobj_s *sym) {
  if (sym->type != FIOBJ_T_SYMBOL)
    return 0;
  return obj2sym(sym)->hash;
}
