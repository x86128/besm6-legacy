/*
 * BESM-6 arithmetic instructions.
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
#include "defs.h"

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
			(from.mr >> 19);\
	to.u.right32 = (from.mr & 0x7ffff) << 13;\
}

int
add()
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
	neg = NEGATIVE(a1);
	if (!diff)
		/*
		accex.o = accex.ml = accex.mr = 0;
		*/      ;
	else if (diff <= 16) {
		int rdiff = 16 - diff;
		/*
		accex.mr = 0;
		*/      ;
		rnd_rq = (accex.ml = (a1.mr << rdiff) & 0xffff) != 0;
		a1.mr = ((a1.mr >> diff) | (a1.ml << (rdiff + 8))) & 0xffffff;
		a1.ml = ((a1.ml >> diff) |
			(neg ? ~0 << rdiff : 0)) & 0x3ffff;
	} else if (diff <= 40) {
		diff -= 16;
		rnd_rq = (accex.mr = (a1.mr << (24 - diff)) & 0xffffff) != 0;
/* было                rnd_rq |= (accex.ml = (((a1.mr & 0xff0000) >> diff) |   */
		rnd_rq |= (accex.ml = ((a1.mr >> diff) |
			(a1.ml << (24 - diff))) & 0xffff) != 0;
		a1.mr = ((((a1.mr >> 16) | (a1.ml << 8)) >> diff) |
				(neg ? (~0l << (24 - diff)) : 0)) & 0xffffff;
		a1.ml = neg ? 0x3ffff : 0;
	} else if (diff <= 56) {
		int rdiff = 16 - (diff -= 40);
		rnd_rq = a1.ml || a1.mr;
		accex.mr = ((a1.mr >> diff) | (a1.ml << (rdiff + 8))) & 0xffffff;
		accex.ml = ((a1.ml >> diff) |
			(neg ? ~0 << rdiff : 0)) & 0xffff;
		if (neg) {
			a1.mr = 0xffffff;
			a1.ml = 0x3ffff;
		} else
			a1.ml = a1.mr = 0;
	} else if (diff <= 80) {
		diff -= 56;
		rnd_rq = a1.ml || a1.mr;
		accex.mr = ((((a1.mr >> 16) | (a1.ml << 8)) >> diff) |
				(neg ? (~0l << (24 - diff)) : 0)) & 0xffffff;
		accex.ml = neg ? 0x3ffff : 0;
		if (neg) {
			a1.mr = 0xffffff;
			accex.ml = a1.ml = 0x3ffff;
		} else
			accex.ml = a1.ml = a1.mr = 0;
	} else {
		rnd_rq = a1.ml || a1.mr;
		if (neg) {
			accex.ml = 0xffff;
			a1.mr = accex.mr = 0xffffff;
			a1.ml = 0x3ffff;
		} else
			accex.ml = accex.mr = a1.ml = a1.mr = 0;
	}
	acc.o = a2.o;
	acc.mr = a1.mr + a2.mr;
	acc.ml = a1.ml + a2.ml + (acc.mr >> 24);
	acc.mr &= 0xffffff;
	return E_SUCCESS;
}

int
aax()
{
	acc.l &= enreg.l;
	acc.r &= enreg.r;
	accex = zeroword;
	return E_SUCCESS;
}

int
aex()
{
	accex = acc;
	acc.l ^= enreg.l;
	acc.r ^= enreg.r;
	return E_SUCCESS;
}

int
arx()
{
	uint            i;

	acc.r = (i = acc.r + enreg.r) & 0xffffff;
	acc.l = (i = acc.l + enreg.l + (i >> 24)) & 0xffffff;

	if (i & 0x1000000) {
		acc.r = (i = acc.r + 1) & 0xffffff;
		acc.l = acc.l + (i >> 24);
	}

	accex = zeroword;
	return E_SUCCESS;
}

int
avx()
{
	if (NEGATIVE(enreg))
		NEGATE(acc);
	return E_SUCCESS;
}

int
aox()
{
	acc.l |= enreg.l;
	acc.r |= enreg.r;
	accex = zeroword;
	return E_SUCCESS;
}

/*
 * non-restoring division
 */
