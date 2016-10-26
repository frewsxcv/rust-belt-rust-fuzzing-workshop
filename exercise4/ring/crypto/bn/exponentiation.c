/* Copyright (C) 1995-1998 Eric Young (eay@cryptsoft.com)
 * All rights reserved.
 *
 * This package is an SSL implementation written
 * by Eric Young (eay@cryptsoft.com).
 * The implementation was written so as to conform with Netscapes SSL.
 *
 * This library is free for commercial and non-commercial use as long as
 * the following conditions are aheared to.  The following conditions
 * apply to all code found in this distribution, be it the RC4, RSA,
 * lhash, DES, etc., code; not just the SSL code.  The SSL documentation
 * included with this distribution is covered by the same copyright terms
 * except that the holder is Tim Hudson (tjh@cryptsoft.com).
 *
 * Copyright remains Eric Young's, and as such any Copyright notices in
 * the code are not to be removed.
 * If this package is used in a product, Eric Young should be given attribution
 * as the author of the parts of the library used.
 * This can be in the form of a textual message at program startup or
 * in documentation (online or textual) provided with the package.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    "This product includes cryptographic software written by
 *     Eric Young (eay@cryptsoft.com)"
 *    The word 'cryptographic' can be left out if the rouines from the library
 *    being used are not cryptographic related :-).
 * 4. If you include any Windows specific code (or a derivative thereof) from
 *    the apps directory (application code) you must include an acknowledgement:
 *    "This product includes software written by Tim Hudson (tjh@cryptsoft.com)"
 *
 * THIS SOFTWARE IS PROVIDED BY ERIC YOUNG ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * The licence and distribution terms for any publically available version or
 * derivative of this code cannot be changed.  i.e. this code cannot simply be
 * copied and put under another distribution licence
 * [including the GNU Public Licence.]
 */
/* ====================================================================
 * Copyright (c) 1998-2005 The OpenSSL Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. All advertising materials mentioning features or use of this
 *    software must display the following acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit. (http://www.openssl.org/)"
 *
 * 4. The names "OpenSSL Toolkit" and "OpenSSL Project" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please contact
 *    openssl-core@openssl.org.
 *
 * 5. Products derived from this software may not be called "OpenSSL"
 *    nor may "OpenSSL" appear in their names without prior written
 *    permission of the OpenSSL Project.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit (http://www.openssl.org/)"
 *
 * THIS SOFTWARE IS PROVIDED BY THE OpenSSL PROJECT ``AS IS'' AND ANY
 * EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE OpenSSL PROJECT OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 * ====================================================================
 *
 * This product includes cryptographic software written by Eric Young
 * (eay@cryptsoft.com).  This product includes software written by Tim
 * Hudson (tjh@cryptsoft.com). */

#include <openssl/bn.h>

#include <assert.h>
#include <string.h>

#include <openssl/cpu.h>
#include <openssl/err.h>
#include <openssl/mem.h>

#include "internal.h"


#if defined(OPENSSL_X86_64)
#define OPENSSL_BN_ASM_MONT5
#define RSAZ_ENABLED

#include "rsaz_exp.h"

void GFp_bn_mul_mont_gather5(BN_ULONG *rp, const BN_ULONG *ap,
                             const void *table, const BN_ULONG *np,
                             const BN_ULONG *n0, int num, int power);
void GFp_bn_scatter5(const BN_ULONG *inp, size_t num, void *table,
                     size_t power);
void GFp_bn_gather5(BN_ULONG *out, size_t num, void *table, size_t power);
void GFp_bn_power5(BN_ULONG *rp, const BN_ULONG *ap, const void *table,
                   const BN_ULONG *np, const BN_ULONG *n0, int num, int power);
int GFp_bn_from_montgomery(BN_ULONG *rp, const BN_ULONG *ap,
                           const BN_ULONG *not_used, const BN_ULONG *np,
                           const BN_ULONG *n0, int num);
#endif

/* maximum precomputation table size for *variable* sliding windows */
#define TABLE_SIZE 32

