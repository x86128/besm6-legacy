/*
 * BESM-6 arithmetic instructions.
 *
 * Copyright (c) 1997-2009, Leonid Broukhis
 * Copyright (c) 2009, Serge Vakulenko
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

typedef struct {
	t_uint64 mantissa;
	unsigned exponent;		/* offset by 64 */
} alureg_t;				/* ALU register type */

static alureg_t zeroword;

static alureg_t ieee_to_alu (double d)
{
	alureg_t res;
	t_value word;
	int exponent;
	int sign;

	sign = d < 0;
	if (sign)
		d = -d;
	d = frexp (d, &exponent);
	/* 0.5 <= d < 1.0 */
	d = ldexp (d, 40);
	word = d;
	if (sign)
		word = BIT42 - word;
	res.mantissa = word;
	res.exponent = exponent + 64;
	return res;
}

static alureg_t toalu (t_value val)
{
	alureg_t ret;

	ret.mantissa = val & BITS41;
	ret.exponent = (val >> 41) & BITS7;
	if (ret.mantissa & BIT41)
		ret.mantissa |= BIT41<<1;
	return ret;
}

static t_value fromalu (alureg_t reg)
{
	return (t_value) (reg.exponent & BITS7) << 41 |
		(t_value) (reg.mantissa & BITS41);
}

static int inline is_negative (alureg_t word)
{
	return (word.mantissa & BIT41) != 0;
}

static alureg_t negate (alureg_t word)
{
	if (is_negative (word))
		word.mantissa |= BIT42;
	word.mantissa = (~word.mantissa + 1) & BITS42;
	if (((word.mantissa >> 1) ^ word.mantissa) & BIT41) {
		word.mantissa >>= 1;
		++word.exponent;
	}
	if (is_negative (word))
		word.mantissa |= BIT42;
	return word;
}

/*
 * Нормализация и округление.
 * Результат помещается в регистры ACC и RMR.
 */
static void normalize_and_round (alureg_t acc, alureg_t rmr, int rnd_rq)
{
	t_uint64 rr = 0;
	int i;
	t_uint64 r;

	if (RAU & RAU_NORM_DISABLE)
		goto chk_rnd;
	i = (acc.mantissa >> 39) & 3;
	if (i == 0) {
		if ((r = acc.mantissa & BITS40)) {
			int cnt;
			for (cnt = 0; (r & BIT40) == 0;
						++cnt, r <<= 1);
			acc.mantissa = r | (rr = rmr.mantissa >> (40 - cnt));
		        /* 41 р. РМР может быть равен 1? */
			rmr.mantissa <<= cnt;
			acc.exponent -= cnt;
			goto chk_zero;
		}
		if ((r = rmr.mantissa & BITS40)) {
			int cnt;
			rr = rmr.mantissa;
			for (cnt = 0; (r & BIT40) == 0;
						++cnt, r <<= 1);
			acc.mantissa = r;
			rmr.mantissa = 0;
			acc.exponent -= 40 + cnt;
			goto chk_zero;
		}
		goto zero;
	} else if (i == 3) {
		if ((r = ~acc.mantissa & BITS40)) {
			int cnt;
			for (cnt = 0; (r & BIT40) == 0;
						++cnt, r = (r << 1) | 1);
			acc.mantissa = BIT41 | (~r & BITS40) |
					(rr = rmr.mantissa >> (40 - cnt));
		        /* 41 р. РМР не может быть равен 1? */
			rmr.mantissa = (rmr.mantissa << cnt) & BITS40;
			acc.exponent -= cnt;
			goto chk_zero;
		}
		if ((r = ~rmr.mantissa & BITS40)) {
			int cnt;
			rr = rmr.mantissa;
			for (cnt = 0; (r & BIT40) == 0;
						++cnt, r = (r << 1) | 1);
			acc.mantissa = BIT41 | (~r & BITS40); 
			rmr.mantissa = 0;
			acc.exponent -= 40 + cnt;
			goto chk_zero;
		} else {
			rr = 1;
			acc.mantissa = BIT41;
			rmr.mantissa = 0;
			acc.exponent -= 80;
			goto chk_zero;
		}
	}
chk_zero:
	rnd_rq = rnd_rq && !rr;
chk_rnd:
	if (acc.exponent & 0x8000)
		goto zero;
	if (acc.exponent & 0x80) {
		acc.exponent = 0;
		if (! (RAU & RAU_OVF_DISABLE))
			longjmp (cpu_halt, STOP_OVFL);
	}
	if (! (RAU & RAU_ROUND_DISABLE) && rnd_rq)
		acc.mantissa |= 1;

	if (!acc.mantissa && ! (RAU & RAU_NORM_DISABLE)) {
zero:		ACC = 0;
		RMR = 0;
		return;
	}

	ACC = fromalu (acc);
	RMR = fromalu (rmr);
}

