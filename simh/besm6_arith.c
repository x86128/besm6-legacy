/*
 * BESM-6 arithmetic instructions.
 *
 * Copyright (c) 1997-2009, Leonid Broukhis
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 *
 * You can redistribute this program and/or modify it under the terms of
 * the GNU General Public License as published by the Free Software Foundation;
 * either version 2 of the License, or (at your discretion) any later version.
 * See the accompanying file "COPYING" for more details.
 */
#include <math.h>
#include "besm6_defs.h"
#include "besm6_legacy.h"

/*
 * 64-bit floating-point value in format of standard IEEE 754.
 */
typedef union {
	double d;
	struct {
#ifndef WORDS_BIGENDIAN
		unsigned right32, left32;
#else
		unsigned left32, right32;
#endif
	} u;
} math_t;

/*
 * Convert floating-point value from BESM-6 format to IEEE 754.
 */
#define BESM_TO_IEEE(from,to) {\
	to.u.left32 = ((from.o - 64 + 1022) << 20) |\
			((from.ml << 5) & 0xfffff) |\
			(from.r >> 19);\
	to.u.right32 = (from.r & 0x7ffff) << 13;\
}

alureg_t acc, accex, enreg, zeroword;

/* Требуется округление. */
int rnd_rq;

alureg_t negate (alureg_t word)
{
	if (NEGATIVE (word))
		word.ml |= 0x20000;
	word.r = (~word.r & 0xffffff) + 1;
	word.ml = (~word.ml + (word.r >> 24)) & 0x3ffff;
	word.r &= 0xffffff;
	if (((word.ml >> 1) ^ word.ml) & 0x10000) {
		word.r = ((word.r >> 1) | (word.ml << 23)) & 0xffffff;
		word.ml >>= 1;
		++word.o;
	}
	if (NEGATIVE (word))
		word.ml |= 0x20000;
	return word;
}

void add()
{
	alureg_t        a1, a2;
	int             diff, neg;

	diff = acc.o - enreg.o;
	if (diff < 0) {
		diff = -diff;
		a1 = acc;
		a2 = enreg;
	} else {
		a1 = enreg;
		a2 = acc;
	}
	neg = NEGATIVE (a1);
	if (!diff)
		/*
		accex.o = accex.ml = accex.r = 0;
		*/      ;
	else if (diff <= 16) {
		int rdiff = 16 - diff;
		/*
		accex.r = 0;
		*/
		rnd_rq = (accex.ml = (a1.r << rdiff) & 0xffff) != 0;
		a1.r = ((a1.r >> diff) | (a1.ml << (rdiff + 8))) & 0xffffff;
		a1.ml = ((a1.ml >> diff) |
			(neg ? ~0 << rdiff : 0)) & 0x3ffff;
	} else if (diff <= 40) {
		diff -= 16;
		rnd_rq = (accex.r = (a1.r << (24 - diff)) & 0xffffff) != 0;
/* было         rnd_rq |= (accex.ml = (((a1.r & 0xff0000) >> diff) |   */
		rnd_rq |= (accex.ml = ((a1.r >> diff) |
			(a1.ml << (24 - diff))) & 0xffff) != 0;
		a1.r = ((((a1.r >> 16) | (a1.ml << 8)) >> diff) |
				(neg ? (~0l << (24 - diff)) : 0)) & 0xffffff;
		a1.ml = neg ? 0x3ffff : 0;
	} else if (diff <= 56) {
		int rdiff = 16 - (diff -= 40);
		rnd_rq = a1.ml || a1.r;
		accex.r = ((a1.r >> diff) | (a1.ml << (rdiff + 8))) & 0xffffff;
		accex.ml = ((a1.ml >> diff) |
			(neg ? ~0 << rdiff : 0)) & 0xffff;
		if (neg) {
			a1.r = 0xffffff;
			a1.ml = 0x3ffff;
		} else
			a1.ml = a1.r = 0;
	} else if (diff <= 80) {
		diff -= 56;
		rnd_rq = a1.ml || a1.r;
		accex.r = ((((a1.r >> 16) | (a1.ml << 8)) >> diff) |
				(neg ? (~0l << (24 - diff)) : 0)) & 0xffffff;
		accex.ml = neg ? 0x3ffff : 0;
		if (neg) {
			a1.r = 0xffffff;
			accex.ml = a1.ml = 0x3ffff;
		} else
			accex.ml = a1.ml = a1.r = 0;
	} else {
		rnd_rq = a1.ml || a1.r;
		if (neg) {
			accex.ml = 0xffff;
			a1.r = accex.r = 0xffffff;
			a1.ml = 0x3ffff;
		} else
			accex.ml = accex.r = a1.ml = a1.r = 0;
	}
	acc.o = a2.o;
	acc.r = a1.r + a2.r;
	acc.ml = a1.ml + a2.ml + (acc.r >> 24);
	acc.r &= 0xffffff;
}

