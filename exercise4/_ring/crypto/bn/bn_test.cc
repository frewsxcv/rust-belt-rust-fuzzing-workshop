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
 * Copyright 2002 Sun Microsystems, Inc. ALL RIGHTS RESERVED.
 *
 * Portions of the attached software ("Contribution") are developed by
 * SUN MICROSYSTEMS, INC., and are contributed to the OpenSSL project.
 *
 * The Contribution is licensed pursuant to the Eric Young open source
 * license provided above.
 *
 * The binary polynomial arithmetic software is originally written by
 * Sheueling Chang Shantz and Douglas Stebila of Sun Microsystems
 * Laboratories. */

/* For BIGNUM format macros. */
#if !defined(__STDC_FORMAT_MACROS)
#define __STDC_FORMAT_MACROS
#endif

// rustc always links with the non-debug runtime, but when _DEBUG is defined
// MSVC's C++ standard library expects to be linked to the debug runtime.
#if defined(_DEBUG)
#undef _DEBUG
#endif

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include <utility>

#include <openssl/bn.h>
#include <openssl/err.h>
#include <openssl/mem.h>

#include "../test/bn_test_lib.h"
#include "../crypto/test/file_test.h"
#include "../crypto/test/scoped_types.h"
#include "../test/bn_test_util.h"


/* Prototypes to avoid -Wmissing-prototypes warnings. */
extern "C" int bssl_bn_test_main(RAND *rng);


static int HexToBIGNUM(ScopedBIGNUM *out, const char *in) {
  BIGNUM *raw = NULL;
  int ret = BN_hex2bn(&raw, in);
  out->reset(raw);
  return ret;
}

static ScopedBIGNUM GetBIGNUM(FileTest *t, const char *attribute) {
  std::string hex;
  if (!t->GetAttribute(&hex, attribute)) {
    return nullptr;
  }

  ScopedBIGNUM ret;
  if (HexToBIGNUM(&ret, hex.c_str()) != static_cast<int>(hex.size())) {
    t->PrintLine("Could not decode '", hex.c_str(), "'.");
    return nullptr;
  }
  return ret;
}

static bool GetInt(FileTest *t, int *out, const char *attribute) {
  ScopedBIGNUM ret = GetBIGNUM(t, attribute);
  if (!ret) {
    return false;
  }

  // This is |BN_get_word|, inlined and improved.
  switch (ret->top) {
    case 0:
      *out = 0;
      return 1;
    case 1:
      if (ret->d[0] > (BN_ULONG)INT_MAX) {
        return false;
      }
      *out = static_cast<int>(ret->d[0]);
      return true;
    default:
      return false;
  }
}

static bool ExpectBIGNUMsEqual(FileTest *t, const char *operation,
                               const BIGNUM *expected, const BIGNUM *actual) {
  if (GFp_BN_cmp(expected, actual) == 0) {
    return true;
  }
  t->PrintLine("Got wrong value for ", operation);
  return false;
}

