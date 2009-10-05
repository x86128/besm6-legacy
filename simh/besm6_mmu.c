/*
 * besm6_mmu.c: BESM-6 fast write cache and TLB registers
 *（стойка БРУС)
 *
 * Copyright (c) 2009, Leonid Broukhis
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
#include "besm6_defs.h"

/*
 * MMU data structures
 *
 * mmu_dev	MMU device descriptor
 * mmu_unit	MMU unit descriptor
 * mmu_reg	MMU register list
 */
UNIT mmu_unit = {
	UDATA (NULL, UNIT_FIX, 8)
};

t_value BRZ[8];
uint32 BAZ[8], TABST, RZ;

/*
 * 64-битные регистры RP0-RP7 - для отображения регистров приписки,
 * группами по 4 ради компактности, 12 бит на страницу.
 * TLB0-TLB31 - постраничные регистры приписки, копии RPi.
 * Обращение к памяти должно вестись через TLBi.
 */
t_value RP[8];
uint32 TLB[32];

unsigned iintr_data;	/* protected page number or parity check location */

t_value pult[8];

REG mmu_reg[] = {
{ "БРЗ0",  &BRZ[0],	8, 50, 0, 1, REG_VMIO},	/* Буферные регистры записи */
{ "БРЗ1",  &BRZ[1],	8, 50, 0, 1, REG_VMIO},
{ "БРЗ2",  &BRZ[2],	8, 50, 0, 1, REG_VMIO},
{ "БРЗ3",  &BRZ[3],	8, 50, 0, 1, REG_VMIO},
{ "БРЗ4",  &BRZ[4],	8, 50, 0, 1, REG_VMIO},
{ "БРЗ5",  &BRZ[5],	8, 50, 0, 1, REG_VMIO},
{ "БРЗ6",  &BRZ[6],	8, 50, 0, 1, REG_VMIO},
{ "БРЗ7",  &BRZ[7],	8, 50, 0, 1, REG_VMIO},
{ "БАЗ0",  &BAZ[0],	8, 16, 0, 1 },		/* Буферные адреса записи */
{ "БАЗ1",  &BAZ[1],	8, 16, 0, 1 },
{ "БАЗ2",  &BAZ[2],	8, 16, 0, 1 },
{ "БАЗ3",  &BAZ[3],	8, 16, 0, 1 },
{ "БАЗ4",  &BAZ[4],	8, 16, 0, 1 },
{ "БАЗ5",  &BAZ[5],	8, 16, 0, 1 },
{ "БАЗ6",  &BAZ[6],	8, 16, 0, 1 },
{ "БАЗ7",  &BAZ[7],	8, 16, 0, 1 },
{ "ТАБСТ", &TABST,	8, 28, 0, 1 },		/* Таблица старшинства БРЗ */
{ "РП0",   &RP[0],	8, 48, 0, 1, REG_VMIO},	/* Регистры приписки, по 12 бит */
{ "РП1",   &RP[1],	8, 48, 0, 1, REG_VMIO},
{ "РП2",   &RP[2],	8, 48, 0, 1, REG_VMIO},
{ "РП3",   &RP[3],	8, 48, 0, 1, REG_VMIO},
{ "РП4",   &RP[4],	8, 48, 0, 1, REG_VMIO},
{ "РП5",   &RP[5],	8, 48, 0, 1, REG_VMIO},
{ "РП6",   &RP[6],	8, 48, 0, 1, REG_VMIO},
{ "РП7",   &RP[7],	8, 48, 0, 1, REG_VMIO},
{ "РЗ",    &RZ,		8, 32, 0, 1 },		/* Регистр защиты */
{ "ТР1",   &pult[1],	8, 50, 0, 1, REG_VMIO},	/* Тумблерные регистры */
{ "ТР2",   &pult[2],	8, 50, 0, 1, REG_VMIO},
{ "ТР3",   &pult[3],	8, 50, 0, 1, REG_VMIO},
{ "ТР4",   &pult[4],	8, 50, 0, 1, REG_VMIO},
{ "ТР5",   &pult[5],	8, 50, 0, 1, REG_VMIO},
{ "ТР6",   &pult[6],	8, 50, 0, 1, REG_VMIO},
{ "ТР7",   &pult[7],	8, 50, 0, 1, REG_VMIO},
{ 0 }
};

MTAB mmu_mod[] = {
	{ 0 }
};

t_stat mmu_reset (DEVICE *dptr);

DEVICE mmu_dev = {
	"MMU", &mmu_unit, mmu_reg, mmu_mod,
	1, 8, 3, 1, 8, 50,
	NULL, NULL, &mmu_reset,
	NULL, NULL, NULL, NULL,
	DEV_DEBUG
};