void aax()
{
	acc.l &= enreg.l;
	acc.r &= enreg.r;
	accex = zeroword;
}

void aex()
{
	accex = acc;
	acc.l ^= enreg.l;
	acc.r ^= enreg.r;
}

void arx()
{
	uint32 i;

	acc.r = (i = acc.r + enreg.r) & 0xffffff;
	acc.l = (i = acc.l + enreg.l + (i >> 24)) & 0xffffff;

	if (i & 0x1000000) {
		acc.r = (i = acc.r + 1) & 0xffffff;
		acc.l = acc.l + (i >> 24);
	}

	accex = zeroword;
}

void avx()
{
	if (NEGATIVE (enreg))
		acc = negate (acc);
}

void aox()
{
	acc.l |= enreg.l;
	acc.r |= enreg.r;
	accex = zeroword;
}

/*
 * non-restoring division
 */
double nrdiv (double n, double d)
{
	int ne, de, re;
	double nn, dd, res, q, eps;

	nn = frexp (n, &ne);
	dd = frexp (d, &de);

	res = 0, q = 0.5;
	eps = ldexp (q, -40);		/* run for 40 bits of precision */

	if (fabs (nn) >= fabs (dd))
		nn/=2, ne++;

	while (q > eps) {
		if (nn == 0.0)
			break;

		if (fabs (nn) < 0.25)
			nn *= 2;	/* magic shortcut */
		else if ((nn > 0) ^ (dd > 0)) {
			res -= q;
			nn = 2*nn+dd;
		} else {
			res += q;
			nn = 2*nn-dd;
		}
		q /= 2;
	}
	res = frexp (res, &re);

	return ldexp (res, re+ne-de);
}

void b6div ()
{
	int             neg, o;
	unsigned long   i, c, bias = 0;
	math_t          dividend, divisor, quotient;

	accex.o = accex.ml = accex.r = 0;
	neg = NEGATIVE (acc) != NEGATIVE (enreg);
	if (NEGATIVE (acc))
		acc = negate (acc);
	if (NEGATIVE (enreg))
		enreg = negate (enreg);
	if ((enreg.ml & 0x8000) == 0)
		longjmp (cpu_halt, STOP_DIVZERO);

	if ((acc.ml == 0) && (acc.r == 0)) {
qzero:
		acc = zeroword;
		return;
	}
	if ((acc.ml & 0x8000) == 0) {   /* normalize */
		while (acc.ml == 0) {
			if (!acc.r)
				goto qzero;
			bias += 16;
			acc.ml = acc.r >> 8;
			acc.r = (acc.r & 0xff) << 16;
		}
		for (i = 0x8000, c = 0; (i & acc.ml) == 0; ++c)
			i >>= 1;
		bias += c;
		acc.ml = ((acc.ml << c) | (acc.r >> (24 - c))) & 0xffff;
		acc.r = (acc.r << c) & 0xffffff;
	}

	BESM_TO_IEEE (acc, dividend);
	dividend.u.left32 -= bias << 20;
	BESM_TO_IEEE (enreg, divisor);

	/* quotient.d = dividend.d / divisor.d; */
	quotient.d = nrdiv (dividend.d, divisor.d);

	o = quotient.u.left32 >> 20;
	o = o - 1022 + 64;
	if (o < 0)
		goto qzero;
	acc.o = o & 0x7f;
	acc.ml = ((quotient.u.left32 & 0xfffff) | 0x100000) >> 5;
	acc.r = ((quotient.u.left32 & 0x1f) << 19) |
			(quotient.u.right32 >> 13);
	if (neg)
		acc = negate (acc);
	if ((o > 0x7f) && ! (RAU & RAU_OVF_DISABLE))
		longjmp (cpu_halt, STOP_OVFL);
}