static bool TestSum(FileTest *t) {
  ScopedBIGNUM a = GetBIGNUM(t, "A");
  ScopedBIGNUM b = GetBIGNUM(t, "B");
  ScopedBIGNUM sum = GetBIGNUM(t, "Sum");
  if (!a || !b || !sum) {
    return false;
  }

  ScopedBIGNUM ret(GFp_BN_new());
  if (!ret ||
      !GFp_BN_add(ret.get(), a.get(), b.get()) ||
      !ExpectBIGNUMsEqual(t, "A + B", sum.get(), ret.get()) ||
      !GFp_BN_sub(ret.get(), sum.get(), a.get()) ||
      !ExpectBIGNUMsEqual(t, "Sum - A", b.get(), ret.get()) ||
      !GFp_BN_sub(ret.get(), sum.get(), b.get()) ||
      !ExpectBIGNUMsEqual(t, "Sum - B", a.get(), ret.get())) {
    return false;
  }

  // Test that the functions work when |r| and |a| point to the same |BIGNUM|,
  // or when |r| and |b| point to the same |BIGNUM|. TODO: Test the case where
  // all of |r|, |a|, and |b| point to the same |BIGNUM|.
  if (!GFp_BN_copy(ret.get(), a.get()) ||
      !GFp_BN_add(ret.get(), ret.get(), b.get()) ||
      !ExpectBIGNUMsEqual(t, "A + B (r is a)", sum.get(), ret.get()) ||
      !GFp_BN_copy(ret.get(), b.get()) ||
      !GFp_BN_add(ret.get(), a.get(), ret.get()) ||
      !ExpectBIGNUMsEqual(t, "A + B (r is b)", sum.get(), ret.get()) ||
      !GFp_BN_copy(ret.get(), sum.get()) ||
      !GFp_BN_sub(ret.get(), ret.get(), a.get()) ||
      !ExpectBIGNUMsEqual(t, "Sum - A (r is a)", b.get(), ret.get()) ||
      !GFp_BN_copy(ret.get(), a.get()) ||
      !GFp_BN_sub(ret.get(), sum.get(), ret.get()) ||
      !ExpectBIGNUMsEqual(t, "Sum - A (r is b)", b.get(), ret.get()) ||
      !GFp_BN_copy(ret.get(), sum.get()) ||
      !GFp_BN_sub(ret.get(), ret.get(), b.get()) ||
      !ExpectBIGNUMsEqual(t, "Sum - B (r is a)", a.get(), ret.get()) ||
      !GFp_BN_copy(ret.get(), b.get()) ||
      !GFp_BN_sub(ret.get(), sum.get(), ret.get()) ||
      !ExpectBIGNUMsEqual(t, "Sum - B (r is b)", a.get(), ret.get())) {
    return false;
  }

  // Test |GFp_BN_uadd| and |GFp_BN_usub| with the prerequisites they are
  // documented as having. Note that these functions are frequently used when
  // the prerequisites don't hold. In those cases, they are supposed to work as
  // if the prerequisite hold, but we don't test that yet. TODO: test that.
  if (!GFp_BN_is_negative(a.get()) &&
      !GFp_BN_is_negative(b.get()) && GFp_BN_cmp(a.get(), b.get()) >= 0) {
    if (!GFp_BN_uadd(ret.get(), a.get(), b.get()) ||
        !ExpectBIGNUMsEqual(t, "A +u B", sum.get(), ret.get()) ||
        !GFp_BN_usub(ret.get(), sum.get(), a.get()) ||
        !ExpectBIGNUMsEqual(t, "Sum -u A", b.get(), ret.get()) ||
        !GFp_BN_usub(ret.get(), sum.get(), b.get()) ||
        !ExpectBIGNUMsEqual(t, "Sum -u B", a.get(), ret.get())) {
      return false;
    }

    // Test that the functions work when |r| and |a| point to the same |BIGNUM|,
    // or when |r| and |b| point to the same |BIGNUM|. TODO: Test the case where
    // all of |r|, |a|, and |b| point to the same |BIGNUM|.
    if (!GFp_BN_copy(ret.get(), a.get()) ||
        !GFp_BN_uadd(ret.get(), ret.get(), b.get()) ||
        !ExpectBIGNUMsEqual(t, "A +u B (r is a)", sum.get(), ret.get()) ||
        !GFp_BN_copy(ret.get(), b.get()) ||
        !GFp_BN_uadd(ret.get(), a.get(), ret.get()) ||
        !ExpectBIGNUMsEqual(t, "A +u B (r is b)", sum.get(), ret.get()) ||
        !GFp_BN_copy(ret.get(), sum.get()) ||
        !GFp_BN_usub(ret.get(), ret.get(), a.get()) ||
        !ExpectBIGNUMsEqual(t, "Sum -u A (r is a)", b.get(), ret.get()) ||
        !GFp_BN_copy(ret.get(), a.get()) ||
        !GFp_BN_usub(ret.get(), sum.get(), ret.get()) ||
        !ExpectBIGNUMsEqual(t, "Sum -u A (r is b)", b.get(), ret.get()) ||
        !GFp_BN_copy(ret.get(), sum.get()) ||
        !GFp_BN_usub(ret.get(), ret.get(), b.get()) ||
        !ExpectBIGNUMsEqual(t, "Sum -u B (r is a)", a.get(), ret.get()) ||
        !GFp_BN_copy(ret.get(), b.get()) ||
        !GFp_BN_usub(ret.get(), sum.get(), ret.get()) ||
        !ExpectBIGNUMsEqual(t, "Sum -u B (r is b)", a.get(), ret.get())) {
      return false;
    }
  }

  return true;
}

static bool TestLShift1(FileTest *t) {
  ScopedBIGNUM a = GetBIGNUM(t, "A");
  ScopedBIGNUM lshift1 = GetBIGNUM(t, "LShift1");
  ScopedBIGNUM zero(GFp_BN_new());
  if (!a || !lshift1 || !zero) {
    return false;
  }

  GFp_BN_zero(zero.get());

  ScopedBIGNUM ret(GFp_BN_new()), two(GFp_BN_new()), remainder(GFp_BN_new());
  if (!ret || !two || !remainder ||
      !GFp_BN_set_word(two.get(), 2) ||
      !GFp_BN_add(ret.get(), a.get(), a.get()) ||
      !ExpectBIGNUMsEqual(t, "A + A", lshift1.get(), ret.get()) ||
      !GFp_BN_mul_no_alias(ret.get(), a.get(), two.get()) ||
      !ExpectBIGNUMsEqual(t, "A * 2", lshift1.get(), ret.get()) ||
      !GFp_BN_div(ret.get(), remainder.get(), lshift1.get(), two.get()) ||
      !ExpectBIGNUMsEqual(t, "LShift1 / 2", a.get(), ret.get()) ||
      !ExpectBIGNUMsEqual(t, "LShift1 % 2", zero.get(), remainder.get()) ||
      !GFp_BN_lshift1(ret.get(), a.get()) ||
      !ExpectBIGNUMsEqual(t, "A << 1", lshift1.get(), ret.get()) ||
      !GFp_BN_rshift1(ret.get(), lshift1.get()) ||
      !ExpectBIGNUMsEqual(t, "LShift >> 1", a.get(), ret.get()) ||
      !GFp_BN_rshift1(ret.get(), lshift1.get()) ||
      !ExpectBIGNUMsEqual(t, "LShift >> 1", a.get(), ret.get())) {
    return false;
  }

  // Set the LSB to 1 and test rshift1 again.
  if (!GFp_BN_set_bit(lshift1.get(), 0) ||
      !GFp_BN_div(ret.get(), nullptr /* rem */, lshift1.get(), two.get()) ||
      !ExpectBIGNUMsEqual(t, "(LShift1 | 1) / 2", a.get(), ret.get()) ||
      !GFp_BN_rshift1(ret.get(), lshift1.get()) ||
      !ExpectBIGNUMsEqual(t, "(LShift | 1) >> 1", a.get(), ret.get())) {
    return false;
  }

  return true;
}