/* GFp_BN_window_bits_for_exponent_size -- macro for sliding window mod_exp
 * functions
 *
 * For window size 'w' (w >= 2) and a random 'b' bits exponent, the number of
 * multiplications is a constant plus on average
 *
 *    2^(w-1) + (b-w)/(w+1);
 *
 * here 2^(w-1)  is for precomputing the table (we actually need entries only
 * for windows that have the lowest bit set), and (b-w)/(w+1)  is an
 * approximation for the expected number of w-bit windows, not counting the
 * first one.
 *
 * Thus we should use
 *
 *    w >= 6  if        b > 671
 *     w = 5  if  671 > b > 239
 *     w = 4  if  239 > b >  79
 *     w = 3  if   79 > b >  23
 *    w <= 2  if   23 > b
 *
 * (with draws in between).  Very small exponents are often selected
 * with low Hamming weight, so we use  w = 1  for b <= 23. */
#define GFp_BN_window_bits_for_exponent_size(b) \
		((b) > 671 ? 6 : \
		 (b) > 239 ? 5 : \
		 (b) >  79 ? 4 : \
		 (b) >  23 ? 3 : 1)

int GFp_BN_mod_exp_mont_vartime(BIGNUM *rr, const BIGNUM *a, const BIGNUM *p,
                                const BIGNUM *m, const BN_MONT_CTX *mont) {
  int j, bits, ret = 0, wstart, window;
  int start = 1;
  BIGNUM *val[TABLE_SIZE];
  size_t val_len = 0;
  BN_MONT_CTX *new_mont = NULL;

  if (!GFp_BN_is_odd(m)) {
    OPENSSL_PUT_ERROR(BN, BN_R_CALLED_WITH_EVEN_MODULUS);
    return 0;
  }

  /* XXX: This should be after the |BN_R_INPUT_NOT_REDUCED| check, but it isn't
   * in order to allow the |test_exp_mod_zero| test to keep working. Hopefully
   * we can simplify the users of this code so that it is clear that what
   * |test_exp_mod_zero| tests doesn't need to be supported. */
  bits = GFp_BN_num_bits(p);
  if (bits == 0) {
    /* x**0 mod 1 is still zero. */
    if (GFp_BN_is_one(m)) {
      GFp_BN_zero(rr);
      return 1;
    }
    return GFp_BN_one(rr);
  }

  if (a->neg || GFp_BN_ucmp(a, m) >= 0) {
    OPENSSL_PUT_ERROR(BN, BN_R_INPUT_NOT_REDUCED);
    return 0;
  }

  BIGNUM d;
  GFp_BN_init(&d);

  BIGNUM r;
  GFp_BN_init(&r);

  val[0] = GFp_BN_new();
  if (val[0] == NULL) {
    goto err;
  }
  ++val_len;

  /* Allocate a montgomery context if it was not supplied by the caller. */
  if (mont == NULL) {
    new_mont = GFp_BN_MONT_CTX_new();
    if (new_mont == NULL || !GFp_BN_MONT_CTX_set(new_mont, m)) {
      goto err;
    }
    mont = new_mont;
  }

  if (GFp_BN_is_zero(a)) {
    GFp_BN_zero(rr);
    ret = 1;
    goto err;
  }
  if (!GFp_BN_to_mont(val[0], a, mont)) {
    goto err; /* 1 */
  }

  window = GFp_BN_window_bits_for_exponent_size(bits);
  if (window > 1) {
    if (!GFp_BN_mod_mul_mont(&d, val[0], val[0], mont)) {
      goto err; /* 2 */
    }
    j = 1 << (window - 1);
    for (int i = 1; i < j; i++) {
      val[i] = GFp_BN_new();
      if (val[i] == NULL) {
        goto err;
      }
      ++val_len;
      if (!GFp_BN_mod_mul_mont(val[i], val[i - 1], &d, mont)) {
        goto err;
      }
    }
  }

  start = 1; /* This is used to avoid multiplication etc
              * when there is only the value '1' in the
              * buffer. */
  wstart = bits - 1; /* The top bit of the window */

  j = m->top; /* borrow j */
  if (m->d[j - 1] & (((BN_ULONG)1) << (BN_BITS2 - 1))) {
    if (GFp_bn_wexpand(&r, j) == NULL) {
      goto err;
    }
    /* 2^(top*BN_BITS2) - m */
    r.d[0] = (0 - m->d[0]) & BN_MASK2;
    for (int i = 1; i < j; i++) {
      r.d[i] = (~m->d[i]) & BN_MASK2;
    }
    r.top = j;
    /* Upper words will be zero if the corresponding words of 'm'
     * were 0xfff[...], so decrement r.top accordingly. */
    GFp_bn_correct_top(&r);
  } else if (!GFp_BN_to_mont(&r, GFp_BN_value_one(), mont)) {
    goto err;
  }

  for (;;) {
    int wvalue; /* The 'value' of the window */
    int wend; /* The bottom bit of the window */

    if (GFp_BN_is_bit_set(p, wstart) == 0) {
      if (!start && !GFp_BN_mod_mul_mont(&r, &r, &r, mont)) {
        goto err;
      }
      if (wstart == 0) {
        break;
      }
      wstart--;
      continue;
    }

    /* We now have wstart on a 'set' bit, we now need to work out how bit a
     * window to do.  To do this we need to scan forward until the last set bit
     * before the end of the window */
    wvalue = 1;
    wend = 0;
    for (int i = 1; i < window; i++) {
      if (wstart - i < 0) {
        break;
      }
      if (GFp_BN_is_bit_set(p, wstart - i)) {
        wvalue <<= (i - wend);
        wvalue |= 1;
        wend = i;
      }
    }

    /* wend is the size of the current window */
    j = wend + 1;
    /* add the 'bytes above' */
    if (!start) {
      for (int i = 0; i < j; i++) {
        if (!GFp_BN_mod_mul_mont(&r, &r, &r, mont)) {
          goto err;
        }
      }
    }

    /* wvalue will be an odd number < 2^window */
    if (!GFp_BN_mod_mul_mont(&r, &r, val[wvalue >> 1], mont)) {
      goto err;
    }

    /* move the 'window' down further */
    wstart -= wend + 1;
    start = 0;
    if (wstart < 0) {
      break;
    }
  }

  if (!GFp_BN_from_mont(rr, &r, mont)) {
    goto err;
  }
  ret = 1;

err:
  GFp_BN_MONT_CTX_free(new_mont);
  for (size_t i = 0; i < val_len; ++i) {
    GFp_BN_free(val[i]);
  }
  GFp_BN_free(&r);
  GFp_BN_free(&d);
  return ret;
}