void mul()
{
	uint8           neg = 0;
	alureg_t        a, b;
	uint16          a1, a2, a3, b1, b2, b3;
	register uint32 l;

	a = acc;
	b = enreg;

	if ((!a.l && !a.r) || (!b.l && !b.r)) {
		/* multiplication by zero is zero */
		acc.l = acc.r = acc.o = acc.ml =
		accex.l = accex.r = accex.o = accex.ml = 0;
		rnd_rq = 0;
		return;
	}

	if (NEGATIVE (a)) {
		neg = 1;
		a = negate (a);
	}
	if (NEGATIVE (b)) {
		neg ^= 1;
		b = negate (b);
	}
	acc.o = a.o + b.o - 64;

	a3 = a.r & 0xfff;
	a2 = a.r >> 12;
	a1 = a.ml;

	b3 = b.r & 0xfff;
	b2 = b.r >> 12;
	b1 = b.ml;

	accex.r = (uint32) a3 * b3;

	l = (uint32) a2 * b3 + (uint32) a3 * b2;
	accex.r += (l << 12) & 0xfff000;
	accex.ml = l >> 12;

	l = (uint32) a1 * b3 + (uint32) a2 * b2 + (uint32) a3 * b1;
	accex.ml += l & 0xffff;
	acc.r = l >> 16;

	l = (uint32) a1 * b2 + (uint32) a2 * b1;
	accex.ml += (l & 0xf) << 12;
	acc.r += (l >> 4) & 0xffffff;
	acc.ml = l >> 28;

	l = (uint32) a1 * b1;
	acc.r += (l & 0xffff) << 8;
	acc.ml += l >> 16;

	accex.ml += accex.r >> 24;
	acc.r += accex.ml >> 16;
	acc.ml += acc.r >> 24;
	accex.r &= 0xffffff;
	accex.ml &= 0xffff;
	acc.r &= 0xffffff;
	acc.ml &= 0xffff;

	if (neg) {
		accex.r = (~accex.r & 0xffffff) + 1;
		accex.ml = (~accex.ml & 0xffff) + (accex.r >> 24);
		accex.r &= 0xffffff;
		acc.r = (~acc.r & 0xffffff) + (accex.ml >> 16);
		accex.ml &= 0xffff;
		acc.ml = ((~acc.ml & 0xffff) + (acc.r >> 24)) | 0x30000;
		acc.r &= 0xffffff;
	}

	rnd_rq = !!(accex.ml | accex.r);
}

void apx()
{
	for (accex.l = accex.r = 0; enreg.r; enreg.r >>= 1, acc.r >>= 1)
		if (enreg.r & 1) {
			accex.r = ((accex.r >> 1) | (accex.l << 23)) & BITS24;
			accex.l >>= 1;
			if (acc.r & 1)
				accex.l |= 0x800000;
		}
	for (; enreg.l; enreg.l >>= 1, acc.l >>= 1)
		if (enreg.l & 1) {
			accex.r = ((accex.r >> 1) | (accex.l << 23)) & BITS24;
			accex.l >>= 1;
			if (acc.l & 1)
				accex.l |= 0x800000;
		}
	acc = accex;
	accex.l = accex.r = 0;
}

void aux()
{
	int     i;

	accex.l = accex.r = 0;
	for (i = 0; i < 24; ++i) {
		accex.l <<= 1;
		if (enreg.l & 0x800000) {
			if (acc.l & 0x800000)
				accex.l |= 1;
			acc.l = (acc.l << 1) | (acc.r >> 23);
			acc.r = (acc.r << 1) & BITS24;
		}
		enreg.l <<= 1;
	}
	for (i = 0; i < 24; ++i) {
		accex.r <<= 1;
		if (enreg.r & 0x800000) {
			if (acc.l & 0x800000)
				accex.r |= 1;
			acc.l = (acc.l << 1);
		}
		enreg.r <<= 1;
	}
	acc.l = accex.l;
	acc.r = accex.r;
	accex.l = accex.r = 0;
}