static bool TestLShift(FileTest *t) {
  ScopedBIGNUM a = GetBIGNUM(t, "A");
  ScopedBIGNUM lshift = GetBIGNUM(t, "LShift");
  int n = 0;
  if (!a || !lshift || !GetInt(t, &n, "N")) {
    return false;
  }

  ScopedBIGNUM ret(GFp_BN_new());
  if (!ret ||
      !GFp_BN_lshift(ret.get(), a.get(), n) ||
      !ExpectBIGNUMsEqual(t, "A << N", lshift.get(), ret.get()) ||
      !GFp_BN_rshift(ret.get(), lshift.get(), n) ||
      !ExpectBIGNUMsEqual(t, "A >> N", a.get(), ret.get())) {
    return false;
  }

  return true;
}

static bool TestRShift(FileTest *t) {
  ScopedBIGNUM a = GetBIGNUM(t, "A");
  ScopedBIGNUM rshift = GetBIGNUM(t, "RShift");
  int n = 0;
  if (!a || !rshift || !GetInt(t, &n, "N")) {
    return false;
  }

  ScopedBIGNUM ret(GFp_BN_new());
  if (!ret ||
      !GFp_BN_rshift(ret.get(), a.get(), n) ||
      !ExpectBIGNUMsEqual(t, "A >> N", rshift.get(), ret.get())) {
    return false;
  }

  return true;
}

static bool TestSquare(FileTest *t) {
  ScopedBIGNUM a = GetBIGNUM(t, "A");
  ScopedBIGNUM square = GetBIGNUM(t, "Square");
  ScopedBIGNUM zero(GFp_BN_new());
  if (!a || !square || !zero) {
    return false;
  }

  GFp_BN_zero(zero.get());

  ScopedBIGNUM ret(GFp_BN_new()), remainder(GFp_BN_new());
  if (!ret ||
      !GFp_BN_mul_no_alias(ret.get(), a.get(), a.get()) ||
      !ExpectBIGNUMsEqual(t, "A * A", square.get(), ret.get()) ||
      !GFp_BN_div(ret.get(), remainder.get(), square.get(), a.get()) ||
      !ExpectBIGNUMsEqual(t, "Square / A", a.get(), ret.get()) ||
      !ExpectBIGNUMsEqual(t, "Square % A", zero.get(), remainder.get())) {
    return false;
  }

  return true;
}

static bool TestProduct(FileTest *t) {
  ScopedBIGNUM a = GetBIGNUM(t, "A");
  ScopedBIGNUM b = GetBIGNUM(t, "B");
  ScopedBIGNUM product = GetBIGNUM(t, "Product");
  ScopedBIGNUM zero(GFp_BN_new());
  if (!a || !b || !product || !zero) {
    return false;
  }

  GFp_BN_zero(zero.get());

  ScopedBIGNUM ret(GFp_BN_new()), remainder(GFp_BN_new());
  if (!ret || !remainder ||
      !GFp_BN_mul_no_alias(ret.get(), a.get(), b.get()) ||
      !ExpectBIGNUMsEqual(t, "A * B", product.get(), ret.get()) ||
      !GFp_BN_div(ret.get(), remainder.get(), product.get(), a.get()) ||
      !ExpectBIGNUMsEqual(t, "Product / A", b.get(), ret.get()) ||
      !ExpectBIGNUMsEqual(t, "Product % A", zero.get(), remainder.get()) ||
      !GFp_BN_div(ret.get(), remainder.get(), product.get(), b.get()) ||
      !ExpectBIGNUMsEqual(t, "Product / B", a.get(), ret.get()) ||
      !ExpectBIGNUMsEqual(t, "Product % B", zero.get(), remainder.get())) {
    return false;
  }

  return true;
}