/* GFp_BN_mod_exp_mont_consttime() stores the precomputed powers in a specific
 * layout so that accessing any of these table values shows the same access
 * pattern as far as cache lines are concerned. The following functions are
 * used to transfer a BIGNUM from/to that table. */
static int copy_to_prebuf(const BIGNUM *b, int top, unsigned char *buf, int idx,
                          int window) {
  int i, j;
  const int width = 1 << window;
  BN_ULONG *table = (BN_ULONG *) buf;

  if (top > b->top) {
    top = b->top; /* this works because 'buf' is explicitly zeroed */
  }

  for (i = 0, j = idx; i < top; i++, j += width)  {
    table[j] = b->d[i];
  }

  return 1;
}

static int copy_from_prebuf(BIGNUM *b, int top, unsigned char *buf, int idx,
                            int window) {
  int i, j;
  const int width = 1 << window;
  volatile BN_ULONG *table = (volatile BN_ULONG *)buf;

  if (GFp_bn_wexpand(b, top) == NULL) {
    return 0;
  }

  if (window <= 3) {
    for (i = 0; i < top; i++, table += width) {
      BN_ULONG acc = 0;

      for (j = 0; j < width; j++) {
        acc |= table[j] & ((BN_ULONG)0 - (constant_time_eq_int(j, idx) & 1));
      }

      b->d[i] = acc;
    }
  } else {
    int xstride = 1 << (window - 2);
    BN_ULONG y0, y1, y2, y3;

    i = idx >> (window - 2); /* equivalent of idx / xstride */
    idx &= xstride - 1;      /* equivalent of idx % xstride */

    y0 = (BN_ULONG)0 - (constant_time_eq_int(i, 0) & 1);
    y1 = (BN_ULONG)0 - (constant_time_eq_int(i, 1) & 1);
    y2 = (BN_ULONG)0 - (constant_time_eq_int(i, 2) & 1);
    y3 = (BN_ULONG)0 - (constant_time_eq_int(i, 3) & 1);

    for (i = 0; i < top; i++, table += width) {
      BN_ULONG acc = 0;

      for (j = 0; j < xstride; j++) {
        acc |= ((table[j + 0 * xstride] & y0) | (table[j + 1 * xstride] & y1) |
                (table[j + 2 * xstride] & y2) | (table[j + 3 * xstride] & y3)) &
               ((BN_ULONG)0 - (constant_time_eq_int(j, idx) & 1));
      }

      b->d[i] = acc;
    }
  }

  b->top = top;
  GFp_bn_correct_top(b);
  return 1;
}