/*
 * Reset routine
 */
t_stat mmu_reset (DEVICE *dptr)
{
	int i;
	for (i = 0; i < 8; ++i) {
		BRZ[i] = BAZ[i] = RP[i] = 0;
	}
	TABST = 0;
	RZ = 0;
	/*
	 * Front panel switches survive the reset
	 */
	sim_cancel (&mmu_unit);
	return SCPE_OK;
}

#define loses_to_all(i) ((TABST & win_mask[i]) == 0 && \
			(TABST & lose_mask[i]) == lose_mask[i])

/*
 * N wins over M if the bit is set
 *  M=1   2   3   4   5   6   7
 * N  -------------------------
 * 0| 0   1   2   3   4   5   6
 * 1|     7   8   9  10  11  12
 * 2|        13  14  15  16  17
 * 3|            18  19  20  21
 * 4|                22  23  24
 * 5|                    25  26
 * 6|                        27
 */

static unsigned win_mask[8] = {
	0177,
	0077 << 7,
	0037 << 13,
	0017 << 18,
	0007 << 22,
	0003 << 25,
	0001 << 27,
	0
};

static unsigned lose_mask[8] = {
	0,
	1<<0,
	1<<1|1<<7,
	1<<2|1<<8|1<<13,
	1<<3|1<<9|1<<14|1<<18,
	1<<4|1<<10|1<<15|1<<19|1<<22,
	1<<5|1<<11|1<<16|1<<20|1<<23|1<<25,
	1<<6|1<<12|1<<17|1<<21|1<<24|1<<26|1<<27
};

#define set_wins(i) TABST = (TABST & ~lose_mask[i]) | win_mask[i]

void mmu_protection_check (int addr)
{
	/* Защита блокируется в режиме супервизора для адресов 1-7 */
	int tmp_prot_disabled = (M[PSW] & PSW_PROT_DISABLE) ||
		(IS_SUPERVISOR (RUU) && addr < 010);

	/* Защита не заблокирована, а лист закрыт */
	if (! tmp_prot_disabled && (RZ & (1 << (addr >> 10)))) {
		iintr_data = addr >> 10;
		if (mmu_dev.dctrl)
			besm6_debug ("--- %05o: защита числа", addr);
		longjmp (cpu_halt, STOP_OPERAND_PROT);
	}
}

/*
 * Запись слова в память
 */
void mmu_store (int addr, t_value val)
{
	int i;
	int oldest = -1, matching = -1;

	addr &= BITS15;
	if (addr == 0)
		return;
	if (sim_log && mmu_dev.dctrl)
		fprintf (sim_log, "--- %05o: запись %016llo\n", addr, val);

	mmu_protection_check (addr);

	/* Различаем адреса с припиской и без */
	if (M[PSW] & PSW_MMAP_DISABLE)
		addr |= 0100000;
	for (i = 0; i < 8; ++i) {
		if (loses_to_all (i)) oldest = i;
		if (addr == BAZ[i]) {
			matching = i;
		}
	}

	/* Совпадение адресов не фиксируется для тумблерных регистров */
	if (addr > 0100000 && addr < 0100010)
		matching = -1;

	if (matching < 0) {
		/* Вычисляем физический адрес выталкиваемого БРЗ */
		int waddr = BAZ[oldest];
		waddr = waddr > 0100000 ? waddr - 0100000 :
		waddr & 01777 | TLB[waddr >> 10] << 10;
		if (waddr >= 010) {
			/* В ноль и тумблерные регистры не пишем */
			memory[waddr] = BRZ[oldest];
		}
		matching = oldest;
		BAZ[oldest] = addr;
	}
	BRZ[matching] = CONVOL_NUMBER (val, RUU);
	set_wins (matching);
}

/*
 * Чтение операнда
 */
t_value mmu_load (int addr)
{
	int i, matching = -1;
	t_value val;

	addr &= BITS15;
	if (addr == 0)
		return 0;

	mmu_protection_check (addr);

	/* Различаем адреса с припиской и без */
	if (M[PSW] & PSW_MMAP_DISABLE)
		addr |= 0100000;
	for (i = 0; i < 8; ++i) {
		if (addr == BAZ[i]) {
			matching = i;
		}
	}

	if (matching == -1) {
		/* Вычисляем физический адрес слова */
		addr = addr > 0100000 ? addr - 0100000 :
		addr & 01777 | TLB[addr >> 10] << 10;
		if (addr >= 010) {
			/* Из памяти */
			val = memory[addr];
		} else {
			/* С тумблерных регистров */
			val = pult[addr];
		}
		if (sim_log && mmu_dev.dctrl)
			fprintf (sim_log, "--- %05o: чтение %016llo\n",
				addr & BITS15, val & WORD);

		/* На тумблерных регистрах контроля числа не бывает */
		if (addr >= 010 && ! IS_NUMBER (val)) {
			iintr_data = addr & 7;
			besm6_debug ("--- %05o: контроль числа", addr);
			longjmp (cpu_halt, STOP_RAM_CHECK);
		}
	} else {
		val = BRZ[matching];
		if (sim_log && mmu_dev.dctrl)
			fprintf (sim_log, "--- %05o: чтение %016llo из БРЗ\n",
				addr & BITS15, val & WORD);
		if (! IS_NUMBER (val)) {
			iintr_data = matching;
			besm6_debug ("--- %05o: контроль числа БРЗ", addr);
			longjmp (cpu_halt, STOP_CACHE_CHECK);
		}
	}
	return val & WORD;
}