static bool TestQuotient(FileTest *t) {
  ScopedBIGNUM a = GetBIGNUM(t, "A");
  ScopedBIGNUM b = GetBIGNUM(t, "B");
  ScopedBIGNUM quotient = GetBIGNUM(t, "Quotient");
  ScopedBIGNUM remainder = GetBIGNUM(t, "Remainder");
  if (!a || !b || !quotient || !remainder) {
    return false;
  }

  ScopedBIGNUM ret(GFp_BN_new()), ret2(GFp_BN_new());
  if (!ret || !ret2 ||
      !GFp_BN_div(ret.get(), ret2.get(), a.get(), b.get()) ||
      !ExpectBIGNUMsEqual(t, "A / B", quotient.get(), ret.get()) ||
      !ExpectBIGNUMsEqual(t, "A % B", remainder.get(), ret2.get()) ||
      !GFp_BN_mul_no_alias(ret.get(), quotient.get(), b.get()) ||
      !GFp_BN_add(ret.get(), ret.get(), remainder.get()) ||
      !ExpectBIGNUMsEqual(t, "Quotient * B + Remainder", a.get(), ret.get())) {
    return false;
  }

  // Test GFp_BN_nnmod.
  if (!GFp_BN_is_negative(b.get())) {
    ScopedBIGNUM nnmod(GFp_BN_new());
    if (!nnmod ||
        !GFp_BN_copy(nnmod.get(), remainder.get()) ||
        (GFp_BN_is_negative(nnmod.get()) &&
         !GFp_BN_add(nnmod.get(), nnmod.get(), b.get())) ||
        !GFp_BN_nnmod(ret.get(), a.get(), b.get()) ||
        !ExpectBIGNUMsEqual(t, "A % B (non-negative)", nnmod.get(),
                            ret.get())) {
      return false;
    }
  }

  return true;
}

static bool TestModMul(FileTest *t) {
  ScopedBIGNUM a = GetBIGNUM(t, "A");
  ScopedBIGNUM b = GetBIGNUM(t, "B");
  ScopedBIGNUM m = GetBIGNUM(t, "M");
  ScopedBIGNUM mod_mul = GetBIGNUM(t, "ModMul");
  if (!a || !b || !m || !mod_mul) {
    return false;
  }

  ScopedBIGNUM ret(GFp_BN_new());
  if (GFp_BN_is_odd(m.get())) {
    // Reduce |a| and |b| and test the Montgomery version.
    ScopedBN_MONT_CTX mont(GFp_BN_MONT_CTX_new());
    ScopedBIGNUM a_tmp(GFp_BN_new()), b_tmp(GFp_BN_new());
    if (!mont || !a_tmp || !b_tmp ||
        !GFp_BN_MONT_CTX_set(mont.get(), m.get()) ||
        !GFp_BN_nnmod(a_tmp.get(), a.get(), m.get()) ||
        !GFp_BN_nnmod(b_tmp.get(), b.get(), m.get()) ||
        !GFp_BN_to_mont(a_tmp.get(), a_tmp.get(), mont.get()) ||
        !GFp_BN_to_mont(b_tmp.get(), b_tmp.get(), mont.get()) ||
        !GFp_BN_mod_mul_mont(ret.get(), a_tmp.get(), b_tmp.get(), mont.get()) ||
        !GFp_BN_from_mont(ret.get(), ret.get(), mont.get()) ||
        !ExpectBIGNUMsEqual(t, "A * B (mod M) (Montgomery)",
                            mod_mul.get(), ret.get())) {
      return false;
    }
  }

  return true;
}

static bool TestModExp(FileTest *t) {
  ScopedBIGNUM a = GetBIGNUM(t, "A");
  ScopedBIGNUM e = GetBIGNUM(t, "E");
  ScopedBIGNUM m = GetBIGNUM(t, "M");
  ScopedBIGNUM mod_exp = GetBIGNUM(t, "ModExp");
  if (!a || !e || !m || !mod_exp) {
    return false;
  }

  ScopedBIGNUM ret(GFp_BN_new());
  if (!ret) {
    return false;
  }

  if (GFp_BN_is_odd(m.get())) {
    // |GFp_BN_mod_exp_mont_vartime| requires the input to already be reduced
    // mod |m| unless |e| is zero (purely due to the ordering of how these
    // special cases are handled). |GFp_BN_mod_exp_mont_consttime| doesn't have
    // the same requirement simply because we haven't gotten around to it yet.
    int expected_ok =
        GFp_BN_cmp(a.get(), m.get()) < 0 || GFp_BN_is_zero(e.get());

    int ok = GFp_BN_mod_exp_mont_vartime(ret.get(), a.get(), e.get(), m.get(),
                                         nullptr);
    if (ok != expected_ok) {
      return false;
    }
    if ((ok &&
         !ExpectBIGNUMsEqual(t, "A ^ E (mod M) (Montgomery)", mod_exp.get(),
                             ret.get()))) {
      return false;
    }

    // Test with a non-NULL |BN_MONT_CTX|.
    ScopedBN_MONT_CTX mont(GFp_BN_MONT_CTX_new());
    if (!mont ||
        !GFp_BN_MONT_CTX_set(mont.get(), m.get())) {
      return false;
    }

    ok = GFp_BN_mod_exp_mont_vartime(ret.get(), a.get(), e.get(), m.get(),
                                     mont.get());
    if (ok != expected_ok) {
      return false;
    }
    if ((ok &&
         !ExpectBIGNUMsEqual(t, "A ^ E (mod M) (Montgomery)", mod_exp.get(),
                             ret.get()))) {
      return false;
    }

    if (!GFp_BN_mod_exp_mont_consttime(ret.get(), a.get(), e.get(),
                                       mont.get()) ||
        !ExpectBIGNUMsEqual(t, "A ^ E (mod M) (constant-time)", mod_exp.get(),
                            ret.get())) {
      return false;
    }
  }

  return true;
}

