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

typedef struct  {
	unsigned mr;			/* right 24 bits of mantissa */
	unsigned ml;			/* left part of mantissa */
	unsigned o;			/* exponent */
} alureg_t;				/* ALU register type */

#define NEGATIVE(R)     (((R).ml & BIT17) != 0)

static alureg_t zeroword;

/* Требуется округление. */
static int rnd_rq;

static alureg_t toalu (t_value val)
{
        alureg_t ret;

        ret.mr = val & BITS24;
	ret.ml = (val >> 24) & BITS17;
	ret.o = (val >> 41) & BITS7;
	if (ret.ml & BIT17)
		ret.ml |= BIT18;
	return ret;
}

static t_value fromalu (alureg_t reg)
{
        return (t_value) (reg.o & BITS7) << 41 |
		(t_value) (reg.ml & BITS17) << 24 | (reg.mr & BITS24);
}

static alureg_t negate (alureg_t word)
{
	if (NEGATIVE (word))
		word.ml |= 0x20000;
	word.mr = (~word.mr & 0xffffff) + 1;
	word.ml = (~word.ml + (word.mr >> 24)) & 0x3ffff;
	word.mr &= 0xffffff;
	if (((word.ml >> 1) ^ word.ml) & 0x10000) {
		word.mr = ((word.mr >> 1) | (word.ml << 23)) & 0xffffff;
		word.ml >>= 1;
		++word.o;
	}
	if (NEGATIVE (word))
		word.ml |= 0x20000;
	return word;
}

/*
 * Нормализация и округление.
 * Результат помещается в регистры ACC и RMR.
 */