double
nrdiv (double n, double d)
{
	int ne, de, re;
	double nn, dd, res, q, eps;

	nn = frexp(n, &ne);
	dd = frexp(d, &de);

	res = 0, q = 0.5;
	eps = ldexp(q, -40);		/* run for 40 bits of precision */

	if (fabs(nn) >= fabs(dd))
		nn/=2, ne++;

	while (q > eps) {
		if (nn == 0.0)
			break;

		if (fabs(nn) < 0.25)
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
	res = frexp(res, &re);

	return ldexp(res, re+ne-de);
}

int
b6div()
{
#ifdef DIV_NATIVE
	int             neg, o;
	unsigned long   i, c, bias = 0;
	math_t          dividend, divisor, quotient;

	accex.o = accex.ml = accex.mr = 0;
	neg = NEGATIVE(acc) != NEGATIVE(enreg);
	if (NEGATIVE(acc))
		NEGATE(acc);
	if (NEGATIVE(enreg))
		NEGATE(enreg);
	if ((enreg.ml & 0x8000) == 0)
		return E_ZERODIV;
	if ((acc.ml == 0) && (acc.mr == 0)) {
qzero:
		acc = zeroword;
		return E_SUCCESS;
	}
	if ((acc.ml & 0x8000) == 0) {   /* normalize */
		while (acc.ml == 0) {
			if (!acc.mr)
				goto qzero;
			bias += 16;
			acc.ml = acc.mr >> 8;
			acc.mr = (acc.mr & 0xff) << 16;
		}
		for (i = 0x8000, c = 0; (i & acc.ml) == 0; ++c)
			i >>= 1;
		bias += c;
		acc.ml = ((acc.ml << c) | (acc.mr >> (24 - c))) & 0xffff;
		acc.mr = (acc.mr << c) & 0xffffff;
	}

	BESM_TO_IEEE(acc, dividend);
	dividend.u.left32 -= bias << 20;
	BESM_TO_IEEE(enreg, divisor);

	/* quotient.d = dividend.d / divisor.d; */
	quotient.d = nrdiv(dividend.d, divisor.d);

	o = quotient.u.left32 >> 20;
	o = o - 1022 + 64;
	if (o < 0)
		goto qzero;
	acc.o = o & 0x7f;
	acc.ml = ((quotient.u.left32 & 0xfffff) | 0x100000) >> 5;
	acc.mr = ((quotient.u.left32 & 0x1f) << 19) |
			(quotient.u.right32 >> 13);
	if (neg)
		NEGATE(acc);
	if ((o > 0x7f) && !dis_exc)
		return E_OVFL;

#else
#define NEGNORM(R) do { \
	if (NEGATIVE(R)) \
		(R).ml |= 0x20000; \
	(R).mr = (~(R).mr & 0xffffff) + 1; \
	(R).ml = (~(R).ml + ((R).mr >> 24)) & 0x3ffff; \
	(R).mr &= 0xffffff; \
	if ((((R).ml >> 1) ^ (R).ml) & 0x10000) { \
		(R).mr = (((R).mr >> 1) | ((R).ml << 23)) & 0xffffff; \
		(R).ml >>= 1; \
		++(R).o; \
	} else if (!((((R).ml >> 1) ^ (R).ml) & 0x8000)) { \
		(R).ml = ((R).ml << 1 | (R).mr >> 23) & 0x3ffff; \
		(R).mr = ((R).mr << 1) & 0xffffff; \
		--(R).o; \
	} \
	if (NEGATIVE(R)) \
		(R).ml |= 0x20000; \
} while (0)

	alureg_t        a, b[2], bit, r[2];
	register        sgn;
	int             neg;

	accex.o = accex.ml = accex.mr = 0;
	neg = NEGATIVE(acc) != NEGATIVE(enreg);
	a = acc;
	b[0] = b[1] = enreg;
	if (NEGATIVE(enreg))
		NEGNORM(b[1]);
	else
		NEGNORM(b[0]);
	acc.o = acc.o - enreg.o + 64;
	if (b[0].o != b[1].o) {
	    /* the divisor is a power of 2 */
	    if (neg != NEGATIVE(acc))
		NEGNORM(acc);
	    if (!NEGATIVE(enreg))
		acc.o++;
	    return E_SUCCESS;
	}
	if (NEGATIVE(acc))
		NEGNORM(acc);
	a = acc;
	memset(r, 0, sizeof(r));
	bit.ml = 0x10000;
	bit.mr = 0;

	for (sgn = (a.ml >> 16) & 1;;) {
		r[sgn].ml |= bit.ml;
		r[sgn].mr |= bit.mr;
		bit.mr = ((bit.mr >> 1) | (bit.ml << 23)) & 0xffffff;
		bit.ml >>= 1;
		a.mr += b[sgn].mr;
		a.ml = (a.ml + b[sgn].ml + (a.mr >> 24)) & 0x1ffff;
		a.mr &= 0xffffff;
		if (!bit.mr && !bit.ml || !(a.ml & 0xffff) && !a.mr)
			break;
		sgn = (a.ml >> 16) & 1;
		a.ml = ((a.ml << 1) | (a.mr >> 23)) & 0x1ffff;
		a.mr = (a.mr << 1) & 0xffffff;
	}

	acc.ml = r[0].ml - r[1].ml - (r[0].mr < r[1].mr);
	acc.mr = r[0].mr - r[1].mr;
	acc.mr &= 0xffffff;
	acc.ml &= 0x3ffff;

	if (acc.ml >> 16) {
		acc.mr = ((acc.mr >> 1) | (acc.ml << 23)) & 0xffffff;
		acc.ml >>= 1;
		++acc.o;
	}
	if (neg)
		NEGNORM(acc);
#endif
	return E_SUCCESS;
}