static bool TestModInv(FileTest *t) {
  ScopedBIGNUM a = GetBIGNUM(t, "A");
  ScopedBIGNUM m = GetBIGNUM(t, "M");
  ScopedBIGNUM mod_inv = GetBIGNUM(t, "ModInv");
  if (!a || !m || !mod_inv) {
    return false;
  }

  ScopedBIGNUM ret(GFp_BN_new());
  int no_inverse;
  if (!ret ||
      !GFp_BN_mod_inverse_odd(ret.get(), &no_inverse, a.get(), m.get()) ||
      no_inverse ||
      !ExpectBIGNUMsEqual(t, "inv(A) (mod M)", mod_inv.get(), ret.get())) {
    return false;
  }

  return true;
}

struct Test {
  const char *name;
  bool (*func)(FileTest *t);
};

static const Test kTests[] = {
    {"Sum", TestSum},
    {"LShift1", TestLShift1},
    {"LShift", TestLShift},
    {"RShift", TestRShift},
    {"Square", TestSquare},
    {"Product", TestProduct},
    {"Quotient", TestQuotient},
    {"ModMul", TestModMul},
    {"ModExp", TestModExp},
    {"ModInv", TestModInv},
};

static bool RunTest(FileTest *t, void *) {
  for (const Test &test : kTests) {
    if (t->GetType() != test.name) {
      continue;
    }
    return test.func(t);
  }
  t->PrintLine("Unknown test type: ", t->GetType().c_str());
  return false;
}

static bool TestBN2BinPadded(RAND *rng) {
  uint8_t zeros[256], out[256], reference[128];

  memset(zeros, 0, sizeof(zeros));

  // Test edge case at 0.
  ScopedBIGNUM n(GFp_BN_new());
  if (!n || !GFp_BN_bn2bin_padded(NULL, 0, n.get())) {
    fprintf(stderr,
            "GFp_BN_bn2bin_padded failed to encode 0 in an empty buffer.\n");
    return false;
  }
  memset(out, -1, sizeof(out));
  if (!GFp_BN_bn2bin_padded(out, sizeof(out), n.get())) {
    fprintf(stderr,
            "GFp_BN_bn2bin_padded failed to encode 0 in a non-empty buffer.\n");
    return false;
  }
  if (memcmp(zeros, out, sizeof(out))) {
    fprintf(stderr, "GFp_BN_bn2bin_padded did not zero buffer.\n");
    return false;
  }

  // Test a random numbers at various byte lengths.
  for (size_t bytes = 128 - 7; bytes <= 128; bytes++) {
    if (!BN_rand(n.get(), bytes * 8, rng)) {
      return false;
    }
    if (GFp_BN_num_bytes(n.get()) != bytes ||
        BN_bn2bin(n.get(), reference) != bytes) {
      fprintf(stderr, "Bad result from GFp_BN_rand; bytes.\n");
      return false;
    }
    // Empty buffer should fail.
    if (GFp_BN_bn2bin_padded(NULL, 0, n.get())) {
      fprintf(stderr,
              "GFp_BN_bn2bin_padded incorrectly succeeded on empty buffer.\n");
      return false;
    }
    // One byte short should fail.
    if (GFp_BN_bn2bin_padded(out, bytes - 1, n.get())) {
      fprintf(stderr, "GFp_BN_bn2bin_padded incorrectly succeeded on short.\n");
      return false;
    }
    // Exactly right size should encode.
    if (!GFp_BN_bn2bin_padded(out, bytes, n.get()) ||
        memcmp(out, reference, bytes) != 0) {
      fprintf(stderr, "GFp_BN_bn2bin_padded gave a bad result.\n");
      return false;
    }
    // Pad up one byte extra.
    if (!GFp_BN_bn2bin_padded(out, bytes + 1, n.get()) ||
        memcmp(out + 1, reference, bytes) || memcmp(out, zeros, 1)) {
      fprintf(stderr, "GFp_BN_bn2bin_padded gave a bad result.\n");
      return false;
    }
    // Pad up to 256.
    if (!GFp_BN_bn2bin_padded(out, sizeof(out), n.get()) ||
        memcmp(out + sizeof(out) - bytes, reference, bytes) ||
        memcmp(out, zeros, sizeof(out) - bytes)) {
      fprintf(stderr, "GFp_BN_bn2bin_padded gave a bad result.\n");
      return false;
    }
  }

  return true;
}

static int BN_is_word(const BIGNUM *bn, BN_ULONG w) {
  return GFp_BN_abs_is_word(bn, w) && (w == 0 || bn->neg == 0);
}