/*
 * Выборка команды
 */
t_value mmu_fetch (int addr)
{
	t_value val;

	if (addr == 0) {
		/* В режиме супервизора слово 0 - команда,
		 * в режиме пользователя - число
		 */
		if (IS_SUPERVISOR (RUU))
			return 0;
		if (mmu_dev.dctrl)
			besm6_debug ("--- передача управления на 0");
		longjmp (cpu_halt, STOP_INSN_CHECK);
	}

	/* В режиме супервизора защиты нет */
	if (IS_SUPERVISOR (RUU)) {
		if (addr < 010)
			val = pult [addr];
		else
			val = memory [addr];
	} else {
		int page = TLB[addr >> 10];
		/*
		 * Для команд в режиме пользователя признак защиты -
		 * 0 в регистре приписки.
		 */
		if (page == 0) {
			iintr_data = addr >> 10;
			if (mmu_dev.dctrl)
				besm6_debug ("--- %05o: защита команды", addr);
			longjmp (cpu_halt, STOP_INSN_PROT);
		}

		/* Вычисляем физический адрес слова */
		addr = addr & 01777 | page << 10;

		val = memory[addr];
	}
	if (sim_log && mmu_dev.dctrl)
		fprintf (sim_log, "--- %05o: выборка %016llo\n",
			addr, val & WORD);

	/* Тумблерные регистры пока только с командной сверткой */
	if (addr >= 010 && ! IS_INSN (val)) {
		besm6_debug ("--- %05o: контроль команды", addr);
		longjmp (cpu_halt, STOP_INSN_CHECK);
	}
	return val & WORD;
}

void mmu_setrp (int idx, t_value val)
{
	uint32 p0, p1, p2, p3;

	/* Младшие 5 разрядов 4-х регистров приписки упакованы
	 * по 5 в 1-20 рр, 6-е разряды - в 29-32 рр, 7-е разряды - в 33-36 рр
	 */
	p0 = val & 037 | ((val>>28)&1)<<5 | ((val>>32)&1)<<6;
	p1 = (val>>5) & 037 | ((val>>29)&1)<<5 | ((val>>33)&1)<<6;
	p2 = (val>>10) & 037 | ((val>>30)&1)<<5 | ((val>>34)&1)<<6;
	p3 = (val>>15) & 037 | ((val>>31)&1)<<5 | ((val>>35)&1)<<6;
	RP[idx] = p0 | p1 << 12 | p2 << 24 | (t_value) p3 << 36;
	TLB[idx*4] = p0;
	TLB[idx*4+1] = p1;
	TLB[idx*4+2] = p2;
	TLB[idx*4+3] = p3;
}

void mmu_setup ()
{
	int i;

	/* Перепись РПi в TLBj. */
	for (i=0; i<8; ++i) {
		TLB[i*4] = RP[i] & 0177;
		TLB[i*4+1] = RP[i] >> 12 & 0177;
		TLB[i*4+2] = RP[i] >> 24 & 0177;
		TLB[i*4+3] = RP[i] >> 36 & 0177;
	}
}

void mmu_setprotection (int idx, t_value val)
{
	/* Разряды сумматора, записываемые в регистр защиты - 21-28 */
	int mask = 0xff << (idx * 8);
	val = ((val >> 20) & 0xff) << (idx * 8);
	RZ = (RZ & ~mask) | val;
}

void mmu_setcache (int idx, t_value val)
{
	BRZ[idx] = CONVOL_NUMBER (val, RUU);
}

t_value mmu_getcache (int idx)
{
	return BRZ[idx];
}

void mmu_print_brz ()
{
	int i, k;

	for (i=7; i>=0; --i) {
		besm6_log_cont ("БРЗ [%d] = '", i);
		for (k=47; k>=0; --k)
			besm6_log_cont ("%c", (BRZ[i] >> k & 1) ? '*' : ' ');
		besm6_log ("'");
	}
}
