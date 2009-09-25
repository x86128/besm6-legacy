/*
 * besm6_mmu.c: BESM-6 fast write cache and TLB registers
 *（стойка БРУС)
 *
 * Copyright (c) 2009, Leonid Broukhis
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
int BAZ[8];
unsigned tourn;

/*
 * 64-битные регистры - для отображения регистров приписки
 * группами по 4 ради компактности (порядок зависит от архитектуры хост-машины).
 * Вся работа должна вестись через TLB.val.
 */
union { 
	uint16 val[32];
	t_uint64 RP[8];
} TLB;
 
unsigned protection;

unsigned iintr_data;	/* protected page number or parity check location */

t_value pult[8];

REG mmu_reg[] = {
	{ "BRZ0",   &BRZ[0],         8, 50, 0, 1 },
	{ "BRZ1",   &BRZ[1],         8, 50, 0, 1 },
	{ "BRZ2",   &BRZ[2],         8, 50, 0, 1 },
	{ "BRZ3",   &BRZ[3],         8, 50, 0, 1 },
	{ "BRZ4",   &BRZ[4],         8, 50, 0, 1 },
	{ "BRZ5",   &BRZ[5],         8, 50, 0, 1 },
	{ "BRZ6",   &BRZ[6],         8, 50, 0, 1 },
	{ "BRZ7",   &BRZ[7],         8, 50, 0, 1 },
	{ "BAZ0",   &BAZ[0],         8, 16, 0, 1 },
	{ "BAZ1",   &BAZ[1],         8, 16, 0, 1 },
	{ "BAZ2",   &BAZ[2],         8, 16, 0, 1 },
	{ "BAZ3",   &BAZ[3],         8, 16, 0, 1 },
	{ "BAZ4",   &BAZ[4],         8, 16, 0, 1 },
	{ "BAZ5",   &BAZ[5],         8, 16, 0, 1 },
	{ "BAZ6",   &BAZ[6],         8, 16, 0, 1 },
	{ "BAZ7",   &BAZ[7],         8, 16, 0, 1 },
	{ "Tourn",  &tourn,	     8, 28, 0, 1 },
	{ "RP0",    &TLB.RP[0],	     8, 64, 0, 1 },
	{ "RP1",    &TLB.RP[1],	     8, 64, 0, 1 },
	{ "RP2",    &TLB.RP[2],	     8, 64, 0, 1 },
	{ "RP3",    &TLB.RP[3],	     8, 64, 0, 1 },
	{ "RP4",    &TLB.RP[4],	     8, 64, 0, 1 },
	{ "RP5",    &TLB.RP[5],	     8, 64, 0, 1 },
	{ "RP6",    &TLB.RP[6],	     8, 64, 0, 1 },
	{ "RP7",    &TLB.RP[7],	     8, 64, 0, 1 },
	{ "Prot",   &protection,     8, 32, 0, 1},
	{ "FP1",   &pult[1],         8, 50, 0, 1 },
	{ "FP2",   &pult[2],         8, 50, 0, 1 },
	{ "FP3",   &pult[3],         8, 50, 0, 1 },
	{ "FP4",   &pult[4],         8, 50, 0, 1 },
	{ "FP5",   &pult[5],         8, 50, 0, 1 },
	{ "FP6",   &pult[6],         8, 50, 0, 1 },
	{ "FP7",   &pult[7],         8, 50, 0, 1 },
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
		BRZ[i] = BAZ[i] = 0;
	}
	tourn = 0;
	for (i = 0; i < 32; ++i) {
		TLB.val[i] = 0;
	}
	protection = 0;
	/*
	 * Front panel switches survive the reset
	 */
	sim_cancel (&mmu_unit);
	return SCPE_OK;
}

#define loses_to_all(i) ((tourn & win_mask[i]) == 0 && (tourn & lose_mask[i]) == lose_mask[i])

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