static bool TestHex2BN() {
  ScopedBIGNUM bn;
  int ret = HexToBIGNUM(&bn, "0");
  if (ret != 1 || !GFp_BN_is_zero(bn.get()) || GFp_BN_is_negative(bn.get())) {
    fprintf(stderr, "GFp_BN_hex2bn gave a bad result.\n");
    return false;
  }

  ret = HexToBIGNUM(&bn, "256");
  if (ret != 3 || !BN_is_word(bn.get(), 0x256) ||
      GFp_BN_is_negative(bn.get())) {
    fprintf(stderr, "BN_hex2bn gave a bad result.\n");
    return false;
  }

  ret = HexToBIGNUM(&bn, "-42");
  if (ret != 3 || !GFp_BN_abs_is_word(bn.get(), 0x42) ||
      !GFp_BN_is_negative(bn.get())) {
    fprintf(stderr, "BN_hex2bn gave a bad result.\n");
    return false;
  }

  ret = HexToBIGNUM(&bn, "-0");
  if (ret != 2 || !GFp_BN_is_zero(bn.get()) || GFp_BN_is_negative(bn.get())) {
    fprintf(stderr, "BN_hex2bn gave a bad result.\n");
    return false;
  }

  ret = HexToBIGNUM(&bn, "abctrailing garbage is ignored");
  if (ret != 3 || !BN_is_word(bn.get(), 0xabc) ||
      GFp_BN_is_negative(bn.get())) {
    fprintf(stderr, "BN_hex2bn gave a bad result.\n");
    return false;
  }

  return true;
}

static bool TestRand(RAND *rng) {
  ScopedBIGNUM bn(GFp_BN_new());
  if (!bn) {
    return false;
  }

  // Test GFp_BN_rand accounts for degenerate cases
  if (!BN_rand(bn.get(), 0, rng) ||
      !GFp_BN_is_zero(bn.get())) {
    fprintf(stderr, "BN_rand gave a bad result.\n");
    return false;
  }

  if (!BN_rand(bn.get(), 1, rng) ||
      !BN_is_word(bn.get(), 1)) {
    fprintf(stderr, "BN_rand gave a bad result.\n");
    return false;
  }

  return true;
}

static bool TestNegativeZero() {
  ScopedBIGNUM a(GFp_BN_new());
  ScopedBIGNUM b(GFp_BN_new());
  ScopedBIGNUM c(GFp_BN_new());
  if (!a || !b || !c) {
    return false;
  }

  // Test that GFp_BN_mul_no_alias never gives negative zero.
  if (!GFp_BN_set_word(a.get(), 1)) {
    return false;
  }
  BN_set_negative(a.get(), 1);
  GFp_BN_zero(b.get());
  if (!GFp_BN_mul_no_alias(c.get(), a.get(), b.get())) {
    return false;
  }
  if (!GFp_BN_is_zero(c.get()) || GFp_BN_is_negative(c.get())) {
    fprintf(stderr, "Multiplication test failed.\n");
    return false;
  }

  ScopedBIGNUM numerator(GFp_BN_new()), denominator(GFp_BN_new());
  if (!numerator || !denominator) {
    return false;
  }

  // Test that GFp_BN_div never gives negative zero in the quotient.
  if (!GFp_BN_set_word(numerator.get(), 1) ||
      !GFp_BN_set_word(denominator.get(), 2)) {
    return false;
  }
  BN_set_negative(numerator.get(), 1);
  if (!GFp_BN_div(a.get(), b.get(), numerator.get(), denominator.get())) {
    return false;
  }
  if (!GFp_BN_is_zero(a.get()) || GFp_BN_is_negative(a.get())) {
    fprintf(stderr, "Incorrect quotient.\n");
    return false;
  }

  // Test that GFp_BN_div never gives negative zero in the remainder.
  if (!GFp_BN_set_word(denominator.get(), 1)) {
    return false;
  }
  if (!GFp_BN_div(a.get(), b.get(), numerator.get(), denominator.get())) {
    return false;
  }
  if (!GFp_BN_is_zero(b.get()) || GFp_BN_is_negative(b.get())) {
    fprintf(stderr, "Incorrect remainder.\n");
    return false;
  }

  // Test that BN_set_negative will not produce a negative zero.
  GFp_BN_zero(a.get());
  BN_set_negative(a.get(), 1);
  if (GFp_BN_is_negative(a.get())) {
    fprintf(stderr, "BN_set_negative produced a negative zero.\n");
    return false;
  }

  return true;
}

static bool TestBadModulus() {
  ScopedBIGNUM a(GFp_BN_new());
  ScopedBIGNUM b(GFp_BN_new());
  ScopedBIGNUM zero(GFp_BN_new());
  ScopedBN_MONT_CTX mont(GFp_BN_MONT_CTX_new());
  if (!a || !b || !zero || !mont) {
    return false;
  }

  GFp_BN_zero(zero.get());

  if (GFp_BN_div(a.get(), b.get(), GFp_BN_value_one(), zero.get())) {
    fprintf(stderr, "Division by zero unexpectedly succeeded.\n");
    return false;
  }
  ERR_clear_error();

  if (GFp_BN_mod_exp_mont_vartime(a.get(), GFp_BN_value_one(),
                                  GFp_BN_value_one(), zero.get(), nullptr)) {
    fprintf(stderr, "GFp_BN_mod_exp_mont_vartime with zero modulus unexpectedly "
            "succeeded.\n");
    return 0;
  }
  ERR_clear_error();

  if (GFp_BN_MONT_CTX_set(mont.get(), zero.get())) {
    fprintf(stderr,
            "GFp_BN_MONT_CTX_set unexpectedly succeeded for zero modulus.\n");
    return false;
  }
  ERR_clear_error();

  // Some operations also may not be used with an even modulus.

  if (!GFp_BN_set_word(b.get(), 16)) {
    return false;
  }

  if (GFp_BN_MONT_CTX_set(mont.get(), b.get())) {
    fprintf(stderr,
            "GFp_BN_MONT_CTX_set unexpectedly succeeded for even modulus.\n");
    return false;
  }
  ERR_clear_error();

  if (GFp_BN_mod_exp_mont_vartime(a.get(), GFp_BN_value_one(),
                                  GFp_BN_value_one(), b.get(), nullptr)) {
    fprintf(stderr, "GFp_BN_mod_exp_mont_vartime with even modulus unexpectedly "
            "succeeded!\n");
    return 0;
  }
  ERR_clear_error();

  return true;
}

