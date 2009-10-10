/*
 * besm6_cpu.c: BESM-6 CPU simulator
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
 *
 * For more information about BESM-6 computer, visit sites:
 *  - http://www.computer-museum.ru/english/besm6.htm
 *  - http://mailcom.com/besm6/
 *  - http://groups.google.com/group/besm6
 *
 * Release notes for BESM-6/SIMH
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *  1) All addresses and data values are displayed in octal.
 *  2) Memory size is 128 kwords.
 *  3) Interrupt system is to be implemented.
 *  4) Execution times are in 1/10 of microsecond.
 *  5) Magnetic drums are implemented as a single "DRUM" device.
 *  6) Magnetic disks are planned.
 *  7) Magnetic tape is not implemented.
 *  8) Punch reader is planned.
 *  9) Card puncher is not implemented.
 * 10) Displays are planned.
 * 11) Printers are planned.
 * 12) Instruction mnemonics, register names and stop messages
 *     are in Russian using UTF-8 encoding. It is assumed, that
 *     user locale is UTF-8.
 * 13) A lot of comments in Russian (UTF-8).
 */
#include "besm6_defs.h"
#include <math.h>
#include <float.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

/*
 * Регистр 027: сохранённые режимы УУ.
 * PSW: saved program status word.
 */
#define SPSW_MMAP_DISABLE	000001	/* БлП - блокировка приписки */
#define SPSW_PROT_DISABLE	000002	/* БлЗ - блокировка защиты */
#define SPSW_EXTRACODE		000004	/* РежЭ - режим экстракода */
#define SPSW_INTERRUPT		000010	/* РежПр - режим прерывания */
#define SPSW_MOD_RK		000020	/* ПрИК(РК) - на регистр РК принята
					   команда, которая должна быть
					   модифицирована регистром М[16] */
#define SPSW_MOD_RR		000040	/* ПрИК(РР) - на регистре РР находится
					   команда, выполненная с модификацией */
#define SPSW_UNKNOWN		000100	/* НОК? вписано карандашом в 9 томе */
#define SPSW_RIGHT_INSTR	000400	/* ПрК - признак правой команды */
#define SPSW_NEXT_RK		001000	/* ГД./ДК2 - на регистр РК принята
					   команда, следующая после вызвавшей
					   прерывание */
#define SPSW_INTR_DISABLE	002000	/* БлПр - блокировка внешнего прерывания */

t_value memory [MEMSIZE];
uint32 PC, RK, Aex, M [NREGS], RAU, RUU;
t_value ACC, RMR, GRP, MGRP;
uint32 PRP, MPRP;
uint32 READY; /* ready flags of various devices */

extern const char *scp_error_messages[];

/* нехранящие биты ГРП должны сбрасываться путем обнуления тех регистров,
 * сборкой которых они являются
 */
#define GRP_WIRED_BITS 01400743700000000LL

#define PRP_WIRED_BITS 0	/* unknown? */

/* после каждого изменения PRP или MPRP нужно выполнять PRP2GRP */
#define PRP2GRP	do if (PRP&MPRP) GRP |= GRP_SLAVE; \
		 else GRP &= ~GRP_SLAVE; while (0)

int corr_stack;
uint32 delay;
jmp_buf cpu_halt;