/*
 * Сложение и все варианты вычитаний.
 * Исходные значения: регистр ACC и аргумент 'val'.
 * Результат помещается в регистры ACC и RMR.
 */
void besm6_add (t_value val, int negate_acc, int negate_val)
{
	alureg_t acc, word, rmr, a1, a2;
	int diff, neg, rnd_rq = 0;

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
			if (is_negative (acc))
				acc = negate (acc);
			if (! is_negative (word))
				word = negate (word);
		}
	}

	diff = acc.exponent - word.exponent;
	if (diff < 0) {
		diff = -diff;
		a1 = acc;
		a2 = word;
	} else {
		a1 = word;
		a2 = acc;
	}
	rmr.exponent = rmr.mantissa = 0;
	neg = is_negative (a1);
	if (diff == 0) {
		/* Nothing to do. */
	} else if (diff <= 40) {
		rnd_rq = (rmr.mantissa = (a1.mantissa << (40 - diff)) & BITS40) != 0;
		a1.mantissa = ((a1.mantissa >> diff) |
				(neg ? (~0ll << (40 - diff)) : 0)) & BITS42;
	} else if (diff <= 80) {
		diff -= 40;
		rnd_rq = a1.mantissa != 0;
		rmr.mantissa = ((a1.mantissa >> diff) |
				(neg ? (~0ll << (40 - diff)) : 0)) & BITS40;
		if (neg) {
			a1.mantissa = BITS42;
		} else
			a1.mantissa = 0;
	} else {
		rnd_rq = a1.mantissa != 0;
		if (neg) {
			rmr.mantissa = BITS40;
			a1.mantissa = BITS42;
		} else
			rmr.mantissa = a1.mantissa = 0;
	}
	acc.exponent = a2.exponent;
	acc.mantissa = a1.mantissa + a2.mantissa;

	/* Если требуется нормализация вправо, биты 42:41
	 * принимают значение 01 или 10. */
	switch ((acc.mantissa >> 40) & 3) {
	case 2:
	case 1:
		rnd_rq |= acc.mantissa & 1;
		rmr.mantissa = (rmr.mantissa >> 1) | ((acc.mantissa & 1) << 39);
		acc.mantissa >>= 1;
		++acc.exponent;
	}
	normalize_and_round (acc, rmr, rnd_rq);
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
	double dividend, divisor, quotient;

	if (((val ^ (val << 1)) & BIT41) == 0) {
		/* Ненормализованный делитель: деление на ноль. */
		longjmp (cpu_halt, STOP_DIVZERO);
	}
	dividend = besm6_to_ieee(ACC);
	divisor = besm6_to_ieee(val);

	quotient = nrdiv(dividend, divisor);
	acc = ieee_to_alu(quotient);

	if ((o = acc.exponent) < 0) {
	    ACC = 0;
	    return;
	}
	acc.exponent = o & 0x7f;

	normalize_and_round (acc, zeroword, 0);

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
	t_uint64	alo, blo, ahi, bhi;

	register t_uint64 l;

	if (! ACC || ! val) {
		/* multiplication by zero is zero */
		ACC = 0;
		RMR = 0;
		return;
	}
	acc = toalu (ACC);
	word = toalu (val);

	a = acc;
	b = word;
	rmr.mantissa = rmr.exponent = 0;

	if (is_negative (a)) {
		neg = 1;
		a = negate (a);
	}
	if (is_negative (b)) {
		neg ^= 1;
		b = negate (b);
	}
	acc.exponent = a.exponent + b.exponent - 64;

	alo = a.mantissa & BITS20;
	ahi = a.mantissa >> 20;

	blo = b.mantissa & BITS20;
	bhi = b.mantissa >> 20;

	l = alo * blo + ((alo * bhi + ahi * blo) << 20);

	rmr.mantissa = l & BITS40;
	l >>= 40;

	acc.mantissa = l + ahi * bhi;

	if (neg) {
		rmr.mantissa = (~rmr.mantissa & BITS40) + 1;
		acc.mantissa = ((~acc.mantissa & BITS40) + (rmr.mantissa >> 40))
		    | BIT41 | BIT42;
		rmr.mantissa &= BITS40;
	}

	normalize_and_round (acc, rmr, rmr.mantissa != 0);
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
	normalize_and_round (acc, zeroword, 0);
}

/*
 * Изменение порядка числа на сумматоре ACC.
 * Результат помещается в регистры ACC и RMR.
 */
void besm6_add_exponent (int val)
{
	alureg_t acc;

	acc = toalu (ACC);
	acc.exponent += val;
	normalize_and_round (acc, zeroword, 0);
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