// TestExpModZero tests that 1**0 mod 1 == 0.
static bool TestExpModZero(RAND *rng) {
  ScopedBIGNUM zero(GFp_BN_new()), a(GFp_BN_new()), r(GFp_BN_new());
  if (!zero || !a || !r || !BN_rand(a.get(), 1024, rng)) {
    return false;
  }
  GFp_BN_zero(zero.get());

  ScopedBN_MONT_CTX one_mont(GFp_BN_MONT_CTX_new());
  if (!GFp_BN_mod_exp_mont_vartime(r.get(), a.get(), zero.get(),
                                   GFp_BN_value_one(), nullptr) ||
      !GFp_BN_is_zero(r.get()) ||
      !one_mont ||
      !GFp_BN_MONT_CTX_set(one_mont.get(), GFp_BN_value_one()) ||
      !GFp_BN_mod_exp_mont_consttime(r.get(), a.get(), zero.get(),
                                     one_mont.get()) ||
      !GFp_BN_is_zero(r.get())) {
    return false;
  }

  return true;
}

static bool TestExpModRejectUnreduced() {
  ScopedBIGNUM r(GFp_BN_new());
  if (!r) {
    return false;
  }

  static const BN_ULONG kBases[] = { 1, 3 };
  static const BN_ULONG kExponents[] = { 1, 2, 3 };
  static const BN_ULONG kModuli[] = { 1, 3 };

  for (BN_ULONG mod_value : kModuli) {
    ScopedBIGNUM mod(GFp_BN_new());
    ScopedBN_MONT_CTX mont(GFp_BN_MONT_CTX_new());
    if (!mod ||
        !GFp_BN_set_word(mod.get(), mod_value) ||
        !mont ||
        !GFp_BN_MONT_CTX_set(mont.get(), mod.get())) {
      return false;
    }
    for (BN_ULONG exp_value : kExponents) {
      ScopedBIGNUM exp(GFp_BN_new());
      if (!exp ||
          !GFp_BN_set_word(exp.get(), exp_value)) {
        return false;
      }
      for (BN_ULONG base_value : kBases) {
        ScopedBIGNUM base(GFp_BN_new());
        if (!base ||
            !GFp_BN_set_word(base.get(), base_value)) {
          return false;
        }

        if (base_value >= mod_value &&
            GFp_BN_mod_exp_mont_vartime(r.get(), base.get(), exp.get(),
                                        mod.get(), nullptr)) {
          fprintf(stderr, "GFp_BN_mod_exp_mont_vartime(%d, %d, %d) succeeded!\n",
                  (int)base_value, (int)exp_value, (int)mod_value);
          return false;
        }

        if (base_value >= mod_value &&
            GFp_BN_mod_exp_mont_consttime(r.get(), base.get(), exp.get(),
                                          mont.get())) {
          fprintf(stderr, "GFp_BN_mod_exp_mont_consttime(%d, %d, %d) succeeded!\n",
                  (int)base_value, (int)exp_value, (int)mod_value);
          return false;
        }

        BN_set_negative(base.get(), 1);

        if (GFp_BN_mod_exp_mont_vartime(r.get(), base.get(), exp.get(),
                                        mod.get(), nullptr)) {
          fprintf(stderr, "GFp_BN_mod_exp_mont_vartime(%d, %d, %d) succeeded!\n",
                  -(int)base_value, (int)exp_value, (int)mod_value);
          return false;
        }
        if (GFp_BN_mod_exp_mont_consttime(r.get(), base.get(), exp.get(),
                                          mont.get())) {
          fprintf(stderr, "GFp_BN_mod_exp_mont_consttime(%d, %d, %d) succeeded!\n",
                  -(int)base_value, (int)exp_value, (int)mod_value);
          return false;
        }
      }
    }
  }

  return true;
}