void acx()
{
	int     c = 0;
	uint32  i;

	for (i = acc.l; i; i &= i - 1, c++);
	for (i = acc.r; i; i &= i - 1, c++);
	acc.r = c;
	acc.l = 0;
	arx();
}

void anx()
{
	uint32  c;
	uint32  i;
	uint8   b;

	if (acc.l) {
		i = acc.l;
		c = 1;
	} else if (acc.r) {
		i = acc.r;
		c = 25;
	} else {
		acc = enreg;
		accex.l = accex.r = 0;
		return;
	}
	if (i & 0xff0000)
		b = i >> 16;
	else if (i & 0xff00) {
		b = i >> 8;
		c += 8;
	} else {
		b = i;
		c += 16;
	}
	while (!(b & 0x80)) {
		b <<= 1;
		++c;
	}

	enreg.o = 64 + 48 - c;
	asx();

	acc.r = (i = c + enreg.r) & 0xffffff;
	acc.l = (i = enreg.l + (i >> 24)) & 0xffffff;

	if (i & 0x1000000) {
		acc.r = (i = acc.r + 1) & 0xffffff;
		acc.l = acc.l + (i >> 24);
	}
}

void epx()
{
	acc.o += enreg.o - 64;
}

void emx()
{
	acc.o += 64 - enreg.o;
}

void asx()
{
	int     i, j;

	accex.l = accex.r = 0;
	if (!(i = enreg.o - 64))
		return;
	if (i > 0) {
		if (i < 24) {
			j = 24 - i;
			accex.l = (acc.r << j) & 0xffffff;
			acc.r = ((acc.r >> i) | (acc.l << j)) & 0xffffff;
			acc.l >>= i;
		} else if (i < 48) {
			i -= 24;
			j = 24 - i;
			accex.r = (acc.r << j) & 0xffffff;
			accex.l = ((acc.r >> i) | (acc.l << j)) & 0xffffff;
			acc.r = acc.l >> i;
			acc.l = 0;
		} else if (i < 72) {
			i -= 48;
			accex.r = ((acc.r >> i) | (acc.l << (24 - i))) & 0xffffff;
			accex.l = acc.l >> i;
			acc.l = acc.r = 0;
		} else if (i < 96) {
			accex.r = acc.l >> (i - 72);
			acc.l = acc.r = 0;
		} else
			acc.l = acc.r = 0;
	} else {
		if (i > -24) {
			i = -i;
			j = 24 - i;
			accex.r = acc.l >> j;
			acc.l = ((acc.l << i) | (acc.r >> j)) & 0xffffff;
			acc.r = (acc.r << i) & 0xffffff;
		} else if (i > -48) {
			i = -i - 24;
			j = 24 - i;
			accex.l = acc.l >> j;
			accex.r = ((acc.l << i) | (acc.r >> j)) & 0xffffff;
			acc.l = (acc.r << i) & 0xffffff;
			acc.r = 0;
		} else if (i > -72) {
			i = -i - 48;
			j = 24 - i;
			accex.l = ((acc.l << i) | (acc.r >> j)) & 0xffffff;
			accex.r = (acc.r << i) & 0xffffff;
			acc.l = acc.r = 0;
		} else if (i > -96) {
			accex.l = (acc.r << i) & 0xffffff;
			acc.l = acc.r = 0;
		} else
			acc.l = acc.r = 0;
	}
}

void yta()
{
	if (IS_LOGICAL (RAU)) {
		acc = accex;
		return;
	}

	acc.r = accex.r;
	acc.l = (accex.l & 0xffff) |
		((acc.l + (enreg.o << 17) - (64 << 17)) & 0x1fe0000);
	if (acc.l & 0x1000000) {
		acc.l &= 0xffffff;
		if (! (RAU & RAU_OVF_DISABLE))
			longjmp (cpu_halt, STOP_OVFL);
	}
}

/*
 * Fetch BESM "real" value and return it as native double.
 */
