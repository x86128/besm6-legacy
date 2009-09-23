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

int TLB[32];
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
	{ "TLB0",   &TLB[0],	     8, 7, 0, 1 },
	{ "TLB1",   &TLB[1],	     8, 7, 0, 1 },
	{ "TLB2",   &TLB[2],	     8, 7, 0, 1 },
	{ "TLB3",   &TLB[3],	     8, 7, 0, 1 },
	{ "TLB4",   &TLB[4],	     8, 7, 0, 1 },
	{ "TLB5",   &TLB[5],	     8, 7, 0, 1 },
	{ "TLB6",   &TLB[6],	     8, 7, 0, 1 },
	{ "TLB7",   &TLB[7],	     8, 7, 0, 1 },
	{ "TLB8",   &TLB[8],	     8, 7, 0, 1 },
	{ "TLB9",   &TLB[9],	     8, 7, 0, 1 },
	{ "TLB10",  &TLB[10],	     8, 7, 0, 1 },
	{ "TLB11",  &TLB[11],	     8, 7, 0, 1 },
	{ "TLB12",  &TLB[12],	     8, 7, 0, 1 },
	{ "TLB13",  &TLB[13],	     8, 7, 0, 1 },
	{ "TLB14",  &TLB[14],	     8, 7, 0, 1 },
	{ "TLB15",  &TLB[15],	     8, 7, 0, 1 },
	{ "TLB16",  &TLB[16],	     8, 7, 0, 1 },
	{ "TLB17",  &TLB[17],	     8, 7, 0, 1 },
	{ "TLB18",  &TLB[18],	     8, 7, 0, 1 },
	{ "TLB19",  &TLB[19],	     8, 7, 0, 1 },
	{ "TLB20",  &TLB[20],	     8, 7, 0, 1 },
	{ "TLB21",  &TLB[21],	     8, 7, 0, 1 },
	{ "TLB22",  &TLB[22],	     8, 7, 0, 1 },
	{ "TLB23",  &TLB[23],	     8, 7, 0, 1 },
	{ "TLB24",  &TLB[24],	     8, 7, 0, 1 },
	{ "TLB25",  &TLB[25],	     8, 7, 0, 1 },
	{ "TLB26",  &TLB[26],	     8, 7, 0, 1 },
	{ "TLB27",  &TLB[27],	     8, 7, 0, 1 },
	{ "TLB28",  &TLB[28],	     8, 7, 0, 1 },
	{ "TLB29",  &TLB[29],	     8, 7, 0, 1 },
	{ "TLB30",  &TLB[30],	     8, 7, 0, 1 },
	{ "TLB31",  &TLB[31],	     8, 7, 0, 1 },
	{ "Prot",    &protection,     8, 32, 0, 1},
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
	sim_cancel (&mmu_unit);
	return SCPE_OK;
}

#define loses_to_all(i) ((tourn & win_mask[i] == 0) && (tourn & lose_mask[i]) == lose_mask[i])

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
 * Запись слова в память по ненулевому адресу
 */
void mmu_store (int addr, t_value val)
{
	int i;
	int oldest = -1, matching = -1;

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
		waddr & 01777 | TLB[waddr >> 10] << 10;
		if (waddr > 010) {
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
 * Чтение операнда по ненулевому адресу
 */
t_value mmu_load (int addr)
{
	int i, matching = -1;
	t_value val;

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
		addr & 01777 | TLB[addr >> 10] << 10;
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
		int page = TLB[addr >> 10];
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