t_stat cpu_examine (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_deposit (t_value val, t_addr addr, UNIT *uptr, int32 sw);
t_stat cpu_reset (DEVICE *dptr);

/*
 * CPU data structures
 *
 * cpu_dev      CPU device descriptor
 * cpu_unit     CPU unit descriptor
 * cpu_reg      CPU register list
 * cpu_mod      CPU modifiers list
 */

UNIT cpu_unit = { UDATA (NULL, UNIT_FIX, MEMSIZE) };

REG cpu_reg[] = {
{ "СчАС",  &PC,		8, 15, 0, 1 },		/* счётчик адреса команды */
{ "РК",    &RK,		8, 24, 0, 1 },		/* регистр выполняемой команды */
{ "Аисп",  &Aex,	8, 15, 0, 1 },		/* исполнительный адрес */
{ "СМ",    &ACC,	8, 48, 0, 1, REG_VMIO},	/* сумматор */
{ "РМР",   &RMR,	8, 48, 0, 1, REG_VMIO},	/* регистр младших разрядов */
{ "РАУ",   &RAU,	2, 6,  0, 1 },		/* режимы АУ */
{ "М1",    &M[1],	8, 15, 0, 1 },		/* регистры-модификаторы */
{ "М2",    &M[2],	8, 15, 0, 1 },
{ "М3",    &M[3],	8, 15, 0, 1 },
{ "М4",    &M[4],	8, 15, 0, 1 },
{ "М5",    &M[5],	8, 15, 0, 1 },
{ "М6",    &M[6],	8, 15, 0, 1 },
{ "М7",    &M[7],	8, 15, 0, 1 },
{ "М10",   &M[010],	8, 15, 0, 1 },
{ "М11",   &M[011],	8, 15, 0, 1 },
{ "М12",   &M[012],	8, 15, 0, 1 },
{ "М13",   &M[013],	8, 15, 0, 1 },
{ "М14",   &M[014],	8, 15, 0, 1 },
{ "М15",   &M[015],	8, 15, 0, 1 },
{ "М16",   &M[016],	8, 15, 0, 1 },
{ "М17",   &M[017],	8, 15, 0, 1 },		/* указатель магазина */
{ "М20",   &M[020],	8, 15, 0, 1 },		/* MOD - модификатор адреса */
{ "М21",   &M[021],	8, 15, 0, 1 },		/* PSW - режимы УУ */
{ "М27",   &M[027],	8, 15, 0, 1 },		/* SPSW - упрятывание режимов УУ */
{ "М32",   &M[032],	8, 15, 0, 1 },		/* ERET - адрес возврата из экстракода */
{ "М33",   &M[033],	8, 15, 0, 1 },		/* IRET - адрес возврата из прерывания */
{ "М34",   &M[034],	8, 16, 0, 1 },		/* IBP - адрес прерывания по выполнению */
{ "М35",   &M[035],	8, 16, 0, 1 },		/* DWP - адрес прерывания по чтению/записи */
{ "РУУ",   &RUU,	2, 9,  0, 1 },		/* ПКП, ПКЛ, РежЭ, РежПр, ПрИК, БРО, ПрК */
{ "ГРП",   &GRP,	8, 48, 0, 1, REG_VMIO},	/* главный регистр прерываний */
{ "МГРП",  &MGRP,	8, 48, 0, 1, REG_VMIO},	/* маска ГРП */
{ 0 }
};

MTAB cpu_mod[] = {
	{ 0 }
};

DEVICE cpu_dev = {
	"CPU", &cpu_unit, cpu_reg, cpu_mod,
	1, 8, 17, 1, 8, 50,
	&cpu_examine, &cpu_deposit, &cpu_reset,
	NULL, NULL, NULL, NULL,
	DEV_DEBUG
};

/*
 * REG: псевдоустройство, содержащее латинские синонимы всех регистров.
 */
REG reg_reg[] = {
{ "PC",    &PC,		8, 15, 0, 1 },		/* счётчик адреса команды */
{ "RK",    &RK,		8, 24, 0, 1 },		/* регистр выполняемой команды */
{ "Aex",   &Aex,	8, 15, 0, 1 },		/* исполнительный адрес */
{ "ACC",   &ACC,	8, 48, 0, 1, REG_VMIO}, /* сумматор */
{ "RMR",   &RMR,	8, 48, 0, 1, REG_VMIO}, /* регистр младших разрядов */
{ "RAU",   &RAU,	2, 6,  0, 1 },		/* режимы АУ */
{ "M1",    &M[1],	8, 15, 0, 1 },		/* регистры-модификаторы */
{ "M2",    &M[2],	8, 15, 0, 1 },
{ "M3",    &M[3],	8, 15, 0, 1 },
{ "M4",    &M[4],	8, 15, 0, 1 },
{ "M5",    &M[5],	8, 15, 0, 1 },
{ "M6",    &M[6],	8, 15, 0, 1 },
{ "M7",    &M[7],	8, 15, 0, 1 },
{ "M10",   &M[010],	8, 15, 0, 1 },
{ "M11",   &M[011],	8, 15, 0, 1 },
{ "M12",   &M[012],	8, 15, 0, 1 },
{ "M13",   &M[013],	8, 15, 0, 1 },
{ "M14",   &M[014],	8, 15, 0, 1 },
{ "M15",   &M[015],	8, 15, 0, 1 },
{ "M16",   &M[016],	8, 15, 0, 1 },
{ "M17",   &M[017],	8, 15, 0, 1 },		/* указатель магазина */
{ "M20",   &M[020],	8, 15, 0, 1 },		/* MOD - модификатор адреса */
{ "M21",   &M[021],	8, 15, 0, 1 },		/* PSW - режимы УУ */
{ "M27",   &M[027],	8, 15, 0, 1 },		/* SPSW - упрятывание режимов УУ */
{ "M32",   &M[032],	8, 15, 0, 1 },		/* ERET - адрес возврата из экстракода */
{ "M33",   &M[033],	8, 15, 0, 1 },		/* IRET - адрес возврата из прерывания */
{ "M34",   &M[034],	8, 16, 0, 1 },		/* IBP - адрес прерывания по выполнению */
{ "M35",   &M[035],	8, 16, 0, 1 },		/* DWP - адрес прерывания по чтению/записи */
{ "RUU",   &RUU,        2, 9,  0, 1 },		/* ПКП, ПКЛ, РежЭ, РежПр, ПрИК, БРО, ПрК */
{ "GRP",   &GRP,	8, 48, 0, 1, REG_VMIO},	/* главный регистр прерываний */
{ "MGRP",  &MGRP,	8, 48, 0, 1, REG_VMIO},	/* маска ГРП */

{ "BRZ0",  &BRZ[0],	8, 50, 0, 1, REG_VMIO },
{ "BRZ1",  &BRZ[1],	8, 50, 0, 1, REG_VMIO },
{ "BRZ2",  &BRZ[2],	8, 50, 0, 1, REG_VMIO },
{ "BRZ3",  &BRZ[3],	8, 50, 0, 1, REG_VMIO },
{ "BRZ4",  &BRZ[4],	8, 50, 0, 1, REG_VMIO },
{ "BRZ5",  &BRZ[5],	8, 50, 0, 1, REG_VMIO },
{ "BRZ6",  &BRZ[6],	8, 50, 0, 1, REG_VMIO },
{ "BRZ7",  &BRZ[7],	8, 50, 0, 1, REG_VMIO },
{ "BAZ0",  &BAZ[0],	8, 16, 0, 1 },
{ "BAZ1",  &BAZ[1],	8, 16, 0, 1 },
{ "BAZ2",  &BAZ[2],	8, 16, 0, 1 },
{ "BAZ3",  &BAZ[3],	8, 16, 0, 1 },
{ "BAZ4",  &BAZ[4],	8, 16, 0, 1 },
{ "BAZ5",  &BAZ[5],	8, 16, 0, 1 },
{ "BAZ6",  &BAZ[6],	8, 16, 0, 1 },
{ "BAZ7",  &BAZ[7],	8, 16, 0, 1 },
{ "TABST", &TABST,	8, 28, 0, 1 },
{ "RP0",   &RP[0],	8, 48, 0, 1, REG_VMIO },
{ "RP1",   &RP[1],	8, 48, 0, 1, REG_VMIO },
{ "RP2",   &RP[2],	8, 48, 0, 1, REG_VMIO },
{ "RP3",   &RP[3],	8, 48, 0, 1, REG_VMIO },
{ "RP4",   &RP[4],	8, 48, 0, 1, REG_VMIO },
{ "RP5",   &RP[5],	8, 48, 0, 1, REG_VMIO },
{ "RP6",   &RP[6],	8, 48, 0, 1, REG_VMIO },
{ "RP7",   &RP[7],	8, 48, 0, 1, REG_VMIO },
{ "RZ",    &RZ,		8, 32, 0, 1 },
{ "FP1",   &pult[1],	8, 50, 0, 1, REG_VMIO },
{ "FP2",   &pult[2],	8, 50, 0, 1, REG_VMIO },
{ "FP3",   &pult[3],	8, 50, 0, 1, REG_VMIO },
{ "FP4",   &pult[4],	8, 50, 0, 1, REG_VMIO },
{ "FP5",   &pult[5],	8, 50, 0, 1, REG_VMIO },
{ "FP6",   &pult[6],	8, 50, 0, 1, REG_VMIO },
{ "FP7",   &pult[7],	8, 50, 0, 1, REG_VMIO },
{ 0 }
};

UNIT reg_unit = {
	UDATA (NULL, UNIT_FIX, 8)
};

DEVICE reg_dev = {
	"REG", &reg_unit, reg_reg, NULL,
	1, 8, 1, 1, 8, 50,
};

/*
 * SCP data structures and interface routines
 *
 * sim_name		simulator name string
 * sim_PC		pointer to saved PC register descriptor
 * sim_emax		maximum number of words for examine/deposit
 * sim_devices		array of pointers to simulated devices
 * sim_stop_messages	array of pointers to stop messages
 * sim_load		binary loader
 */

char sim_name[] = "БЭСМ-6";

REG *sim_PC = &cpu_reg[0];

int32 sim_emax = 1;	/* максимальное количество слов в машинной команде */

DEVICE *sim_devices[] = {
	&cpu_dev,
	&reg_dev,
	&drum_dev,
	&mmu_dev,
	&clock_dev,
	&printer_dev,
	0
};

const char *sim_stop_messages[] = {
	"Неизвестная ошибка",				/* Unknown error */
	"Останов",					/* STOP */
	"Точка останова",				/* Emulator breakpoint */
	"Выход за пределы памяти",			/* Run out end of memory */
	"Неверный код команды",				/* Invalid instruction */
	"Контроль команды",				/* A data-tagged word fetched */
        "Команда в чужом листе",			/* Paging error during fetch */
        "Число в чужом листе",				/* Paging error during load/store */
        "Контроль числа МОЗУ",				/* RAM parity error */
        "Контроль числа БРЗ",				/* Write cache parity error */
	"Переполнение АУ",				/* Arith. overflow */
	"Деление на нуль",				/* Division by zero or denorm */
	"Двойное внутреннее прерывание",		/* SIMH: Double internal interrupt */
	"Чтение неформатированного барабана",		/* Reading unformatted drum */
	"Останов по КРА",				/* Hardware breakpoint */
	"Останов по считыванию",			/* Load watchpoint */
	"Останов по записи",				/* Store watchpoint */
};

/*
 * Memory examine
 */
t_stat cpu_examine (t_value *vptr, t_addr addr, UNIT *uptr, int32 sw)
{
	if (addr >= MEMSIZE)
		return SCPE_NXM;
	if (vptr) {
		if (addr < 010)
			*vptr = pult [addr];
		else
			*vptr = memory [addr];
	}
	return SCPE_OK;
}

/*
 * Memory deposit
 */
t_stat cpu_deposit (t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
	if (addr >= MEMSIZE)
		return SCPE_NXM;
	if (addr < 010)
		pult [addr] = SET_CONVOL (val, CONVOL_INSN);
	else
		memory [addr] = SET_CONVOL (val, CONVOL_INSN);
	return SCPE_OK;
}

/*
 * Reset routine
 */
t_stat cpu_reset (DEVICE *dptr)
{
	int i;

	ACC = 0;
	RMR = 0;
	RAU = 0;
	RUU = RUU_EXTRACODE | RUU_AVOST_DISABLE;
	for (i=0; i<NREGS; ++i)
		M[i] = 0;

	/* Регистр 17: БлП, БлЗ, ПОП, ПОК, БлПр */
	M[PSW] = PSW_MMAP_DISABLE | PSW_PROT_DISABLE | PSW_INTR_HALT |
		PSW_CHECK_HALT | PSW_INTR_DISABLE;

	/* Регистр 23: БлП, БлЗ, РежЭ, БлПр */
	M[SPSW] = SPSW_MMAP_DISABLE | SPSW_PROT_DISABLE | SPSW_EXTRACODE |
		SPSW_INTR_DISABLE;

	GRP = MGRP = 0;
	sim_brk_types = sim_brk_dflt = SWMASK ('E');
	return SCPE_OK;
}

/*
 * Write Unicode symbol to file.
 * Convert to UTF-8 encoding:
 * 00000000.0xxxxxxx -> 0xxxxxxx
 * 00000xxx.xxyyyyyy -> 110xxxxx, 10yyyyyy
 * xxxxyyyy.yyzzzzzz -> 1110xxxx, 10yyyyyy, 10zzzzzz
 */
void
utf8_putc (unsigned ch, FILE *fout)
{
	if (ch < 0x80) {
		putc (ch, fout);
		return;
	}
	if (ch < 0x800) {
		putc (ch >> 6 | 0xc0, fout);
		putc ((ch & 0x3f) | 0x80, fout);
		return;
	}
	putc (ch >> 12 | 0xe0, fout);
	putc (((ch >> 6) & 0x3f) | 0x80, fout);
	putc ((ch & 0x3f) | 0x80, fout);
}

static void cmd_002 ()
{
	switch (Aex & 0377) {
	case 0 ... 7:
		mmu_setcache (Aex & 7, ACC);
		break;
	case 020 ... 027:
		/* Запись в регистры приписки */
		mmu_setrp (Aex & 7, ACC);
		break;
	case 030 ... 033:
		/* Запись в регистры защиты */
		mmu_setprotection (Aex & 3, ACC);
		break;
	case 036:
		/* Запись в маску главного регистра прерываний */
		MGRP = ACC;
		break;
	case 037:
		/* Гашение главного регистра прерываний */
		/* нехранящие биты невозможно погасить */
		GRP &= ACC | GRP_WIRED_BITS;
		break;
	case 0100 ... 0137:
		/* Бит 1: управление блокировкой режима останова БРО.
		 * Биты 2 и 3 - признаки формирования контрольных
		 * разрядов (ПКП и ПКЛ). */
		if (Aex & 1)
			RUU |= RUU_AVOST_DISABLE;
		else
			RUU &= ~RUU_AVOST_DISABLE;
		if (Aex & 2)
			RUU |= RUU_CONVOL_RIGHT;
		else
			RUU &= ~RUU_CONVOL_RIGHT;
		if (Aex & 4)
			RUU |= RUU_CONVOL_LEFT;
		else
			RUU &= ~RUU_CONVOL_LEFT;
		break;
	case 0140 ... 0177:
		/* TODO: управление блокировкой схемы
		 * автоматического запуска */
		longjmp (cpu_halt, STOP_STOP);
		break;
	case 0200 ... 0207:
		/* Чтение БРЗ */
		ACC = mmu_getcache (Aex & 7);
		break;
	case 0237:
		/* Чтение главного регистра прерываний */
		ACC = GRP;
		break;
	default:
		/* Неиспользуемые адреса */
		besm6_debug ("*** %05o%s: РЕГ %o - неправильный адрес спец.регистра",
			PC, (RUU & RUU_RIGHT_INSTR) ? "п" : "л", Aex);
		break;
	}
}

static void cmd_033 ()
{
	switch (Aex & 04177) {
	case 1 ... 2:
		/* Управление обменом с магнитными барабанами */
		drum (Aex - 1, (uint32) ACC);
		break;
	case 3 ... 7:
		/* TODO: управление обменом с магнитными лентами */
		longjmp (cpu_halt, STOP_STOP);
		break;
	case 010 ... 013:
		/* TODO: управление устройствами ввода с перфоленты */
		longjmp (cpu_halt, STOP_STOP);
		break;
	case 014 ... 015:
		/* управление АЦПУ */
		printer_control (Aex - 014, (uint32) (ACC & 017));
		break;
	case 030:
		/* гашение ПРП */
		PRP &= ACC | PRP_WIRED_BITS;
		PRP2GRP;
		break;
	case 031:
		/* имитация сигналов прерывания ГРП (уточнить) */
		GRP |= GRP_IMITATION;
		break;
	case 032 ... 033:
		/* TODO: имитация сигналов из КМБ в КВУ */
		longjmp (cpu_halt, STOP_STOP);
		break;
	case 034:
		/* запись в МПРП */
		MPRP = ACC & 077777777;
		PRP2GRP;
		break;
	case 035:
		/* TODO: управление режимом имитации обмена
		 * с МБ и МЛ, имитация обмена */
		longjmp (cpu_halt, STOP_STOP);
		break;
	case 040 ... 057:
		/* управление молоточками АЦПУ */
		printer_hammer (Aex >= 050, Aex & 7, (uint32) (ACC & BITS16));
		break;
	case 0100 ... 0137:
		/* TODO: управление лентопротяжными механизмами
		 * и гашение разрядов регистров признаков
		 * окончания подвода зоны */
		longjmp (cpu_halt, STOP_STOP);
		break;
	case 0140:
		/* запись в регистр телеграфных каналов */
		tty_send((uint32) ACC & BITS24);
		break;
	case 0141:
		/* TODO: управление разметкой магнитной ленты */
		longjmp (cpu_halt, STOP_STOP);
		break;
	case 0142:
		/* TODO: имитация сигналов прерывания ПРП */
		longjmp (cpu_halt, STOP_STOP);
		break;
	case 0147:
		/* запись в регистр управления электропитанием */
		/* не оказывает видимого эффекта на выполнение */
		break;
	case 0150 ... 0151:
		/* TODO: управление вводом с перфокарт */
		longjmp (cpu_halt, STOP_STOP);
		break;
	case 0154 ... 0155:
		/* TODO: управление выводом на перфокарты */
		longjmp (cpu_halt, STOP_STOP);
		break;
	case 0160 ... 0167:
		/* TODO: управление электромагнитами пробивки перфокарт */
		longjmp (cpu_halt, STOP_STOP);
		break;
	case 0170 ... 0171:
		/* TODO: пробивка строки на перфоленте */
		longjmp (cpu_halt, STOP_STOP);
		break;
	case 0174:
		/* TODO: выдача кода в пульт оператора */
		longjmp (cpu_halt, STOP_STOP);
		break;
	case 04001 ... 04003:
		/* TODO: считывание слога в режиме имитации обмена */
		longjmp (cpu_halt, STOP_STOP);
		break;
	case 04006:
		/* TODO: считывание строки с устройства ввода
		 * с перфоленты в запаянной программе */
		longjmp (cpu_halt, STOP_STOP);
		break;
	case 04007:
		/* TODO: опрос синхроимпульса ненулевой строки
		 * в запаянной программе ввода с перфоленты */
		longjmp (cpu_halt, STOP_STOP);
		break;
	case 04014 ... 04017:
		/* TODO: считывание строки с устройства
		 * ввода с перфоленты */
		longjmp (cpu_halt, STOP_STOP);
		break;
	case 04020 ... 04023:
		/* TODO: считывание слога в режиме имитации
		 * внешнего обмена */
		longjmp (cpu_halt, STOP_STOP);
		break;
	case 04030:
		/* чтение старшей половины ПРП */
		ACC = PRP & 077770000;
		break;
	case 04031:
		/* опрос сигналов готовности (АЦПУ и пр.) */
		ACC = READY;
		break;
	case 04034:
		/* чтение младшей половины ПРП */
		ACC = (PRP & 07777) | 0377;
		break;
	case 04035:
		/* TODO: опрос схем контроля внешнего обмена */
		longjmp (cpu_halt, STOP_STOP);
		break;
	case 04100:
		/* TODO: опрос телеграфных каналов связи */
		longjmp (cpu_halt, STOP_STOP);
		break;
	case 04102:
		/* TODO: опрос сигналов готовности
		 * перфокарт и перфолент */
		longjmp (cpu_halt, STOP_STOP);
		break;
	case 04103 ... 04106:
		/* TODO: опрос состояния лентопротяжных механизмов */
		longjmp (cpu_halt, STOP_STOP);
		break;
	case 04107:
		/* TODO: опрос схемы контроля записи на МЛ */
		longjmp (cpu_halt, STOP_STOP);
		break;
	case 04140 ... 04157:
		/* TODO: считывание строки перфокарты */
		longjmp (cpu_halt, STOP_STOP);
		break;
	case 04160 ... 04167:
		/* TODO: контрольное считывание строки перфокарты */
		longjmp (cpu_halt, STOP_STOP);
		break;
	case 04170 ... 04173:
		/* TODO: считывание контрольного кода
		 * строки перфоленты */
		longjmp (cpu_halt, STOP_STOP);
		break;
	case 04174:
		/* TODO: считывание кода с пульта оператора */
		longjmp (cpu_halt, STOP_STOP);
		break;
	}
}

/*
 * Execute one instruction, placed on address PC:RUU_RIGHT_INSTR.
 * Increment delay. When stopped, perform a longjmp to cpu_halt,
 * sending a stop code.
 */
void cpu_one_inst ()
{
	int reg, opcode, addr, nextpc, nextaddrmod = 0;

	corr_stack = 0;
	t_value word = mmu_fetch (PC);
	if (RUU & RUU_RIGHT_INSTR)
		RK = word;		/* get right instruction */
	else
		RK = word >> 24;	/* get left instruction */

	RK &= BITS24;

	reg = RK >> 20;
	if (RK & BIT20) {
		addr = RK & BITS15;
		opcode = (RK >> 12) & 0370;
	} else {
		addr = RK & BITS12;
		if (RK & BIT19)
			addr |= 070000;
		opcode = (RK >> 12) & 077;
	}

	if (sim_deb && cpu_dev.dctrl) {
		fprintf (sim_deb, "*** %05o%s: ", PC,
			(RUU & RUU_RIGHT_INSTR) ? "п" : "л");
		besm6_fprint_cmd (sim_deb, RK);
		fprintf (sim_deb, "\tСМ=");
		fprint_sym (sim_deb, 0, &ACC, 0, 0);
		fprintf (sim_deb, "\tРАУ=%02o\n", RAU);
	}
	nextpc = ADDR(PC + 1);
	if (RUU & RUU_RIGHT_INSTR) {
		PC += 1;			/* increment PC */
		RUU &= ~RUU_RIGHT_INSTR;
	} else {
		mmu_prefetch(nextpc | (IS_SUPERVISOR(RUU) ? BIT16 : 0), 0);
		RUU |= RUU_RIGHT_INSTR;
	}

	if (RUU & RUU_MOD_RK) {
                addr = ADDR (addr + M[MOD]);
        }

	delay = 0;

	switch (opcode) {
	case 000:					/* зп, atx */
		Aex = ADDR (addr + M[reg]);
		mmu_store (Aex, ACC);
		if (! addr && reg == 017)
			M[017] = ADDR (M[017] + 1);
		break;
	case 001:					/* зпм, stx */
		Aex = ADDR (addr + M[reg]);
		mmu_store (Aex, ACC);
		M[017] = ADDR (M[017] - 1);
		corr_stack = 1;
		ACC = mmu_load (M[017]);
		RAU = SET_LOGICAL (RAU);
		break;
	case 002:					/* рег, mod */
		Aex = ADDR (addr + M[reg]);
		if (! IS_SUPERVISOR (RUU))
			longjmp (cpu_halt, STOP_BADCMD);
		cmd_002 ();
		/* Режим АУ - логический, если операция была "чтение" */
		if (Aex & 0200)
			RAU = SET_LOGICAL (RAU);
		delay = MEAN_TIME (3, 3);
		break;
	case 003:					/* счм, xts */
		mmu_store (M[017], ACC);
		M[017] = ADDR (M[017] + 1);
		corr_stack = -1;
		Aex = ADDR (addr + M[reg]);
		ACC = mmu_load (Aex);
		RAU = SET_LOGICAL (RAU);
		break;
	case 004:					/* сл, a+x */
		if (! addr && reg == 017) {
			M[017] = ADDR (M[017] - 1);
			corr_stack = 1;
		}
		Aex = ADDR (addr + M[reg]);
		besm6_add (mmu_load (Aex), 0, 0);
		RAU = SET_ADDITIVE (RAU);
		break;
	case 005:					/* вч, a-x */
		if (! addr && reg == 017) {
			M[017] = ADDR (M[017] - 1);
			corr_stack = 1;
		}
		Aex = ADDR (addr + M[reg]);
		besm6_add (mmu_load (Aex), 0, 1);
		RAU = SET_ADDITIVE (RAU);
		break;
	case 006:					/* вчоб, x-a */
		if (! addr && reg == 017) {
			M[017] = ADDR (M[017] - 1);
			corr_stack = 1;
		}
		Aex = ADDR (addr + M[reg]);
		besm6_add (mmu_load (Aex), 1, 0);
		RAU = SET_ADDITIVE (RAU);
		break;
	case 007:					/* вчаб, amx */
		if (! addr && reg == 017) {
			M[017] = ADDR (M[017] - 1);
			corr_stack = 1;
		}
		Aex = ADDR (addr + M[reg]);
		besm6_add (mmu_load (Aex), 1, 1);
		RAU = SET_ADDITIVE (RAU);
		break;
	case 010:					/* сч, xta */
		if (! addr && reg == 017) {
			M[017] = ADDR (M[017] - 1);
			corr_stack = 1;
		}
		Aex = ADDR (addr + M[reg]);
		ACC = mmu_load (Aex);
		RAU = SET_LOGICAL (RAU);
		break;
	case 011:					/* и, aax */
		if (! addr && reg == 017) {
			M[017] = ADDR (M[017] - 1);
			corr_stack = 1;
		}
		Aex = ADDR (addr + M[reg]);
		ACC &= mmu_load (Aex);
		RMR = 0;
		RAU = SET_LOGICAL (RAU);
		break;
	case 012:					/* нтж, aex */
		if (! addr && reg == 017) {
			M[017] = ADDR (M[017] - 1);
			corr_stack = 1;
		}
		Aex = ADDR (addr + M[reg]);
		RMR = ACC;
		ACC ^= mmu_load (Aex);
		RAU = SET_LOGICAL (RAU);
		break;
	case 013:					/* слц, arx */
		if (! addr && reg == 017) {
			M[017] = ADDR (M[017] - 1);
			corr_stack = 1;
		}
		Aex = ADDR (addr + M[reg]);
		ACC += mmu_load (Aex);
		if (ACC & BIT49)
			ACC = (ACC + 1) & BITS48;
		RMR = 0;
		RAU = SET_MULTIPLICATIVE (RAU);
		break;
	case 014:					/* знак, avx */
		if (! addr && reg == 017) {
			M[017] = ADDR (M[017] - 1);
			corr_stack = 1;
		}
		Aex = ADDR (addr + M[reg]);
		besm6_change_sign (mmu_load (Aex) >> 40 & 1);
		RAU = SET_ADDITIVE (RAU);
		break;
	case 015:					/* или, aox */
		if (! addr && reg == 017) {
			M[017] = ADDR (M[017] - 1);
			corr_stack = 1;
		}
		Aex = ADDR (addr + M[reg]);
		ACC |= mmu_load (Aex);
		RMR = 0;
		RAU = SET_LOGICAL (RAU);
		break;
	case 016:					/* дел, a/x */
		if (! addr && reg == 017) {
			M[017] = ADDR (M[017] - 1);
			corr_stack = 1;
		}
		Aex = ADDR (addr + M[reg]);
		besm6_divide (mmu_load (Aex));
		RAU = SET_MULTIPLICATIVE (RAU);
		break;
	case 017:					/* умн, a*x */
		if (! addr && reg == 017) {
			M[017] = ADDR (M[017] - 1);
			corr_stack = 1;
		}
		Aex = ADDR (addr + M[reg]);
		besm6_multiply (mmu_load (Aex));
		RAU = SET_MULTIPLICATIVE (RAU);
		break;
	case 020:					/* сбр, apx */
		if (! addr && reg == 017) {
			M[017] = ADDR (M[017] - 1);
			corr_stack = 1;
		}
		Aex = ADDR (addr + M[reg]);
		ACC = besm6_pack (ACC, mmu_load (Aex));
		RMR = 0;
		RAU = SET_LOGICAL (RAU);
		break;
	case 021:					/* рзб, aux */
		if (! addr && reg == 017) {
			M[017] = ADDR (M[017] - 1);
			corr_stack = 1;
		}
		Aex = ADDR (addr + M[reg]);
		ACC = besm6_unpack (ACC, mmu_load (Aex));
		RMR = 0;
		RAU = SET_LOGICAL (RAU);
		break;
	case 022:					/* чед, acx */
		if (! addr && reg == 017) {
			M[017] = ADDR (M[017] - 1);
			corr_stack = 1;
		}
		Aex = ADDR (addr + M[reg]);
		ACC = besm6_count_ones (ACC) + mmu_load (Aex);
		if (ACC & BIT49)
			ACC = (ACC + 1) & BITS48;
		RAU = SET_LOGICAL (RAU);
		break;
	case 023:					/* нед, anx */
		if (! addr && reg == 017) {
			M[017] = ADDR (M[017] - 1);
			corr_stack = 1;
		}
		Aex = ADDR (addr + M[reg]);
		besm6_highest_bit (mmu_load (Aex));
		RAU = SET_LOGICAL (RAU);
		break;
	case 024:					/* слп, e+x */
		if (! addr && reg == 017) {
			M[017] = ADDR (M[017] - 1);
			corr_stack = 1;
		}
		Aex = ADDR (addr + M[reg]);
		besm6_add_exponent ((mmu_load (Aex) >> 41) - 64);
		RAU = SET_MULTIPLICATIVE (RAU);
		break;
	case 025:					/* вчп, e-x */
		if (! addr && reg == 017) {
			M[017] = ADDR (M[017] - 1);
			corr_stack = 1;
		}
		Aex = ADDR (addr + M[reg]);
		besm6_add_exponent (64 - (mmu_load (Aex) >> 41));
		RAU = SET_MULTIPLICATIVE (RAU);
		break;
	case 026:					/* сд, asx */
		if (! addr && reg == 017) {
			M[017] = ADDR (M[017] - 1);
			corr_stack = 1;
		}
		Aex = ADDR (addr + M[reg]);
		besm6_shift ((mmu_load (Aex) >> 41) - 64);
		RAU = SET_LOGICAL (RAU);
		break;
	case 027:					/* рж, xtr */
		if (! addr && reg == 017) {
			M[017] = ADDR (M[017] - 1);
			corr_stack = 1;
		}
		Aex = ADDR (addr + M[reg]);
		RAU = (mmu_load (Aex) >> 41) & 077;
		break;
	case 030:					/* счрж, rte */
		Aex = ADDR (addr + M[reg]);
		ACC = (t_value) (RAU & Aex & 0177) << 41;
		RAU = SET_LOGICAL (RAU);
		break;
	case 031: {					/* счмр, yta */
		t_value x;

		Aex = ADDR (addr + M[reg]);
		if (IS_LOGICAL (RAU)) {
			ACC = RMR;
			break;
		}
		ACC = (ACC & ~BITS41) | (RMR & BITS40);
		x = RMR;
		besm6_add_exponent ((Aex & 0177) - 64);
		RMR = x;
		break;
	}
	case 032:					/* э32, ext */
		/* Fall through... */
	case 033:					/* увв, ext */
		Aex = ADDR (addr + M[reg]);
		if (! IS_SUPERVISOR (RUU))
			longjmp (cpu_halt, STOP_BADCMD);
		cmd_033 ();
		/* Режим АУ - логический, если операция была "чтение" */
		if (Aex & 04000)
			RAU = SET_LOGICAL (RAU);
		break;
	case 034:					/* слпа, e+n */
		Aex = ADDR (addr + M[reg]);
		besm6_add_exponent ((Aex & 0177) - 64);
		RAU = SET_MULTIPLICATIVE (RAU);
		break;
	case 035:					/* вчпа, e-n */
		Aex = ADDR (addr + M[reg]);
		besm6_add_exponent (64 - (Aex & 0177));
		RAU = SET_MULTIPLICATIVE (RAU);
		break;
	case 036:					/* сда, asn */
		Aex = ADDR (addr + M[reg]);
		besm6_shift ((Aex & 0177) - 64);
		RAU = SET_LOGICAL (RAU);
		break;
	case 037:					/* ржа, ntr */
		Aex = ADDR (addr + M[reg]);
		RAU = Aex & 077;
		break;
	case 040:					/* уи, ati */
		Aex = ADDR (addr + M[reg]);
		if (IS_SUPERVISOR (RUU)) {
			int reg = Aex & 037;
			M[reg] = ADDR (ACC);
			/* breakpoint/watchpoint regs will match physical
			 * or virtual addresses depending on the current
			 * mapping mode.
			 */
			if ((M[PSW] & PSW_MMAP_DISABLE) &&
				 (reg == IBP || reg == DWP))
				M[reg] |= BIT16;

		} else
			M[Aex & 017] = ADDR (ACC);
		M[0] = 0;
		break;
	case 041: {					/* уим, sti */
		unsigned rg, ad;

		Aex = ADDR (addr + M[reg]);
		rg = Aex & (IS_SUPERVISOR (RUU) ? 037 : 017);
		ad = ADDR (ACC);
		if ((M[PSW] & PSW_MMAP_DISABLE) &&
                                 (rg == IBP || rg == DWP))
                                M[rg] |= BIT16;
		M[0] = 0;
		if (rg != 017)
			M[017] = ADDR (M[017] - 1);
		ACC = mmu_load (rg != 017 ? M[017] : ad);
		M[rg] = ad;
		RAU = SET_LOGICAL (RAU);
		break;
	}
	case 042:					/* счи, ita */
load_modifier:	Aex = ADDR (addr + M[reg]);
		ACC = ADDR(M[Aex & (IS_SUPERVISOR (RUU) ? 037 : 017)]);
		RAU = SET_LOGICAL (RAU);
		break;
	case 043:					/* счим, its */
		mmu_store (M[017], ACC);
		M[017] = ADDR (M[017] + 1);
		goto load_modifier;
	case 044:					/* уии, mtj */
		Aex = addr;
		if (IS_SUPERVISOR (RUU)) {
transfer_modifier:	M[Aex & 037] = M[reg];
			if ((M[PSW] & PSW_MMAP_DISABLE) &&
			    ((Aex & 037) == IBP || (Aex & 037) == DWP))
                                M[Aex & 037] |= BIT16;

		} else
			M[Aex & 017] = M[reg];
		M[0] = 0;
		break;
	case 045:					/* сли, j+m */
		Aex = addr;
		if ((Aex & 020) && IS_SUPERVISOR (RUU))
			goto transfer_modifier;
		M[Aex & 017] = ADDR (M[Aex & 017] + M[reg]);
		M[0] = 0;
		break;
	case 046:					/* э46, x46 */
		Aex = addr;
		if (! IS_SUPERVISOR (RUU))
			longjmp (cpu_halt, STOP_BADCMD);
		M[Aex & 017] = ADDR (Aex);
		M[0] = 0;
		break;
	case 047:					/* э47, x47 */
		Aex = addr;
		if (! IS_SUPERVISOR (RUU))
			longjmp (cpu_halt, STOP_BADCMD);
		M[Aex & 017] = ADDR (M[Aex & 017] + Aex);
		M[0] = 0;
		break;
	case 050 ... 077:				/* э50...э77 */
	case 0200:					/* э20 */
	case 0210:					/* э21 */
		Aex = ADDR (addr + M[reg]);
		/* Адрес возврата из экстракода. */
		M[ERET] = nextpc;
		/* Сохранённые режимы УУ. */
		M[SPSW] = (M[PSW] & (PSW_INTR_DISABLE | PSW_MMAP_DISABLE |
			PSW_PROT_DISABLE)) | IS_SUPERVISOR (RUU);
		/* Текущие режимы УУ. */
		M[PSW] = PSW_INTR_DISABLE | PSW_MMAP_DISABLE |
			PSW_PROT_DISABLE | /*?*/ PSW_INTR_HALT;
		M[14] = Aex;
		RUU = SET_SUPERVISOR (RUU, SPSW_EXTRACODE);

		if (opcode <= 077)
			PC = 0500 + opcode;		/* э50-э77 */
		else
			PC = 0540 + (opcode >> 3);	/* э20, э21 */
		RUU &= ~RUU_RIGHT_INSTR;
		break;
	case 0220:					/* мода, utc */
		Aex = ADDR (addr + M[reg]);
		M[MOD] = Aex;
		nextaddrmod = 1;
		break;
	case 0230:					/* мод, wtc */
		if (! addr && reg == 017) {
			M[017] = ADDR (M[017] - 1);
			corr_stack = 1;
		}
		Aex = ADDR (addr + M[reg]);
		M[MOD] = ADDR (mmu_load (Aex));
		nextaddrmod = 1;
		break;
	case 0240:					/* уиа, vtm */
		Aex = addr;
		M[reg] = addr;
		M[0] = 0;
		if (IS_SUPERVISOR (RUU) && reg == 0) {
			M[PSW] &= ~(PSW_INTR_DISABLE |
				PSW_MMAP_DISABLE | PSW_PROT_DISABLE);
			M[PSW] |= addr & (PSW_INTR_DISABLE |
				PSW_MMAP_DISABLE | PSW_PROT_DISABLE);
		}
		break;
	case 0250:					/* слиа, utm */
		Aex = ADDR (addr + M[reg]);
		M[reg] = Aex;
		M[0] = 0;
		if (IS_SUPERVISOR (RUU) && reg == 0) {
			M[PSW] &= ~(PSW_INTR_DISABLE |
				PSW_MMAP_DISABLE | PSW_PROT_DISABLE);
			M[PSW] |= addr & (PSW_INTR_DISABLE |
				PSW_MMAP_DISABLE | PSW_PROT_DISABLE);
		}
		break;
	case 0260:					/* по, uza */
		Aex = ADDR (addr + M[reg]);
		RMR = ACC;
		if (IS_ADDITIVE (RAU)) {
			if (ACC & BIT41)
				break;
		} else if (IS_MULTIPLICATIVE (RAU)) {
			if (! (ACC & BIT48))
				break;
		} else if (IS_LOGICAL (RAU)) {
			if (ACC)
				break;
		} else
			break;
		PC = Aex;
		RUU &= ~RUU_RIGHT_INSTR;
		break;
	case 0270:					/* пе, u1a */
		Aex = ADDR (addr + M[reg]);
		RMR = ACC;
		if (IS_ADDITIVE (RAU)) {
			if (! (ACC & BIT41))
				break;
		} else if (IS_MULTIPLICATIVE (RAU)) {
			if (ACC & BIT48)
				break;
		} else if (IS_LOGICAL (RAU)) {
			if (! ACC)
				break;
		} else
			/* fall thru, i.e. branch */;
		PC = Aex;
		RUU &= ~RUU_RIGHT_INSTR;
		break;
	case 0300:					/* пб, uj */
		Aex = ADDR (addr + M[reg]);
		PC = Aex;
		RUU &= ~RUU_RIGHT_INSTR;
		break;
	case 0310:					/* пв, vjm */
		Aex = addr;
		M[reg] = nextpc;
		M[0] = 0;
		PC = addr;
		RUU &= ~RUU_RIGHT_INSTR;
		break;
	case 0320:					/* выпр, iret */
		Aex = addr;
		if (! IS_SUPERVISOR (RUU)) {
			longjmp (cpu_halt, STOP_BADCMD);
		}
		M[PSW] = (M[PSW] & PSW_WRITE_WATCH) |
			M[SPSW] & (SPSW_INTR_DISABLE |
			SPSW_MMAP_DISABLE | SPSW_PROT_DISABLE);
		PC = M[(reg & 3) | 030];
		RUU &= ~RUU_RIGHT_INSTR;
		if (M[SPSW] & SPSW_RIGHT_INSTR)
			RUU |= RUU_RIGHT_INSTR;
		else
			RUU &= ~RUU_RIGHT_INSTR;
		RUU = SET_SUPERVISOR (RUU,
			M[SPSW] & (SPSW_EXTRACODE | SPSW_INTERRUPT));
		if (M[SPSW] & SPSW_NEXT_RK)
			RUU |= RUU_MOD_RK;
		else
			RUU &= ~RUU_MOD_RK;
		break;
	case 0330:					/* стоп, stop */
		Aex = ADDR (addr + M[reg]);
		mmu_print_brz ();
		longjmp (cpu_halt, STOP_STOP);
		break;
	case 0340:					/* пио, vzm */
branch_zero:	Aex = addr;
		if (! M[reg]) {
			PC = addr;
			RUU &= ~RUU_RIGHT_INSTR;
		}
		break;
	case 0350:					/* пино, v1m */
		Aex = addr;
		if (M[reg]) {
			PC = addr;
			RUU &= ~RUU_RIGHT_INSTR;
		}
		break;
	case 0360:					/* э36, *36 */
		goto branch_zero;
	case 0370:					/* цикл, vlm */
		Aex = addr;
		if (!M[reg])
			break;
		M[reg] = ADDR (M[reg] + 1);
		PC = addr;
		RUU &= ~RUU_RIGHT_INSTR;
		break;
	default:
		/* Unknown instruction - cannot happen. */
		longjmp (cpu_halt, STOP_STOP);
		break;
	}

	/*
	 * Команда выполнилась успешно: можно сбросить признаки
	 * модификации адреса, если они не были только что установлены.
	 */
	if (nextaddrmod)
		RUU |= RUU_MOD_RK;
	else {
		RUU &= ~RUU_MOD_RK;
	}
}

/*
 * Операция прерывания 1: внутреннее прерывание.
 * Описана в 9-м томе технического описания БЭСМ-6, страница 119.
 */
void op_int_1 ()
{
	M[SPSW] = (M[PSW] & (PSW_INTR_DISABLE | PSW_MMAP_DISABLE |
		PSW_PROT_DISABLE)) | IS_SUPERVISOR (RUU);
	if (RUU & RUU_RIGHT_INSTR)
		M[SPSW] |= SPSW_RIGHT_INSTR;
	M[IRET] = PC;
	M[PSW] |= PSW_INTR_DISABLE | PSW_MMAP_DISABLE | PSW_PROT_DISABLE;
	if (RUU & RUU_MOD_RK) {
		M[SPSW] |= SPSW_MOD_RK;
		RUU &= ~RUU_MOD_RK;
	}
	PC = 0500;
	RUU &= ~RUU_RIGHT_INSTR;
	RUU = SET_SUPERVISOR (RUU, SPSW_INTERRUPT);
}

/*
 * Операция прерывания 2: внешнее прерывание.
 * Описана в 9-м томе технического описания БЭСМ-6, страница 129.
 */
void op_int_2 ()
{
	M[SPSW] = (M[PSW] & (PSW_INTR_DISABLE | PSW_MMAP_DISABLE |
		PSW_PROT_DISABLE)) | IS_SUPERVISOR (RUU);
	M[IRET] = PC;
	M[PSW] |= PSW_INTR_DISABLE | PSW_MMAP_DISABLE | PSW_PROT_DISABLE;
	if (RUU & RUU_MOD_RK) {
		M[SPSW] |= SPSW_MOD_RK;
		RUU &= ~RUU_MOD_RK;
	}
	PC = 0501;
	RUU &= ~RUU_RIGHT_INSTR;
	RUU = SET_SUPERVISOR (RUU, SPSW_INTERRUPT);
}

/*
 * Main instruction fetch/decode loop
 */
t_stat sim_instr (void)
{
	t_stat r;
	int iintr = 0;

	/* Restore register state */
	PC = PC & BITS15;				/* mask PC */
	sim_cancel_step ();				/* defang SCP step */
	mmu_setup ();					/* copy RP to TLB */

	/* An internal interrupt or user intervention */
	r = setjmp (cpu_halt);
	if (r) {
		M[017] += corr_stack;
		if (cpu_dev.dctrl) {
			const char *message = (r >= SCPE_BASE) ?
				scp_error_messages [r - SCPE_BASE] :
				sim_stop_messages [r];
			besm6_debug ("/// %05o%s: %s", PC,
				(RUU & RUU_RIGHT_INSTR) ? "п" : "л",
				message);
		}

		/*
		 * ПоП и ПоК вызывают останов при любом внутреннем прерывании
		 * или прерывании по контролю, соответственно.
		 * Если произошёл останов по ПоП или ПоК,
		 * то продолжение выполнения начнётся с команды, следующей
		 * за вызвавшей прерывание. Как если бы кнопка "ТП" (тип
		 * перехода) была включена. Подробнее на странице 119 ТО9.
		 */
		switch (r) {
		default:
			return r;
		case STOP_BADCMD:
			if (M[PSW] & PSW_INTR_HALT)		/* ПоП */
				return r;
			op_int_1();
			// SPSW_NEXT_RK is not important for this interrupt
			GRP |= GRP_ILL_INSN;
			break;
		case STOP_INSN_CHECK:
			if (M[PSW] & PSW_CHECK_HALT)		/* ПоК */
				return r;
			op_int_1();
			// SPSW_NEXT_RK must be 0 for this interrupt; it is already
			GRP |= GRP_INSN_CHECK;
			break;
		case STOP_INSN_PROT:
			if (M[PSW] & PSW_INTR_HALT)		/* ПоП */
				return r;
			if (RUU & RUU_RIGHT_INSTR) {
				++PC;
			}
			RUU ^= RUU_RIGHT_INSTR;
			op_int_1();
			// SPSW_NEXT_RK must be 1 for this interrupt
			M[SPSW] |= SPSW_NEXT_RK;
			GRP |= GRP_INSN_PROT;
			break;
		case STOP_OPERAND_PROT:
			if (M[PSW] & PSW_INTR_HALT)		/* ПоП */
				return r;
			if (RUU & RUU_RIGHT_INSTR) {
				++PC;
			}
			RUU ^= RUU_RIGHT_INSTR;
			op_int_1();
			M[SPSW] |= SPSW_NEXT_RK;
			// The offending virtual page is in bits 5-9
			GRP |= GRP_OPRND_PROT;
			GRP = GRP_SET_PAGE (GRP, iintr_data);
			break;
		case STOP_RAM_CHECK:
			if (M[PSW] & PSW_CHECK_HALT)		/* ПоК */
				return r;
			op_int_1();
			// The offending interleaved block # is in bits 1-3.
			GRP |= GRP_CHECK | GRP_RAM_CHECK;
			GRP = GRP_SET_BLOCK (GRP, iintr_data);
			break;
		case STOP_CACHE_CHECK:
			if (M[PSW] & PSW_CHECK_HALT)		/* ПоК */
				return r;
			op_int_1();
			// The offending BRZ # is in bits 1-3.
			GRP |= GRP_CHECK;
			GRP &= ~GRP_RAM_CHECK;
			GRP = GRP_SET_BLOCK (GRP, iintr_data);
			break;
		case STOP_INSN_ADDR_MATCH:
			if (M[PSW] & PSW_INTR_HALT)		/* ПоП */
				return r;
			if (RUU & RUU_RIGHT_INSTR) {
				++PC;
			}
			RUU ^= RUU_RIGHT_INSTR;
			op_int_1();
			M[SPSW] |= SPSW_NEXT_RK;
			GRP |= GRP_BREAKPOINT;
			break;
		case STOP_LOAD_ADDR_MATCH:
			if (M[PSW] & PSW_INTR_HALT)		/* ПоП */
				return r;
			if (RUU & RUU_RIGHT_INSTR) {
				++PC;
			}
			RUU ^= RUU_RIGHT_INSTR;
			op_int_1();
			M[SPSW] |= SPSW_NEXT_RK;
			GRP |= GRP_WATCHPT_R;
			break;
		case STOP_STORE_ADDR_MATCH:
			if (M[PSW] & PSW_INTR_HALT)		/* ПоП */
				return r;
			if (RUU & RUU_RIGHT_INSTR) {
				++PC;
			}
			RUU ^= RUU_RIGHT_INSTR;
			op_int_1();
			M[SPSW] |= SPSW_NEXT_RK;
			GRP |= GRP_WATCHPT_W;
			break;
		case STOP_OVFL:
			/* Прерывание по АУ вызывает останов, если БРО=0
			 * и установлен ПоП или ПоК.
			 * Страница 118 ТО9.*/
			if (! (RUU & RUU_AVOST_DISABLE) &&	/* ! БРО */
			    ((M[PSW] & PSW_INTR_HALT) ||	/* ПоП */
			     (M[PSW] & PSW_CHECK_HALT)))	/* ПоК */
				return r;
			op_int_1();
			GRP |= GRP_OVERFLOW|GRP_RAM_CHECK;
			break;
		case STOP_DIVZERO:
			if (! (RUU & RUU_AVOST_DISABLE) &&	/* ! БРО */
			    ((M[PSW] & PSW_INTR_HALT) ||	/* ПоП */
			     (M[PSW] & PSW_CHECK_HALT)))	/* ПоК */
				return r;
			op_int_1();
			GRP |= GRP_DIVZERO|GRP_RAM_CHECK;
			break;
		}
		++iintr;
	}

	if (iintr > 1)
		return STOP_DOUBLE_INTR;

	/* Main instruction fetch/decode loop */
	for (;;) {
		if (sim_interval <= 0) {		/* check clock queue */
			r = sim_process_event ();
			if (r)
				return r;
		}

		if (PC > BITS15) {			/* выход за пределы памяти */
			return STOP_RUNOUT;		/* stop simulation */
		}

		if (sim_brk_summ &&			/* breakpoint? */
		    sim_brk_test (PC, SWMASK ('E'))) {
			return STOP_IBKPT;		/* stop simulation */
		}

		if (! iintr && ! (RUU & RUU_RIGHT_INSTR) &&
		    ! (M[PSW] & PSW_INTR_DISABLE) && (GRP & MGRP)) {
			/* external interrupt */
			op_int_2();
		}
		cpu_one_inst ();			/* one instr */
		iintr = 0;

		if (delay < 1)
			delay = 1;
		sim_interval -= delay;			/* count down delay */

		if (sim_step && (--sim_step <= 0))	/* do step count */
			return SCPE_STOP;
	}
}

t_stat slow_clk (UNIT * this)
{
	GRP |= GRP_WATCHDOG;
	return sim_activate (this, 80000);
}

/*
 * В 9-й части частота таймера 250 Гц (4 мс),
 * в жизни - 50 Гц (20 мс).
 */
t_stat fast_clk (UNIT * this)
{
	GRP |= GRP_TIMER;
	return sim_activate (this, 20000);
}


UNIT clocks[] = {
	{ UDATA(slow_clk, UNIT_FIX, 0) },
	{ UDATA(fast_clk, UNIT_FIX, 0) }
};

t_stat clk_reset(DEVICE * dev)
{
	/* Схема автозапуска включается по нереализованной кнопке "МР" */
	/*sim_activate (&clocks[0], 80000);*/
	return sim_activate (&clocks[1], 20000);
}

DEVICE clock_dev = {
	"CLK", clocks, NULL, NULL,
	2, 0, 0, 0, 0, 0,
	NULL, NULL, &clk_reset,
	NULL, NULL, NULL, NULL,
	DEV_DEBUG
};