static bool TestModInvRejectUnreduced(RAND *rng) {
  ScopedBIGNUM r(GFp_BN_new());
  if (!r) {
    return false;
  }

  static const BN_ULONG kBases[] = { 2, 4, 6 };
  static const BN_ULONG kModuli[] = { 1, 3 };

  for (BN_ULONG mod_value : kModuli) {
    ScopedBIGNUM mod(GFp_BN_new());
    ScopedBN_MONT_CTX mont(GFp_BN_MONT_CTX_new());
    if (!mod ||
        !GFp_BN_set_word(mod.get(), mod_value) ||
        !mont ||
        !GFp_BN_MONT_CTX_set(mont.get(), mod.get())) {
      return false;
    }
    for (BN_ULONG base_value : kBases) {
      ScopedBIGNUM base(GFp_BN_new());
      if (!base ||
          !GFp_BN_set_word(base.get(), base_value)) {
        return false;
      }

      int no_inverse;

      if (base_value >= mod_value &&
          GFp_BN_mod_inverse_odd(r.get(), &no_inverse, base.get(), mod.get())) {
        fprintf(stderr, "GFp_BN_mod_inverse_odd(%d, %d) succeeded!\n",
                (int)base_value, (int)mod_value);
        return false;
      }
      if (base_value >= mod_value &&
          GFp_BN_mod_inverse_blinded(r.get(), &no_inverse, base.get(),
                                     mont.get(), rng)) {
        fprintf(stderr, "GFp_BN_mod_inverse_blinded(%d, %d) succeeded!\n",
          (int)base_value, (int)mod_value);
        return false;
      }

      BN_set_negative(base.get(), 1);

      if (GFp_BN_mod_inverse_odd(r.get(), &no_inverse, base.get(), mod.get())) {
        fprintf(stderr, "GFp_BN_mod_inverse_odd(%d, %d) succeeded!\n",
                -(int)base_value, (int)mod_value);
        return false;
      }
      if (GFp_BN_mod_inverse_blinded(r.get(), &no_inverse, base.get(),
                                     mont.get(), rng)) {
        fprintf(stderr, "GFp_BN_mod_inverse_blinded(%d, %d) succeeded!\n",
                -(int)base_value, (int)mod_value);
        return false;
      }

    }
  }

  return true;
}

static bool TestCmpWord() {
  static const BN_ULONG kMaxWord = (BN_ULONG)-1;

  ScopedBIGNUM r(GFp_BN_new());
  if (!r ||
      !GFp_BN_set_word(r.get(), 0)) {
    return false;
  }

  if (GFp_BN_cmp_word(r.get(), 0) != 0 ||
      GFp_BN_cmp_word(r.get(), 1) >= 0 ||
      GFp_BN_cmp_word(r.get(), kMaxWord) >= 0) {
    fprintf(stderr, "GFp_BN_cmp_word compared against 0 incorrectly.\n");
    return false;
  }

  if (!GFp_BN_set_word(r.get(), 100)) {
    return false;
  }

  if (GFp_BN_cmp_word(r.get(), 0) <= 0 ||
      GFp_BN_cmp_word(r.get(), 99) <= 0 ||
      GFp_BN_cmp_word(r.get(), 100) != 0 ||
      GFp_BN_cmp_word(r.get(), 101) >= 0 ||
      GFp_BN_cmp_word(r.get(), kMaxWord) >= 0) {
    fprintf(stderr, "GFp_BN_cmp_word compared against 100 incorrectly.\n");
    return false;
  }

  BN_set_negative(r.get(), 1);

  if (GFp_BN_cmp_word(r.get(), 0) >= 0 ||
      GFp_BN_cmp_word(r.get(), 100) >= 0 ||
      GFp_BN_cmp_word(r.get(), kMaxWord) >= 0) {
    fprintf(stderr, "GFp_BN_cmp_word compared against -100 incorrectly.\n");
    return false;
  }

  if (!GFp_BN_set_word(r.get(), kMaxWord)) {
    return false;
  }

  if (GFp_BN_cmp_word(r.get(), 0) <= 0 ||
      GFp_BN_cmp_word(r.get(), kMaxWord - 1) <= 0 ||
      GFp_BN_cmp_word(r.get(), kMaxWord) != 0) {
    fprintf(stderr, "GFp_BN_cmp_word compared against kMaxWord incorrectly.\n");
    return false;
  }

  if (!GFp_BN_add(r.get(), r.get(), GFp_BN_value_one())) {
    return false;
  }

  if (GFp_BN_cmp_word(r.get(), 0) <= 0 ||
      GFp_BN_cmp_word(r.get(), kMaxWord) <= 0) {
    fprintf(stderr, "GFp_BN_cmp_word compared against kMaxWord + 1 incorrectly.\n");
    return false;
  }

  BN_set_negative(r.get(), 1);

  if (GFp_BN_cmp_word(r.get(), 0) >= 0 ||
      GFp_BN_cmp_word(r.get(), kMaxWord) >= 0) {
    fprintf(stderr,
            "GFp_BN_cmp_word compared against -kMaxWord - 1 incorrectly.\n");
    return false;
  }

  return true;
}

extern "C" int bssl_bn_test_main(RAND *rng) {
  if (!TestBN2BinPadded(rng) ||
      !TestHex2BN() ||
      !TestRand(rng) ||
      !TestNegativeZero() ||
      !TestBadModulus() ||
      !TestExpModZero(rng) ||
      !TestExpModRejectUnreduced() ||
      !TestModInvRejectUnreduced(rng) ||
      !TestCmpWord()) {
    return 1;
  }

  return FileTestMain(RunTest, nullptr, "crypto/bn/bn_tests.txt");
}