int
elfun(int fun)
{
#ifdef DIV_NATIVE
	int             neg = 0, o;
	unsigned long   i, c;
	math_t          arg;

	accex.o = accex.ml = accex.mr = 0;
	UNPCK(acc);
	if (NEGATIVE(acc)) {
		NEGATE(acc);
		neg = 1;
	}
	if ((acc.ml == 0) && (acc.mr == 0)) {
qzero:
		acc = zeroword;
		return E_SUCCESS;
	} else
	if ((acc.ml & 0x8000) == 0) {   /* normalize */
		while (acc.ml == 0) {
			if ((acc.o -= 16) & 0x80)
				goto qzero;
			acc.ml = acc.mr >> 8;
			acc.mr = (acc.mr & 0xff) << 16;
		}
		for (i = 0x8000, c = 0; (i & acc.ml) == 0; ++c)
			i >>= 1;
		if ((acc.o -= c) & 0x80)
			goto qzero;
		acc.ml = ((acc.ml << c) | (acc.mr >> (24 - c))) & 0xffff;
		acc.mr = (acc.mr << c) & 0xffffff;
	}

	BESM_TO_IEEE(acc, arg);

	if (neg) {
		arg.d = -arg.d;
		neg = 0;
	}

	switch (fun) {
	case EF_SQRT:
		arg.d = sqrt(arg.d);
		if (isnan(arg.d)) {
			return E_SQRT;
		}
		break;
	case EF_SIN:
		arg.d = sin(arg.d);
		break;
	case EF_COS:
		arg.d = cos(arg.d);
		break;
	case EF_ARCTG:
		arg.d = atan(arg.d);
		break;
	case EF_ARCSIN:
		arg.d = asin(arg.d);
		if (isnan(arg.d)) {
			return E_ASIN;
		}
		break;
	case EF_ALOG:
		arg.d = log(arg.d);
		if (isnan(arg.d)) {
			return E_ALOG;
		}
		break;
	case EF_EXP:
		arg.d = exp(arg.d);
		if (isinf(arg.d)) {
			return E_EXP;
		}
		break;
	case EF_ENTIER:
		arg.d = floor(arg.d);
		break;
	default:
		return E_INT;
	}

	if ((neg = arg.d < 0.0))
		arg.d *= -1.0;

	o = arg.u.left32 >> 20;
	o = o - 1022 + 64;
	if (o < 0) {
		// biased exponent is negative,
		// flush to zero
		acc = zeroword;
		return E_SUCCESS;
	}
	acc.o = o & 0x7f;
	acc.ml = ((arg.u.left32 & 0xfffff) | 0x100000) >> 5;
	acc.mr = ((arg.u.left32 & 0x1f) << 19) |
			(arg.u.right32 >> 13);
	if (neg)
		NEGATE(acc);
	if ((o > 0x7f) && !dis_exc)
		return E_EXP;	// the only one that can overflow
	PACK(acc)
	return E_SUCCESS;

#else
	return E_UNIMP;
#endif
}