double fetch_real (int addr)
{
	alureg_t word;
	math_t exponent;
	int64_t mantissa;

	word = toalu (mmu_load (addr));
	mantissa = ((int64_t) word.l << 24 | word.r) << (64 - 48 + 7);

	exponent.u.left32 = ((word.l >> 17) - 64 + 1023 - 64 + 1) << 20;
	exponent.u.right32 = 0;

	return mantissa * exponent.d;
}

void normalize_and_round ()
{
	uint32 rr = 0;
	int i, r;

	switch ((acc.ml >> 16) & 3) {
	case 2:
	case 1:
		rnd_rq |= acc.r & 1;
		accex.r = (accex.r >> 1) | (accex.ml << 23);
		accex.ml = (accex.ml >> 1) | (acc.r << 15);
		acc.r = (acc.r >> 1) | (acc.ml << 23);
		acc.ml >>= 1;
		++acc.o;
		goto chk_rnd;
	}

	if (RAU & RAU_NORM_DISABLE)
		goto chk_rnd;
	i = (acc.ml >> 15) & 3;
	if (i == 0) {
		if ((r = acc.ml & 0xffff)) {
			int cnt;
			for (cnt = 0; (r & 0x8000) == 0;
						++cnt, r <<= 1);
			acc.ml = (r & 0xffff) |
					(acc.r >> (24 - cnt));
			acc.r = (acc.r << cnt) |
					(rr = accex.ml >> (16 - cnt));
			accex.ml = (accex.ml << cnt) |
					(accex.r >> (24 - cnt));
			accex.r <<= cnt;
			acc.o -= cnt;
			goto chk_zero;
		}
		if ((r = acc.r >> 16)) {
			int     cnt, fcnt;
			for (cnt = 0; (r & 0x80) == 0;
						++cnt, r <<= 1);
			acc.ml = acc.r >> (8 - cnt);
			acc.r = (acc.r << (fcnt = 16 + cnt)) |
					(accex.ml << cnt) |
					(accex.r >> (24 - cnt));
			accex.r <<= fcnt;
			acc.o -= fcnt;
			rr = acc.r & ((1l << fcnt) - 1);
			goto chk_zero;
		}
		if ((r = acc.r & 0xffff)) {
			int cnt;
			for (cnt = 0; (r & 0x8000) == 0;
						++cnt, r <<= 1);
			acc.ml = (r & 0xffff) |
					(accex.ml >> (16 - cnt));
			acc.r = (accex.ml << (8 + cnt)) |
					(accex.r >> (16 - cnt));
			accex.ml = accex.r << cnt;
			accex.r = 0;
			acc.o -= 24 + cnt;
			rr = (acc.ml & ((1 << cnt) - 1)) | acc.r;
			goto chk_zero;
		}
		if ((r = accex.ml & 0xffff)) {
			int cnt;
			rr = accex.ml | accex.r;
			for (cnt = 0; (r & 0x8000) == 0;
						++cnt, r <<= 1);
			acc.ml = (r & 0xffff) |
					(accex.r >> (24 - cnt));
			acc.r = (accex.r << cnt);
			accex.ml = accex.r = 0;
			acc.o -= 40 + cnt;
			goto chk_zero;
		}
		if ((r = accex.r >> 16)) {
			int cnt;
			rr = accex.ml | accex.r;
			for (cnt = 0; (r & 0x80) == 0;
						++cnt, r <<= 1);
			acc.ml = accex.r >> (8 - cnt);
			acc.r = accex.r << (16 + cnt);
			accex.ml = accex.r = 0;
			acc.o -= 56 + cnt;
			goto chk_zero;
		}
		if ((r = accex.r & 0xffff)) {
			int cnt;
			rr = accex.ml | accex.r;
			for (cnt = 0; (r & 0x8000) == 0;
						++cnt, r <<= 1);
			acc.ml = (r & 0xffff);
			acc.r = accex.ml = accex.r = 0;
			acc.o -= 64 + cnt;
			goto chk_zero;
		}
		goto zero;
	} else if (i == 3) {
		if ((r = ~acc.ml & 0xffff)) {
			int cnt;
			for (cnt = 0; (r & 0x8000) == 0;
						++cnt, r = (r << 1) | 1);
			acc.ml = 0x10000 | (~r & 0xffff) |
					(acc.r >> (24 - cnt));
			acc.r = (acc.r << cnt) |
					(rr = accex.ml >> (16 - cnt));
			accex.ml = ((accex.ml << cnt) |
					(accex.r >> (24 - cnt)))
					& 0xffff;
			accex.r <<= cnt;
			acc.o -= cnt;
			goto chk_zero;
		}
		if ((r = (~acc.r >> 16) & 0xff)) {
			int     cnt, fcnt;
			for (cnt = 0; (r & 0x80) == 0;
						++cnt, r = (r << 1) | 1);
			acc.ml = 0x10000 | (acc.r >> (8 - cnt));
			acc.r = (acc.r << (fcnt = 16 + cnt)) |
					(accex.ml << cnt) |
					(accex.r >> (24 - cnt));
			accex.ml = ((accex.ml << fcnt) |
					(accex.r >> (8 - cnt)))
					& 0xffff;
			accex.r <<= fcnt;
			acc.o -= fcnt;
			rr = acc.r & ((1l << fcnt) - 1);
			goto chk_zero;
		}
		if ((r = ~acc.r & 0xffff)) {
			int cnt;
			for (cnt = 0; (r & 0x8000) == 0;
						++cnt, r = (r << 1) | 1);
			acc.ml = 0x10000 | (~r & 0xffff) |
					(accex.ml >> (16 - cnt));
			acc.r = (accex.ml << (8 + cnt)) |
					(accex.r >> (16 - cnt));
			accex.ml = (accex.r << cnt) & 0xffff;
			accex.r = 0;
			acc.o -= 24 + cnt;
			rr = (acc.ml & ((1 << cnt) - 1)) | acc.r;
			goto chk_zero;
		}
		if ((r = ~accex.ml & 0xffff)) {
			int cnt;
			rr = accex.ml | accex.r;
			for (cnt = 0; (r & 0x8000) == 0;
						++cnt, r = (r << 1) | 1);
			acc.ml = 0x10000 | (~r & 0xffff) |
					(accex.r >> (24 - cnt));
			acc.r = (accex.r << cnt);
			accex.ml = accex.r = 0;
			acc.o -= 40 + cnt;
			goto chk_zero;
		}
		if ((r = (~accex.r >> 16) & 0xff)) {
			int cnt;
			rr = accex.ml | accex.r;
			for (cnt = 0; (r & 0x80) == 0;
						++cnt, r = (r << 1) | 1);
			acc.ml = 0x10000 | (accex.r >> (8 - cnt));
			acc.r = accex.r << (16 + cnt);
			accex.ml = accex.r = 0;
			acc.o -= 56 + cnt;
			goto chk_zero;
		}
		if ((r = ~accex.r & 0xffff)) {
			int cnt;
			rr = accex.ml | accex.r;
			for (cnt = 0; (r & 0x8000) == 0;
						++cnt, r = (r << 1) | 1);
			acc.ml = 0x10000 | (~r & 0xffff);
			acc.r = accex.ml = accex.r = 0;
			acc.o -= 64 + cnt;
			goto chk_zero;
		} else {
			rr = 1;
			acc.ml = 0x10000;
			acc.r = accex.ml = accex.r = 0;
			acc.o -= 80;
			goto chk_zero;
		}
	}
chk_zero:
	rnd_rq = rnd_rq && !rr;
chk_rnd:
	if (acc.o & 0x8000)
		goto zero;
	if (acc.o & 0x80) {
		acc.o = 0;
		if (! (RAU & RAU_OVF_DISABLE))
			longjmp (cpu_halt, STOP_OVFL);
	}
	if (! (RAU & RAU_ROUND_DISABLE) && rnd_rq)
		acc.r |= 1;

	if (!acc.ml && !acc.r && ! (RAU & RAU_NORM_DISABLE)) {
zero:		acc.l = acc.r = accex.l = accex.r = 0;
		rnd_rq = 0;
		return;
	}
	acc.l = ((uint32) (acc.o & 0x7f) << 17) | (acc.ml & 0x1ffff);
	acc.r = acc.r & 0xffffff;

	accex.l = ((uint32) (accex.o & 0x7f) << 17) | (accex.ml & 0x1ffff);
	accex.r = accex.r & 0xffffff;
	rnd_rq = 0;
}