/* GFp_BN_mod_exp_mont_consttime is based on the assumption that the L1 data cache
 * line width of the target processor is at least the following value. */
#define MOD_EXP_CTIME_MIN_CACHE_LINE_WIDTH (64)
#define MOD_EXP_CTIME_MIN_CACHE_LINE_MASK \
  (MOD_EXP_CTIME_MIN_CACHE_LINE_WIDTH - 1)

/* Window sizes optimized for fixed window size modular exponentiation
 * algorithm (GFp_BN_mod_exp_mont_consttime).
 *
 * To achieve the security goals of GFp_BN_mod_exp_mont_consttime, the maximum
 * size of the window must not exceed
 * log_2(MOD_EXP_CTIME_MIN_CACHE_LINE_WIDTH).
 *
 * Window size thresholds are defined for cache line sizes of 32 and 64, cache
 * line sizes where log_2(32)=5 and log_2(64)=6 respectively. A window size of
 * 7 should only be used on processors that have a 128 byte or greater cache
 * line size. */
#if MOD_EXP_CTIME_MIN_CACHE_LINE_WIDTH == 64

#define GFp_BN_window_bits_for_ctime_exponent_size(b) \
  ((b) > 937 ? 6 : (b) > 306 ? 5 : (b) > 89 ? 4 : (b) > 22 ? 3 : 1)
#define BN_MAX_WINDOW_BITS_FOR_CTIME_EXPONENT_SIZE (6)

#elif MOD_EXP_CTIME_MIN_CACHE_LINE_WIDTH == 32

#define GFp_BN_window_bits_for_ctime_exponent_size(b) \
  ((b) > 306 ? 5 : (b) > 89 ? 4 : (b) > 22 ? 3 : 1)
#define BN_MAX_WINDOW_BITS_FOR_CTIME_EXPONENT_SIZE (5)

#endif

/* Given a pointer value, compute the next address that is a cache line
 * multiple. */
#define MOD_EXP_CTIME_ALIGN(x_)          \
  ((unsigned char *)(x_) +               \
   (MOD_EXP_CTIME_MIN_CACHE_LINE_WIDTH - \
    (((uintptr_t)(x_)) & (MOD_EXP_CTIME_MIN_CACHE_LINE_MASK))))

/* This variant of GFp_BN_mod_exp_mont() uses fixed windows and the special
 * precomputation memory layout to limit data-dependency to a minimum
 * to protect secret exponents (cf. the hyper-threading timing attacks
 * pointed out by Colin Percival,
 * http://www.daemonology.net/hyperthreading-considered-harmful/)
 */