#define set_wins(i) tourn = tourn & ~lose_mask[i] | win_mask[i]

void mmu_protection_check(int addr) {
	/* Защита блокируется в режиме супервизора для адресов 1-7 */
	int tmp_prot_disabled = prot_disabled || (supmode && addr < 010);

	/* Защита не заблокирована, а лист закрыт */
	if (!tmp_prot_disabled && (protection & (1 << (addr >> 10)))) {
		iintr_data = addr >> 10;
		longjmp(cpu_halt, STOP_OPERAND_PROT);
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

	mmu_protection_check(addr);

	/* Различаем адреса с припиской и без */
	addr |= sup_mmap << 15;
	for (i = 0; i < 8; ++i) {
		if (loses_to_all(i)) oldest = i;
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
		waddr & 01777 | TLB.val[waddr >> 10] << 10;
		if (waddr >= 010) {
			/* В ноль и тумблерные регистры не пишем */
			memory[waddr] = BRZ[oldest];
		}
		matching = oldest;
		BAZ[oldest] = addr;
	}
	BRZ[matching] = SET_CONVOL(val, convol_mode);
	set_wins(matching);
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

	mmu_protection_check(addr);

	/* Различаем адреса с припиской и без */
	addr |= sup_mmap << 15;
	for (i = 0; i < 8; ++i) {
		if (addr == BAZ[i]) {
			matching = i;
		}
	}

	if (matching == -1) {
		/* Вычисляем физический адрес слова */
		addr = addr > 0100000 ? addr - 0100000 :
		addr & 01777 | TLB.val[addr >> 10] << 10;
		if (addr >= 010) {
			/* Из памяти */
			val = memory[addr];
		} else {
			/* С тумблерных регистров */
			val = pult[addr];
		}
	
		if (!IS_NUMBER(val)) {
			iintr_data = addr & 7;
			longjmp(cpu_halt, STOP_RAM_CHECK);
		}
	} else {
		val = BRZ[matching];
		if (!IS_NUMBER(val)) {
			iintr_data = matching;
			longjmp(cpu_halt, STOP_CACHE_CHECK);
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
		if (supmode)
			return 0;
		longjmp(cpu_halt, STOP_INSN_CHECK);
	}

	/* В режиме супервизора защиты нет */
	if (supmode) {
		val = memory[addr];
	} else {
		int page = TLB.val[addr >> 10];
		/* 
		 * Для команд в режиме пользователя признак защиты -
		 * 0 в регистре приписки.
		 */
		if (page == 0) {
			iintr_data = addr >> 10;
			longjmp(cpu_halt, STOP_INSN_PROT);
		}

		/* Вычисляем физический адрес слова */
		addr = addr & 01777 | page << 10;

		val = memory[addr];
	}

	if (!IS_INSN(val))
		longjmp(cpu_halt, STOP_INSN_CHECK);

	return val & WORD;
}

void mmu_settlb(int idx, t_value val) {
	TLB.val[idx*4] = val & 037 | (val>>20)&1<<5 | (val>>24)&1<<6;
	TLB.val[idx*4+1] = (val>>5) & 037 | (val>>21)&1<<5 | (val>>25)&1<<6;
	TLB.val[idx*4+2] = (val>>10) & 037 | (val>>22)&1<<5 | (val>>26)&1<<6;
	TLB.val[idx*4+3] = (val>>15) & 037 | (val>>23)&1<<5 | (val>>27)&1<<6;
}

void mmu_setprotection(int idx, t_value val) {
	/* Разряды сумматора, записываемые в регистр защиты - 21-28 */
	int mask = 0xff << (idx * 8);
	val = ((val >> 20) & 0xff) << (idx * 8);
	protection = protection & ~mask | val;
}

void mmu_setcache(int idx, t_value val) {
	BRZ[idx] =  SET_CONVOL(val, convol_mode);
}

t_value mmu_getcache(int idx) {
	return BRZ[idx];
}