static void normalize_and_round (alureg_t acc, alureg_t rmr)
{
	uint32 rr = 0;
	int i, r;

#if 0
	if (sim_deb && cpu_dev.dctrl) {
		fprintf (sim_deb, "*** До нормализации: СМ=%03o-%06o %08o, РМР=%03o-%06o %08o\n",
			acc.o, acc.ml, acc.mr, rmr.o, rmr.ml, rmr.mr);
	}
#endif
	if (RAU & RAU_NORM_DISABLE)
		goto chk_rnd;
	i = (acc.ml >> 15) & 3;
	if (i == 0) {
		if ((r = acc.ml & 0xffff)) {
			int cnt;
			for (cnt = 0; (r & 0x8000) == 0;
						++cnt, r <<= 1);
			acc.ml = (r & 0xffff) |
					(acc.mr >> (24 - cnt));
			acc.mr = (acc.mr << cnt) |
					(rr = rmr.ml >> (16 - cnt));
			rmr.ml = (rmr.ml << cnt) |
					(rmr.mr >> (24 - cnt));
			rmr.mr <<= cnt;
			acc.o -= cnt;
			goto chk_zero;
		}
		if ((r = acc.mr >> 16)) {
			int     cnt, fcnt;
			for (cnt = 0; (r & 0x80) == 0;
						++cnt, r <<= 1);
			acc.ml = acc.mr >> (8 - cnt);
			acc.mr = (acc.mr << (fcnt = 16 + cnt)) |
					(rmr.ml << cnt) |
					(rmr.mr >> (24 - cnt));
			rmr.mr <<= fcnt;
			acc.o -= fcnt;
			rr = acc.mr & ((1l << fcnt) - 1);
			goto chk_zero;
		}
		if ((r = acc.mr & 0xffff)) {
			int cnt;
			for (cnt = 0; (r & 0x8000) == 0;
						++cnt, r <<= 1);
			acc.ml = (r & 0xffff) |
					(rmr.ml >> (16 - cnt));
			acc.mr = (rmr.ml << (8 + cnt)) |
					(rmr.mr >> (16 - cnt));
			rmr.ml = rmr.mr << cnt;
			rmr.mr = 0;
			acc.o -= 24 + cnt;
			rr = (acc.ml & ((1 << cnt) - 1)) | acc.mr;
			goto chk_zero;
		}
		if ((r = rmr.ml & 0xffff)) {
			int cnt;
			rr = rmr.ml | rmr.mr;
			for (cnt = 0; (r & 0x8000) == 0;
						++cnt, r <<= 1);
			acc.ml = (r & 0xffff) |
					(rmr.mr >> (24 - cnt));
			acc.mr = (rmr.mr << cnt);
			rmr.ml = rmr.mr = 0;
			acc.o -= 40 + cnt;
			goto chk_zero;
		}
		if ((r = rmr.mr >> 16)) {
			int cnt;
			rr = rmr.ml | rmr.mr;
			for (cnt = 0; (r & 0x80) == 0;
						++cnt, r <<= 1);
			acc.ml = rmr.mr >> (8 - cnt);
			acc.mr = rmr.mr << (16 + cnt);
			rmr.ml = rmr.mr = 0;
			acc.o -= 56 + cnt;
			goto chk_zero;
		}
		if ((r = rmr.mr & 0xffff)) {
			int cnt;
			rr = rmr.ml | rmr.mr;
			for (cnt = 0; (r & 0x8000) == 0;
						++cnt, r <<= 1);
			acc.ml = (r & 0xffff);
			acc.mr = rmr.ml = rmr.mr = 0;
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
					(acc.mr >> (24 - cnt));
			acc.mr = (acc.mr << cnt) |
					(rr = rmr.ml >> (16 - cnt));
			rmr.ml = ((rmr.ml << cnt) |
					(rmr.mr >> (24 - cnt)))
					& 0xffff;
			rmr.mr <<= cnt;
			acc.o -= cnt;
			goto chk_zero;
		}
		if ((r = (~acc.mr >> 16) & 0xff)) {
			int     cnt, fcnt;
			for (cnt = 0; (r & 0x80) == 0;
						++cnt, r = (r << 1) | 1);
			acc.ml = 0x10000 | (acc.mr >> (8 - cnt));
			acc.mr = (acc.mr << (fcnt = 16 + cnt)) |
					(rmr.ml << cnt) |
					(rmr.mr >> (24 - cnt));
			rmr.ml = ((rmr.ml << fcnt) |
					(rmr.mr >> (8 - cnt)))
					& 0xffff;
			rmr.mr <<= fcnt;
			acc.o -= fcnt;
			rr = acc.mr & ((1l << fcnt) - 1);
			goto chk_zero;
		}
		if ((r = ~acc.mr & 0xffff)) {
			int cnt;
			for (cnt = 0; (r & 0x8000) == 0;
						++cnt, r = (r << 1) | 1);
			acc.ml = 0x10000 | (~r & 0xffff) |
					(rmr.ml >> (16 - cnt));
			acc.mr = (rmr.ml << (8 + cnt)) |
					(rmr.mr >> (16 - cnt));
			rmr.ml = (rmr.mr << cnt) & 0xffff;
			rmr.mr = 0;
			acc.o -= 24 + cnt;
			rr = (acc.ml & ((1 << cnt) - 1)) | acc.mr;
			goto chk_zero;
		}
		if ((r = ~rmr.ml & 0xffff)) {
			int cnt;
			rr = rmr.ml | rmr.mr;
			for (cnt = 0; (r & 0x8000) == 0;
						++cnt, r = (r << 1) | 1);
			acc.ml = 0x10000 | (~r & 0xffff) |
					(rmr.mr >> (24 - cnt));
			acc.mr = (rmr.mr << cnt);
			rmr.ml = rmr.mr = 0;
			acc.o -= 40 + cnt;
			goto chk_zero;
		}
		if ((r = (~rmr.mr >> 16) & 0xff)) {
			int cnt;
			rr = rmr.ml | rmr.mr;
			for (cnt = 0; (r & 0x80) == 0;
						++cnt, r = (r << 1) | 1);
			acc.ml = 0x10000 | (rmr.mr >> (8 - cnt));
			acc.mr = rmr.mr << (16 + cnt);
			rmr.ml = rmr.mr = 0;
			acc.o -= 56 + cnt;
			goto chk_zero;
		}
		if ((r = ~rmr.mr & 0xffff)) {
			int cnt;
			rr = rmr.ml | rmr.mr;
			for (cnt = 0; (r & 0x8000) == 0;
						++cnt, r = (r << 1) | 1);
			acc.ml = 0x10000 | (~r & 0xffff);
			acc.mr = rmr.ml = rmr.mr = 0;
			acc.o -= 64 + cnt;
			goto chk_zero;
		} else {
			rr = 1;
			acc.ml = 0x10000;
			acc.mr = rmr.ml = rmr.mr = 0;
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
		acc.mr |= 1;

	if (!acc.ml && !acc.mr && ! (RAU & RAU_NORM_DISABLE)) {
zero:		ACC = 0;
		RMR = 0;
		rnd_rq = 0;
		return;
	}
	rnd_rq = 0;

	ACC = fromalu (acc);
	RMR = fromalu (rmr);
#if 0
	if (sim_deb && cpu_dev.dctrl) {
		fprintf (sim_deb, "*** После нормализации: СМ=");
		fprint_sym (sim_deb, 0, &ACC, 0, 0);
		fprintf (sim_deb, ", РМР=");
		fprint_sym (sim_deb, 0, &RMR, 0, 0);
		fprintf (sim_deb, "\n");
	}
#endif
}

/*
 * Сложение и все варианты вычитаний.
 * Исходные значения: регистр ACC и аргумент 'val'.
 * Результат помещается в регистры ACC и RMR.
 */
void besm6_add (t_value val, int negate_acc, int negate_val)
{
	alureg_t acc, word, rmr, a1, a2;
	int diff, neg;

	acc = toalu (ACC);
	word = toalu (val);
	if (! negate_acc) {
		if (! negate_val) {
			/* Сложение */
		} else {
			/* Вычитание */
			word = negate (word);
		}
	} else {
		if (! negate_val) {
			/* Обратное вычитание */
			acc = negate (acc);
		} else {
			/* Вычитание модулей */
			if (NEGATIVE (acc))
				acc = negate (acc);
			if (! NEGATIVE (word))
				word = negate (word);
		}
	}
#if 0
	if (sim_deb && cpu_dev.dctrl) {
		fprintf (sim_deb, "*** Сложение: СМ=%03o-%06o %08o + M[Аисп]=%03o-%06o %08o\n",
			acc.o, acc.ml, acc.mr, word.o, word.ml, word.mr);
	}
#endif
	diff = acc.o - word.o;
	if (diff < 0) {
		diff = -diff;
		a1 = acc;
		a2 = word;
	} else {
		a1 = word;
		a2 = acc;
	}
	rmr.o = rmr.ml = rmr.mr = 0;
	neg = NEGATIVE (a1);
	if (diff == 0) {
		/* Nothing to do. */
	} else if (diff <= 16) {
		int rdiff = 16 - diff;
		rnd_rq = (rmr.ml = (a1.mr << rdiff) & 0xffff) != 0;
		a1.mr = ((a1.mr >> diff) | (a1.ml << (rdiff + 8))) & 0xffffff;
		a1.ml = ((a1.ml >> diff) |
			(neg ? ~0 << rdiff : 0)) & 0x3ffff;
	} else if (diff <= 40) {
		diff -= 16;
		rnd_rq = (rmr.mr = (a1.mr << (24 - diff)) & 0xffffff) != 0;
		rnd_rq |= (rmr.ml = ((a1.mr >> diff) |
			(a1.ml << (24 - diff))) & 0xffff) != 0;
		a1.mr = ((((a1.mr >> 16) | (a1.ml << 8)) >> diff) |
				(neg ? (~0l << (24 - diff)) : 0)) & 0xffffff;
		a1.ml = neg ? 0x3ffff : 0;
	} else if (diff <= 56) {
		int rdiff = 16 - (diff -= 40);
		rnd_rq = a1.ml || a1.mr;
		rmr.mr = ((a1.mr >> diff) | (a1.ml << (rdiff + 8))) & 0xffffff;
		rmr.ml = ((a1.ml >> diff) |
			(neg ? ~0 << rdiff : 0)) & 0xffff;
		if (neg) {
			a1.mr = 0xffffff;
			a1.ml = 0x3ffff;
		} else
			a1.ml = a1.mr = 0;
	} else if (diff <= 80) {
		diff -= 56;
		rnd_rq = a1.ml || a1.mr;
		rmr.mr = ((((a1.mr >> 16) | (a1.ml << 8)) >> diff) |
				(neg ? (~0l << (24 - diff)) : 0)) & 0xffffff;
		rmr.ml = neg ? 0x3ffff : 0;
		if (neg) {
			a1.mr = 0xffffff;
			rmr.ml = a1.ml = 0x3ffff;
		} else
			rmr.ml = a1.ml = a1.mr = 0;
	} else {
		rnd_rq = a1.ml || a1.mr;
		if (neg) {
			rmr.ml = 0xffff;
			a1.mr = rmr.mr = 0xffffff;
			a1.ml = 0x3ffff;
		} else
			rmr.ml = rmr.mr = a1.ml = a1.mr = 0;
	}
	acc.o = a2.o;
	acc.mr = a1.mr + a2.mr;
	acc.ml = a1.ml + a2.ml + (acc.mr >> 24);
	acc.mr &= 0xffffff;

	/* Если требуется нормализация вправо, биты 42:41
	 * принимают значение 01 или 10. */
	switch ((acc.ml >> 16) & 3) {
	case 2:
	case 1:
		rnd_rq |= acc.mr & 1;
		rmr.mr = (rmr.mr >> 1) | (rmr.ml << 23);
		rmr.ml = (rmr.ml >> 1) | (acc.mr << 15);
		acc.mr = (acc.mr >> 1) | (acc.ml << 23);
		acc.ml >>= 1;
		++acc.o;
	}
	normalize_and_round (acc, rmr);
}

/*
 * non-restoring division
 */
static double nrdiv (double n, double d)
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

/*
 * Деление.
 * Исходные значения: регистр ACC и аргумент 'val'.
 * Результат помещается в регистры ACC и RMR.
 */
void besm6_divide (t_value val)
{
	alureg_t acc, word;
	int neg, o;
	unsigned long i, c, bias = 0;
	math_t dividend, divisor, quotient;

	acc = toalu (ACC);
	word = toalu (val);
	RMR = 0;
	neg = NEGATIVE (acc) != NEGATIVE (word);
	if (NEGATIVE (acc))
		acc = negate (acc);
	if (NEGATIVE (word))
		word = negate (word);
	if (! (word.ml & BIT16)) {
		/* Ненормализованный делитель: деление на ноль. */
		longjmp (cpu_halt, STOP_DIVZERO);
	}
	if ((acc.ml == 0) && (acc.mr == 0)) {
qzero:		ACC = 0;
		return;
	}
	if ((acc.ml & 0x8000) == 0) {   /* normalize */
		while (acc.ml == 0) {
			if (! acc.mr)
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

	BESM_TO_IEEE (acc, dividend);
	dividend.u.left32 -= bias << 20;
	BESM_TO_IEEE (word, divisor);

	/* quotient.d = dividend.d / divisor.d; */
	quotient.d = nrdiv (dividend.d, divisor.d);

	o = quotient.u.left32 >> 20;
	o = o - 1022 + 64;
	if (o < 0)
		goto qzero;
	acc.o = o & 0x7f;
	acc.ml = ((quotient.u.left32 & 0xfffff) | 0x100000) >> 5;
	acc.mr = ((quotient.u.left32 & 0x1f) << 19) |
			(quotient.u.right32 >> 13);
	if (neg)
		acc = negate (acc);
	normalize_and_round (acc, zeroword);

	if ((o > 0x7f) && ! (RAU & RAU_OVF_DISABLE))
		longjmp (cpu_halt, STOP_OVFL);
}

/*
 * Умножение.
 * Исходные значения: регистр ACC и аргумент 'val'.
 * Результат помещается в регистры ACC и RMR.
 */
void besm6_multiply (t_value val)
{
	uint8           neg = 0;
	alureg_t        acc, word, rmr, a, b;
	uint16          a1, a2, a3, b1, b2, b3;
	register uint32 l;

	if (! ACC || ! val) {
		/* multiplication by zero is zero */
		ACC = 0;
		RMR = 0;
		rnd_rq = 0;
		return;
	}
	acc = toalu (ACC);
	word = toalu (val);

	a = acc;
	b = word;
	rmr = zeroword;

	if (NEGATIVE (a)) {
		neg = 1;
		a = negate (a);
	}
	if (NEGATIVE (b)) {
		neg ^= 1;
		b = negate (b);
	}
	acc.o = a.o + b.o - 64;

	a3 = a.mr & 0xfff;
	a2 = a.mr >> 12;
	a1 = a.ml;

	b3 = b.mr & 0xfff;
	b2 = b.mr >> 12;
	b1 = b.ml;

	rmr.mr = (uint32) a3 * b3;

	l = (uint32) a2 * b3 + (uint32) a3 * b2;
	rmr.mr += (l << 12) & 0xfff000;
	rmr.ml = l >> 12;

	l = (uint32) a1 * b3 + (uint32) a2 * b2 + (uint32) a3 * b1;
	rmr.ml += l & 0xffff;
	acc.mr = l >> 16;

	l = (uint32) a1 * b2 + (uint32) a2 * b1;
	rmr.ml += (l & 0xf) << 12;
	acc.mr += (l >> 4) & 0xffffff;
	acc.ml = l >> 28;

	l = (uint32) a1 * b1;
	acc.mr += (l & 0xffff) << 8;
	acc.ml += l >> 16;

	rmr.ml += rmr.mr >> 24;
	acc.mr += rmr.ml >> 16;
	acc.ml += acc.mr >> 24;
	rmr.mr &= 0xffffff;
	rmr.ml &= 0xffff;
	acc.mr &= 0xffffff;
	acc.ml &= 0xffff;

	if (neg) {
		rmr.mr = (~rmr.mr & 0xffffff) + 1;
		rmr.ml = (~rmr.ml & 0xffff) + (rmr.mr >> 24);
		rmr.mr &= 0xffffff;
		acc.mr = (~acc.mr & 0xffffff) + (rmr.ml >> 16);
		rmr.ml &= 0xffff;
		acc.ml = ((~acc.ml & 0xffff) + (acc.mr >> 24)) | 0x30000;
		acc.mr &= 0xffffff;
	}

	rnd_rq = !!(rmr.ml | rmr.mr);

	normalize_and_round (acc, rmr);
}

/*
 * Изменение знака числа на сумматоре ACC.
 * Результат помещается в регистры ACC и RMR.
 */
void besm6_change_sign (int negate_acc)
{
	alureg_t acc;

	acc = toalu (ACC);
	if (negate_acc)
		acc = negate (acc);
	normalize_and_round (acc, zeroword);
}

/*
 * Изменение порядка числа на сумматоре ACC.
 * Результат помещается в регистры ACC и RMR.
 */
void besm6_add_exponent (int val)
{
	alureg_t acc;

	acc = toalu (ACC);
	acc.o += val;
	normalize_and_round (acc, zeroword);
}

/*
 * Сборка значения по маске.
 */
t_value besm6_pack (t_value val, t_value mask)
{
	t_value result;

	result = 0;
	for (; mask; mask>>=1, val>>=1)
		if (mask & 1) {
			result >>= 1;
			if (val & 1)
				result |= BIT48;
		}
	return result;
}

/*
 * Разборка значения по маске.
 */
t_value besm6_unpack (t_value val, t_value mask)
{
	t_value result;
	int i;

	result = 0;
	for (i=0; i<48; ++i) {
		result <<= 1;
		if (mask & BIT48) {
			if (val & BIT48)
				result |= 1;
			val <<= 1;
		}
		mask <<= 1;
	}
	return result;
}

/*
 * Подсчёт количества единиц в слове.
 */
int besm6_count_ones (t_value word)
{
	int c;

	for (c=0; word; ++c)
		word &= word-1;
	return c;
}

/*
 * Вычисление номера старшей единицы сумматора АСС, слева направо, нумеруя с единицы.
 * 48-й разряд сумматора соответствует результату "1" и так далее.
 * К номеру старшей единицы циклически прибавляется слово по Аисп.
 * В регистр младших разрядов помещается исходное значение, придвинутое влево,
 * без старшего бита.
 */
void besm6_highest_bit (t_value val)
{
	static const int highest_of_six_bits [64] = {
		0,6,5,5,4,4,4,4,3,3,3,3,3,3,3,3,
		2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
		1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
		1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	};
	int n;

	if (! ACC) {
		/* Нулевой сумматор, возвращаем 0. */
		ACC = val;
		RMR = 0;
		return;
	}
	if (ACC >> 24 & BITS24) {
		if (ACC >> 36 & BITS12) {
			if (ACC >> 42 & BITS6) {
				n = highest_of_six_bits [ACC >> 42 & BITS6];
			} else {
				n = 6 + highest_of_six_bits [ACC >> 36 & BITS6];
			}
		} else {
			if (ACC >> 30 & BITS6) {
				n = 12 + highest_of_six_bits [ACC >> 30 & BITS6];
			} else {
				n = 18 + highest_of_six_bits [ACC >> 24 & BITS6];
			}
		}
	} else {
		if (ACC >> 12 & BITS12) {
			if (ACC >> 18 & BITS6) {
				n = 24 + highest_of_six_bits [ACC >> 18 & BITS6];
			} else {
				n = 30 + highest_of_six_bits [ACC >> 12 & BITS6];
			}
		} else {
			if (ACC >> 6 & BITS6) {
				n = 36 + highest_of_six_bits [ACC >> 6 & BITS6];
			} else {
				n = 42 + highest_of_six_bits [ACC & BITS6];
			}
		}
	}
	/* Вычисляем РМР: отрезаем старший бит и все левее. */
	besm6_shift (48 - n);

	/* Циклическое сложение номера старшей единицы с словом по Аисп. */
	ACC = n + val;
	if (ACC & BIT49)
		ACC = (ACC + 1) & BITS48;
}

/*
 * Сдвиг сумматора ACC с выдвижением в регистр младших разрядов RMR.
 */
void besm6_shift (int i)
{
	RMR = 0;
	if (i > 0) {
		/* Сдвиг вправо. */
		if (i < 48) {
			RMR = (ACC << (48-i)) & BITS48;
			ACC >>= i;
		} else {
			if (i < 96)
				RMR = ACC >> (i-48);
			ACC = 0;
		}
	} else if (i < 0) {
		/* Сдвиг влево. */
		i = -i;
		if (i < 48) {
			RMR = ACC >> (48-i);
			ACC = (ACC << i) & BITS48;
		} else {
			if (i < 96)
				RMR = (ACC << (i-48)) & BITS48;
			ACC = 0;
		}
	}
}