int
mul()
{
	uchar           neg = 0;
	alureg_t        a, b;
	ushort          a1, a2, a3, b1, b2, b3;
	register uint   l;

	a = acc;
	b = enreg;

	if ((!a.l && !a.r) || (!b.l && !b.r)) {
		/* multiplication by zero is zero */
		acc.l = acc.r = acc.o = acc.ml =
		accex.l = accex.r = accex.o = accex.ml = 0;
		rnd_rq = 0;
		return E_SUCCESS;
	}

	if (NEGATIVE(a)) {
		neg = 1;
		NEGATE(a);
	}
	if (NEGATIVE(b)) {
		neg ^= 1;
		NEGATE(b);
	}
	acc.o = a.o + b.o - 64;

	a3 = a.mr & 0xfff;
	a2 = a.mr >> 12;
	a1 = a.ml;

	b3 = b.mr & 0xfff;
	b2 = b.mr >> 12;
	b1 = b.ml;

	accex.mr = (uint) a3 * b3;

	l = (uint) a2 * b3 + (uint) a3 * b2;
	accex.mr += (l << 12) & 0xfff000;
	accex.ml = l >> 12;

	l = (uint) a1 * b3 + (uint) a2 * b2 + (uint) a3 * b1;
	accex.ml += l & 0xffff;
	acc.mr = l >> 16;

	l = (uint) a1 * b2 + (uint) a2 * b1;
	accex.ml += (l & 0xf) << 12;
	acc.mr += (l >> 4) & 0xffffff;
	acc.ml = l >> 28;

	l = (uint) a1 * b1;
	acc.mr += (l & 0xffff) << 8;
	acc.ml += l >> 16;

	accex.ml += accex.mr >> 24;
	acc.mr += accex.ml >> 16;
	acc.ml += acc.mr >> 24;
	accex.mr &= 0xffffff;
	accex.ml &= 0xffff;
	acc.mr &= 0xffffff;
	acc.ml &= 0xffff;

	if (neg) {
		accex.mr = (~accex.mr & 0xffffff) + 1;
		accex.ml = (~accex.ml & 0xffff) + (accex.mr >> 24);
		accex.mr &= 0xffffff;
		acc.mr = (~acc.mr & 0xffffff) + (accex.ml >> 16);
		accex.ml &= 0xffff;
		acc.ml = ((~acc.ml & 0xffff) + (acc.mr >> 24)) | 0x30000;
		acc.mr &= 0xffffff;
	}

	rnd_rq = !!(accex.ml | accex.mr);

	return E_SUCCESS;
}

int
apx()
{
	for (accex.l = accex.r = 0; enreg.r; enreg.r >>= 1, acc.r >>= 1)
		if (enreg.r & 1) {
			accex.r = ((accex.r >> 1) | (accex.l << 23)) & HALFW;
			accex.l >>= 1;
			if (acc.r & 1)
				accex.l |= 0x800000;
		}
	for (; enreg.l; enreg.l >>= 1, acc.l >>= 1)
		if (enreg.l & 1) {
			accex.r = ((accex.r >> 1) | (accex.l << 23)) & HALFW;
			accex.l >>= 1;
			if (acc.l & 1)
				accex.l |= 0x800000;
		}
	acc = accex;
	accex.l = accex.r = 0;
	return E_SUCCESS;
}

int
aux()
{
	int     i;

	accex.l = accex.r = 0;
	for (i = 0; i < 24; ++i) {
		accex.l <<= 1;
		if (enreg.l & 0x800000) {
			if (acc.l & 0x800000)
				accex.l |= 1;
			acc.l = (acc.l << 1) | (acc.r >> 23);
			acc.r = (acc.r << 1) & HALFW;
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
	return E_SUCCESS;
}

int
acx()
{
	int     c = 0;
	uint    i;

	for (i = acc.l; i; i &= i - 1, c++);
	for (i = acc.r; i; i &= i - 1, c++);
	acc.r = c;
	acc.l = 0;
	return arx();
}

int
anx()
{
	uint    c;
	uint    i;
	uchar   b;

	if (acc.l) {
		i = acc.l;
		c = 1;
	} else if (acc.r) {
		i = acc.r;
		c = 25;
	} else {
		acc = enreg;
		accex.l = accex.r = 0;
		return E_SUCCESS;
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

	return E_SUCCESS;
}

int
epx()
{
	acc.o += enreg.o - 64;
	return E_SUCCESS;
}

int
emx()
{
	acc.o += 64 - enreg.o;
	return E_SUCCESS;
}

int
asx()
{
	int     i, j;

	accex.l = accex.r = 0;
	if (!(i = enreg.o - 64))
		return E_SUCCESS;
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

	return E_SUCCESS;
}

int
yta()
{
	if (G_LOG) {
		acc = accex;
		return E_SUCCESS;
	}

	acc.mr = accex.mr;
	acc.l = (accex.l & 0xffff) |
		((acc.l + (enreg.o << 17) - (64 << 17)) & 0x1fe0000);
	if (acc.l & 0x1000000) {
		acc.l &= 0xffffff;
		return E_OVFL;
	}
	return E_SUCCESS;
}

/*
 * Fetch BESM "real" value and return it as native double.
 */
double
fetch_real (int addr)
{
	alureg_t word;
	math_t exponent;
	int64_t mantissa;

	LOAD(word, addr);
	mantissa = ((int64_t) word.l << 24 | word.r) << (64 - 48 + 7);

	exponent.u.left32 = ((word.l >> 17) - 64 + 1023 - 64 + 1) << 20;
	exponent.u.right32 = 0;

	return mantissa * exponent.d;
}