int GFp_BN_mod_exp_mont_consttime(BIGNUM *rr, const BIGNUM *a, const BIGNUM *p,
                              const BN_MONT_CTX *mont) {
  int i, bits, ret = 0, window, wvalue;
  int top;
  BN_MONT_CTX *new_mont = NULL;

  int numPowers;
  unsigned char *powerbufFree = NULL;
  int powerbufLen = 0;
  unsigned char *powerbuf = NULL;
  BIGNUM tmp, am;

  const BIGNUM *m = &mont->N;

  if (!GFp_BN_is_odd(m)) {
    OPENSSL_PUT_ERROR(BN, BN_R_CALLED_WITH_EVEN_MODULUS);
    return 0;
  }

  top = m->top;

  bits = GFp_BN_num_bits(p);
  if (bits == 0) {
    /* x**0 mod 1 is still zero. */
    if (GFp_BN_is_one(m)) {
      GFp_BN_zero(rr);
      return 1;
    }
    return GFp_BN_one(rr);
  }

#ifdef RSAZ_ENABLED
  /* If the size of the operands allow it, perform the optimized
   * RSAZ exponentiation. For further information see
   * crypto/bn/rsaz_exp.c and accompanying assembly modules. */
  if ((16 == a->top) && (16 == p->top) && (GFp_BN_num_bits(m) == 1024) &&
      GFp_rsaz_avx2_eligible()) {
    if (NULL == GFp_bn_wexpand(rr, 16)) {
      goto err;
    }
    GFp_RSAZ_1024_mod_exp_avx2(rr->d, a->d, p->d, m->d, mont->RR.d,
                               mont->n0[0]);
    rr->top = 16;
    rr->neg = 0;
    GFp_bn_correct_top(rr);
    ret = 1;
    goto err;
  }
#endif

  /* Get the window size to use with size of p. */
  window = GFp_BN_window_bits_for_ctime_exponent_size(bits);
#if defined(OPENSSL_BN_ASM_MONT5)
  if (window >= 5) {
    window = 5; /* ~5% improvement for RSA2048 sign, and even for RSA4096 */
    /* reserve space for mont->N.d[] copy */
    powerbufLen += top * sizeof(mont->N.d[0]);
  }
#endif

  /* Allocate a buffer large enough to hold all of the pre-computed
   * powers of am, am itself and tmp.
   */
  numPowers = 1 << window;
  powerbufLen +=
      sizeof(m->d[0]) *
      (top * numPowers + ((2 * top) > numPowers ? (2 * top) : numPowers));
#ifdef alloca
  if (powerbufLen < 3072) {
    powerbufFree = alloca(powerbufLen + MOD_EXP_CTIME_MIN_CACHE_LINE_WIDTH);
  } else
#endif
  {
    if ((powerbufFree = OPENSSL_malloc(
            powerbufLen + MOD_EXP_CTIME_MIN_CACHE_LINE_WIDTH)) == NULL) {
      goto err;
    }
  }

  powerbuf = MOD_EXP_CTIME_ALIGN(powerbufFree);
  memset(powerbuf, 0, powerbufLen);

#ifdef alloca
  if (powerbufLen < 3072) {
    powerbufFree = NULL;
  }
#endif

  /* lay down tmp and am right after powers table */
  tmp.d = (BN_ULONG *)(powerbuf + sizeof(m->d[0]) * top * numPowers);
  am.d = tmp.d + top;
  tmp.top = am.top = 0;
  tmp.dmax = am.dmax = top;
  tmp.neg = am.neg = 0;
  tmp.flags = am.flags = BN_FLG_STATIC_DATA;

/* prepare a^0 in Montgomery domain */
/* by Shay Gueron's suggestion */
  if (m->d[top - 1] & (((BN_ULONG)1) << (BN_BITS2 - 1))) {
    /* 2^(top*BN_BITS2) - m */
    tmp.d[0] = (0 - m->d[0]) & BN_MASK2;
    for (i = 1; i < top; i++) {
      tmp.d[i] = (~m->d[i]) & BN_MASK2;
    }
    tmp.top = top;
  } else if (!GFp_BN_to_mont(&tmp, GFp_BN_value_one(), mont)) {
    goto err;
  }

  /* prepare a^1 in Montgomery domain */
  if (a->neg || GFp_BN_ucmp(a, m) >= 0) {
    OPENSSL_PUT_ERROR(BN, BN_R_INPUT_NOT_REDUCED);
    goto err;
  } else if (!GFp_BN_to_mont(&am, a, mont)) {
    goto err;
  }

#if defined(OPENSSL_BN_ASM_MONT5)
  /* This optimization uses ideas from http://eprint.iacr.org/2011/239,
   * specifically optimization of cache-timing attack countermeasures
   * and pre-computation optimization. */

  /* Dedicated window==4 case improves 512-bit RSA sign by ~15%, but as
   * 512-bit RSA is hardly relevant, we omit it to spare size... */
  if (window == 5 && top > 1) {
    const BN_ULONG *n0 = mont->n0;
    BN_ULONG *np;

    /* BN_to_mont can contaminate words above .top
     * [in BN_DEBUG[_DEBUG] build]... */
    for (i = am.top; i < top; i++) {
      am.d[i] = 0;
    }
    for (i = tmp.top; i < top; i++) {
      tmp.d[i] = 0;
    }

    /* copy mont->N.d[] to improve cache locality */
    for (np = am.d + top, i = 0; i < top; i++) {
      np[i] = mont->N.d[i];
    }

    GFp_bn_scatter5(tmp.d, top, powerbuf, 0);
    GFp_bn_scatter5(am.d, am.top, powerbuf, 1);
    GFp_bn_mul_mont(tmp.d, am.d, am.d, np, n0, top);
    GFp_bn_scatter5(tmp.d, top, powerbuf, 2);

    /* same as above, but uses squaring for 1/2 of operations */
    for (i = 4; i < 32; i *= 2) {
      GFp_bn_mul_mont(tmp.d, tmp.d, tmp.d, np, n0, top);
      GFp_bn_scatter5(tmp.d, top, powerbuf, i);
    }
    for (i = 3; i < 8; i += 2) {
      int j;
      GFp_bn_mul_mont_gather5(tmp.d, am.d, powerbuf, np, n0, top, i - 1);
      GFp_bn_scatter5(tmp.d, top, powerbuf, i);
      for (j = 2 * i; j < 32; j *= 2) {
        GFp_bn_mul_mont(tmp.d, tmp.d, tmp.d, np, n0, top);
        GFp_bn_scatter5(tmp.d, top, powerbuf, j);
      }
    }
    for (; i < 16; i += 2) {
      GFp_bn_mul_mont_gather5(tmp.d, am.d, powerbuf, np, n0, top, i - 1);
      GFp_bn_scatter5(tmp.d, top, powerbuf, i);
      GFp_bn_mul_mont(tmp.d, tmp.d, tmp.d, np, n0, top);
      GFp_bn_scatter5(tmp.d, top, powerbuf, 2 * i);
    }
    for (; i < 32; i += 2) {
      GFp_bn_mul_mont_gather5(tmp.d, am.d, powerbuf, np, n0, top, i - 1);
      GFp_bn_scatter5(tmp.d, top, powerbuf, i);
    }

    bits--;
    for (wvalue = 0, i = bits % 5; i >= 0; i--, bits--) {
      wvalue = (wvalue << 1) + GFp_BN_is_bit_set(p, bits);
    }
    GFp_bn_gather5(tmp.d, top, powerbuf, wvalue);

    /* At this point |bits| is 4 mod 5 and at least -1. (|bits| is the first bit
     * that has not been read yet.) */
    assert(bits >= -1 && (bits == -1 || bits % 5 == 4));

    /* Scan the exponent one window at a time starting from the most
     * significant bits.
     */
    if (top & 7) {
      while (bits >= 0) {
        for (wvalue = 0, i = 0; i < 5; i++, bits--) {
          wvalue = (wvalue << 1) + GFp_BN_is_bit_set(p, bits);
        }

        GFp_bn_mul_mont(tmp.d, tmp.d, tmp.d, np, n0, top);
        GFp_bn_mul_mont(tmp.d, tmp.d, tmp.d, np, n0, top);
        GFp_bn_mul_mont(tmp.d, tmp.d, tmp.d, np, n0, top);
        GFp_bn_mul_mont(tmp.d, tmp.d, tmp.d, np, n0, top);
        GFp_bn_mul_mont(tmp.d, tmp.d, tmp.d, np, n0, top);
        GFp_bn_mul_mont_gather5(tmp.d, tmp.d, powerbuf, np, n0, top, wvalue);
      }
    } else {
      const uint8_t *p_bytes = (const uint8_t *)p->d;
      int max_bits = p->top * BN_BITS2;
      assert(bits < max_bits);
      /* |p = 0| has been handled as a special case, so |max_bits| is at least
       * one word. */
      assert(max_bits >= 64);

      /* If the first bit to be read lands in the last byte, unroll the first
       * iteration to avoid reading past the bounds of |p->d|. (After the first
       * iteration, we are guaranteed to be past the last byte.) Note |bits|
       * here is the top bit, inclusive. */
      if (bits - 4 >= max_bits - 8) {
        /* Read five bits from |bits-4| through |bits|, inclusive. */
        wvalue = p_bytes[p->top * BN_BYTES - 1];
        wvalue >>= (bits - 4) & 7;
        wvalue &= 0x1f;
        bits -= 5;
        GFp_bn_power5(tmp.d, tmp.d, powerbuf, np, n0, top, wvalue);
      }
      while (bits >= 0) {
        /* Read five bits from |bits-4| through |bits|, inclusive. */
        int first_bit = bits - 4;
        wvalue = *(const uint16_t *) (p_bytes + (first_bit >> 3));
        wvalue >>= first_bit & 7;
        wvalue &= 0x1f;
        bits -= 5;
        GFp_bn_power5(tmp.d, tmp.d, powerbuf, np, n0, top, wvalue);
      }
    }

    ret = GFp_bn_from_montgomery(tmp.d, tmp.d, NULL, np, n0, top);
    tmp.top = top;
    GFp_bn_correct_top(&tmp);
    if (ret) {
      if (!GFp_BN_copy(rr, &tmp)) {
        ret = 0;
      }
      goto err; /* non-zero ret means it's not error */
    }
  } else
#endif
  {
    if (!copy_to_prebuf(&tmp, top, powerbuf, 0, window) ||
        !copy_to_prebuf(&am, top, powerbuf, 1, window)) {
      goto err;
    }

    /* If the window size is greater than 1, then calculate
     * val[i=2..2^winsize-1]. Powers are computed as a*a^(i-1)
     * (even powers could instead be computed as (a^(i/2))^2
     * to use the slight performance advantage of sqr over mul).
     */
    if (window > 1) {
      if (!GFp_BN_mod_mul_mont(&tmp, &am, &am, mont) ||
          !copy_to_prebuf(&tmp, top, powerbuf, 2, window)) {
        goto err;
      }
      for (i = 3; i < numPowers; i++) {
        /* Calculate a^i = a^(i-1) * a */
        if (!GFp_BN_mod_mul_mont(&tmp, &am, &tmp, mont) ||
            !copy_to_prebuf(&tmp, top, powerbuf, i, window)) {
          goto err;
        }
      }
    }

    bits--;
    for (wvalue = 0, i = bits % window; i >= 0; i--, bits--) {
      wvalue = (wvalue << 1) + GFp_BN_is_bit_set(p, bits);
    }
    if (!copy_from_prebuf(&tmp, top, powerbuf, wvalue, window)) {
      goto err;
    }

    /* Scan the exponent one window at a time starting from the most
     * significant bits.
     */
    while (bits >= 0) {
      wvalue = 0; /* The 'value' of the window */

      /* Scan the window, squaring the result as we go */
      for (i = 0; i < window; i++, bits--) {
        if (!GFp_BN_mod_mul_mont(&tmp, &tmp, &tmp, mont)) {
          goto err;
        }
        wvalue = (wvalue << 1) + GFp_BN_is_bit_set(p, bits);
      }

      /* Fetch the appropriate pre-computed value from the pre-buf */
      if (!copy_from_prebuf(&am, top, powerbuf, wvalue, window)) {
        goto err;
      }

      /* Multiply the result into the intermediate result */
      if (!GFp_BN_mod_mul_mont(&tmp, &tmp, &am, mont)) {
        goto err;
      }
    }
  }

  /* Convert the final result from montgomery to standard format */
  if (!GFp_BN_from_mont(rr, &tmp, mont)) {
    goto err;
  }
  ret = 1;

err:
  GFp_BN_MONT_CTX_free(new_mont);
  OPENSSL_free(powerbufFree);
  return (ret);
}
