/*
 * besm6_cpu.c: BESM-6 CPU simulator
 *
 * Copyright (c) 2009, Serge Vakulenko
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
#include "besm6_optab.h"
#include "legacy.h"
#include <math.h>
#include <float.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

/*
 * Специальные индекс-регистры.
 */
#define M17_MMAP_DISABLE	000001	/* БлП - блокировка приписки */
#define M17_PROT_DISABLE	000002	/* БлЗ - блокировка защиты */
#define M17_INTR_HALT		000004	/* ПоП - признак останова при
					   любом внутреннем прерывании */
#define M17_CHECK_HALT		000010	/* ПоК - признак останова при
					   прерывании по контролю */
#define M17_WRITE_WATCH		000020	/* Зп(М29) - признак совпадения адреса
					   операнда прии записи в память
					   с содержанием регистра М29 */
#define M17_INTR_DISABLE	002000	/* БлПр - блокировка внешнего прерывания */
#define M17_AUT_B		004000	/* АвтБ - признак режима Автомат Б */

#define M23_MMAP_DISABLE	000001	/* БлП - блокировка приписки */
#define M23_PROT_DISABLE	000002	/* БлЗ - блокировка защиты */
#define M23_EXTRACODE		000004	/* РежЭ - режим экстракода */
#define M23_INTERRUPT		000010	/* РежПр - режим прерывания */
#define M23_MOD_RK		000020	/* ПрИК(РК) - на регистр РК принята
					   команда, которая должна быть
					   модифицирована регистром М[16] */
#define M23_MOD_RR		000040	/* ПрИК(РР) - на регистре РР находится
					   команда, выполненная с модификацией */
#define M23_UNKNOWN		000100	/* НОК? вписано карандашом в 9 томе */
#define M23_RIGHT_INSTR		000400	/* ПрК - признак правой команды */
#define M23_NEXT_RK		001000	/* ГД./ДК2 - на регистр РК принята
					   команда, следующая после вызвавшей
					   прерывание */
#define M23_INTR_DISABLE	002000	/* БлПр - блокировка внешнего прерывания */

t_value memory [MEMSIZE];
uint32 PC, RK, Aex, M [NREGS], RAU, RUU;
t_value ACC, RMR, GRP, MGRP;
uint32 PRP, MPRP;

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
{ "М8",    &M[8],	8, 15, 0, 1 },
{ "М9",    &M[9],	8, 15, 0, 1 },
{ "М10",   &M[10],	8, 15, 0, 1 },
{ "М11",   &M[11],	8, 15, 0, 1 },
{ "М12",   &M[12],	8, 15, 0, 1 },
{ "М13",   &M[13],	8, 15, 0, 1 },
{ "М14",   &M[14],	8, 15, 0, 1 },
{ "М15",   &M[15],	8, 15, 0, 1 },		/* указатель магазина */
{ "М16",   &M[16],	8, 15, 0, 1 },		/* модификатор адреса */
{ "М17",   &M[17],	8, 15, 0, 1 },		/* режимы УУ */
{ "М23",   &M[23],	8, 15, 0, 1 },		/* упрятывание режимов УУ */
{ "М26",   &M[26],	8, 15, 0, 1 },		/* адрес возврата из экстракода */
{ "М27",   &M[27],	8, 15, 0, 1 },		/* адрес возврата из прерывания */
{ "М28",   &M[28],	8, 15, 0, 1 },		/* адрес останова по выполнению */
{ "М29",   &M[29],	8, 15, 0, 1 },		/* адрес останова по чтению/записи */
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
{ "M8",    &M[8],	8, 15, 0, 1 },
{ "M9",    &M[9],	8, 15, 0, 1 },
{ "M10",   &M[10],	8, 15, 0, 1 },
{ "M11",   &M[11],	8, 15, 0, 1 },
{ "M12",   &M[12],	8, 15, 0, 1 },
{ "M13",   &M[13],	8, 15, 0, 1 },
{ "M14",   &M[14],	8, 15, 0, 1 },
{ "M15",   &M[15],	8, 15, 0, 1 },		/* указатель магазина */
{ "M16",   &M[16],	8, 15, 0, 1 },		/* модификатор адреса */
{ "M17",   &M[17],	8, 15, 0, 1 },		/* режимы УУ */
{ "M23",   &M[23],	8, 15, 0, 1 },		/* упрятывание режимов УУ */
{ "M26",   &M[26],	8, 15, 0, 1 },		/* адрес возврата из экстракода */
{ "M27",   &M[27],	8, 15, 0, 1 },		/* адрес возврата из прерывания */
{ "M28",   &M[28],	8, 15, 0, 1 },		/* адрес останова по выполнению */
{ "M29",   &M[29],	8, 15, 0, 1 },		/* адрес останова по чтению/записи */
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
	0
};

const char *sim_stop_messages[] = {
	"Неизвестная ошибка",				/* Unknown error */
	"Останов",					/* STOP */
	"Точка останова",				/* Breakpoint */
	"Выход за пределы памяти",			/* Run out end of memory */
	"Неверный код команды",				/* Invalid instruction */
	"Контроль команды",
        "Команда в чужом листе",
        "Число в чужом листе",
        "КЧ МОЗУ",
        "КЧ БРЗ",

/*TODO*/
	"Переполнение при сложении",			/* Addition overflow */
	"Переполнение при сложении порядков",		/* Exponent overflow */
	"Переполнение при умножении",			/* Multiplication overflow */
	"Переполнение при делении",			/* Division overflow */
	"Переполнение мантиссы при делении",		/* Division mantissa overflow */
	"Корень из отрицательного числа",		/* SQRT from negative number */
	"Ошибка вычисления корня",			/* SQRT error */
	"Ошибка чтения барабана",			/* Drum read error */
	"Неверная длина чтения барабана",		/* Invalid drum read length */
	"Неверная длина записи барабана",		/* Invalid drum write length */
	"Ошибка записи барабана",			/* Drum write error */
	"Неверное УЧ для обращения к барабану", 	/* Invalid drum control word */
	"Чтение неинициализированного барабана", 	/* Reading uninialized drum data */
	"Неверное УЧ для обращения к ленте",		/* Invalid tape control word */
	"Неверное УЧ для разметки ленты",		/* Invalid tape format word */
	"Обмен с магнитной лентой не реализован",	/* Tape not implemented */
	"Разметка магнитной ленты не реализована",	/* Tape formatting not implemented */
	"Вывод на перфокарты не реализован",		/* Punch not implemented */
	"Ввод с перфокарт не реализован",		/* Punch reader not implemented */
	"Неверное УЧ",					/* Invalid control word */
	"Неверный аргумент команды",			/* Invalid argument of instruction */
	"Останов по несовпадению",			/* Assertion failed */
	"Команда МБ не работает без МА",		/* MB instruction without MA */
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
		pult [addr] = val;
	else
		memory [addr] = val;
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
	M[17] = M17_MMAP_DISABLE | M17_PROT_DISABLE | M17_INTR_HALT |
		M17_CHECK_HALT | M17_INTR_DISABLE;

	/* Регистр 23: БлП, БлЗ, РежЭ, БлПр */
	M[23] = M23_MMAP_DISABLE | M23_PROT_DISABLE | M23_EXTRACODE |
		M23_INTR_DISABLE;

	GRP = MGRP = 0;
	sim_brk_types = sim_brk_dflt = SWMASK ('E');
	return SCPE_OK;
}

double besm6_to_ieee (t_value word)
{
	double mantissa;

	/* Сдвигаем так, чтобы знак мантиссы пришелся на знак целого;
	 * таким образом, mantissa равно исходной мантиссе, умноженной на 2**63.
	 */
        mantissa = (t_int64) word << (64 - 48 + 7);

	int exponent = word >> 41;

	/* Порядок смещен вверх на 64, и мантиссу нужно скорректировать */
        return ldexp (mantissa, exponent - 64 - 63);
}

/*
 * Write Unicode symbol to file.
 * Convert to UTF-8 encoding:
 * 00000000.0xxxxxxx -> 0xxxxxxx
 * 00000xxx.xxyyyyyy -> 110xxxxx, 10yyyyyy
 * xxxxyyyy.yyzzzzzz -> 1110xxxx, 10yyyyyy, 10zzzzzz
 */
static void
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

uinstr_t unpack (t_value rk)
{
	uinstr_t ui;

	ui.i_reg = rk >> 20;
	if (rk & BIT20) {
		ui.i_opcode = (rk >> 15) & 037;
		ui.i_opcode += 060;
		ui.i_addr = rk & BITS15;
	} else {
		ui.i_opcode = (rk >> 12) & 077;
		ui.i_addr = rk & 07777;
		if (rk & BIT19)
			ui.i_addr |= 070000;
	}
	return ui;
}

alureg_t toalu (t_value val)
{
        alureg_t ret;
        ret.l = val >> 24;
        ret.r = val & BITS24;
	return ret;
}

t_value fromalu (alureg_t reg)
{
        return (t_value) reg.l << 24 | reg.r;
}

/*
 * Execute one instruction, placed on address PC:RUU_RIGHT_INSTR.
 * Increment delay. When stopped, perform a longjmp to cpu_halt,
 * sending a stop code.
 */
void cpu_one_inst ()
{
	int reg, opcode, addr, n, i, r, nextpc, nextaddrmod = 0;
	uinstr_t ui;
	optab_t op;

	t_value word = mmu_fetch (PC);
	if (RUU & RUU_RIGHT_INSTR)
		RK = word;		/* get right instruction */
	else
		RK = word >> 24;	/* get left instruction */

	RK &= BITS24;

	ui = unpack (RK);
	op = optab[ui.i_opcode];
	reg = ui.i_reg;

	if (sim_deb && cpu_dev.dctrl) {
		fprintf (sim_deb, "*** %05o%s: ", PC,
			(RUU & RUU_RIGHT_INSTR) ? "п" : "л");
		besm6_fprint_cmd (sim_deb, RK);
		fprintf (sim_deb, "\n");
	}
	nextpc = PC + 1;
	if (RUU & RUU_RIGHT_INSTR) {
		PC += 1;			/* increment PC */
		RUU &= ~RUU_RIGHT_INSTR;
	} else {
		RUU |= RUU_RIGHT_INSTR;
	}

	if (RUU & RUU_MOD_RK) {
                addr = ADDR (ui.i_addr + M[16]);
        } else
                addr = ui.i_addr;
	Aex = ADDR (addr + M[reg]);

	delay = 0;
	corr_stack = 0;

	acc = toalu (ACC);
	accex = toalu (RMR);

	switch (op.o_inline) {
	case I_ATX:
		mmu_store (Aex, ACC);
		if (!addr && (reg == 15))
			M[15] = ADDR (M[15] + 1);
		break;
	case I_STX:
		mmu_store (Aex, ACC);
		STK_POP;
		break;
	case I_XTS:
		STK_PUSH;
		corr_stack = -1;
		GET_OP;
		acc = enreg;
		break;
	case I_XTA:
		CHK_STACK;
		GET_OP;
		acc = enreg;
		break;
	case I_VTM:
		M[reg] = addr;
		M[0] = 0;
		if (IS_SUPERVISOR (RUU) && reg == 0) {
			M[17] &= ~(M17_INTR_DISABLE |
				M17_MMAP_DISABLE | M17_PROT_DISABLE);
			M[17] |= addr & (M17_INTR_DISABLE |
				M17_MMAP_DISABLE | M17_PROT_DISABLE);
		}
		break;
	case I_UTM:
		M[reg] = Aex;
		M[0] = 0;
		if (IS_SUPERVISOR (RUU) && reg == 0) {
			M[17] &= ~(M17_INTR_DISABLE |
				M17_MMAP_DISABLE | M17_PROT_DISABLE);
			M[17] |= addr & (M17_INTR_DISABLE |
				M17_MMAP_DISABLE | M17_PROT_DISABLE);
		}
		break;
	case I_VLM:
		if (!M[reg])
			break;
		M[reg] = ADDR (M[reg] + 1);
		PC = addr;
		RUU &= ~RUU_RIGHT_INSTR;
		break;
	case I_UJ:
		PC = Aex;
		RUU &= ~RUU_RIGHT_INSTR;
		break;
	case I_STOP:
		mmu_print_brz ();
		longjmp (cpu_halt, STOP_STOP);
		break;
	case I_ITS:
		STK_PUSH;
		/*      fall    thru    */
	case I_ITA:
		acc.l = 0;
		acc.r = M[Aex & (IS_SUPERVISOR (RUU) ? 0x1f : 0xf)];
		break;
	case I_XTR:
		CHK_STACK;
		GET_OP;
set_mode:
		RAU = enreg.o & 077;
		break;
	case I_NTR:
		GET_NAI_OP;
		goto set_mode;
	case I_RTE:
		GET_NAI_OP;
		acc.o = RAU;
		acc.l = (long) (acc.o & enreg.o) << 17;
		acc.r = 0;
		break;
	case I_ASUB:
		CHK_STACK;
		GET_OP;
		if (NEGATIVE (acc))
			NEGATE (acc);
		if (!NEGATIVE (enreg))
			NEGATE (enreg);
		goto common_add;
	case I_RSUB:
		CHK_STACK;
		GET_OP;
		NEGATE (acc);
		goto common_add;
	case I_SUB:
		CHK_STACK;
		GET_OP;
		NEGATE (enreg);
		goto common_add;
	case I_ADD:
		CHK_STACK;
		GET_OP;
common_add:
		add();
		break;
	case I_YTA:
		if (IS_LOGICAL (RAU)) {
			acc = accex;
			break;
		}
		UNPCK (accex);
		UNPCK (acc);
		acc.mr = accex.mr;
		acc.ml = accex.ml & 0xffff;
		acc.o += (Aex & 0x7f) - 64;
		op.o_flags |= F_AR;
		enreg = accex;
		accex = zeroword;
		PACK (enreg);
		break;
	case I_UZA:
		accex = acc;
		if (IS_ADDITIVE (RAU)) {
			if (acc.l & 0x10000)
				break;
		} else if (IS_MULTIPLICATIVE (RAU)) {
			if (! (acc.l & 0x800000))
				break;
		} else if (IS_LOGICAL (RAU)) {
			if (acc.l | acc.r)
				break;
		} else
			break;
		PC = Aex;
		RUU &= ~RUU_RIGHT_INSTR;
		break;
	case I_UIA:
		accex = acc;
		if (IS_ADDITIVE (RAU)) {
			if (! (acc.l & 0x10000))
				break;
		} else if (IS_MULTIPLICATIVE (RAU)) {
			if (acc.l & 0x800000)
				break;
		} else if (IS_LOGICAL (RAU)) {
			if (! (acc.l | acc.r))
				break;
		} else
			/* fall thru, i.e. branch */;
		PC = Aex;
		RUU &= ~RUU_RIGHT_INSTR;
		break;
	case I_UTC:
		M[16] = Aex;
		nextaddrmod = 1;
		break;
	case I_WTC:
		CHK_STACK;
		GET_OP;
		M[16] = ADDR (enreg.r);
		nextaddrmod = 1;
		break;
	case I_VZM:
		if (ui.i_opcode & 1) {
			if (M[reg]) {
				PC = addr;
				RUU &= ~RUU_RIGHT_INSTR;
			}
		} else {
			if (!M[reg]) {
				PC = addr;
				RUU &= ~RUU_RIGHT_INSTR;
			}
		}
		break;
	case I_VJM:
		M[reg] = nextpc;
		M[0] = 0;
		PC = addr;
		RUU &= ~RUU_RIGHT_INSTR;
		break;
	case I_ATI:
		if (IS_SUPERVISOR (RUU)) {
			M[Aex & 0x1f] = ADDR (acc.r);
		} else
			M[Aex & 0xf] = ADDR (acc.r);
		M[0] = 0;
		break;
	case I_STI: {
		uint8   rg = Aex & (IS_SUPERVISOR (RUU) ? 0x1f : 0xf);
		uint16  ad = ADDR (acc.r);

		M[rg] = ad;
		M[0] = 0;
		if (rg != 15)
			M[15] = ADDR (M[15] - 1);
		LOAD (acc, M[15]);
		break;
	}
	case I_MTJ:
		if (IS_SUPERVISOR (RUU)) {
mtj:			M[addr & 0x1f] = M[reg];
		} else
			M[addr & 0xf] = M[reg];
		M[0] = 0;
		break;
	case I_MPJ: {
		uint8 rg = addr & 0xf;
		if (rg & 020 && IS_SUPERVISOR (RUU))
			goto mtj;
		M[rg] = ADDR (M[rg] + M[reg]);
		M[0] = 0;
		break;
	}
	case I_IRET:
		if (! IS_SUPERVISOR (RUU)) {
			longjmp (cpu_halt, STOP_BADCMD);
		}
		M[17] = M[23] & (M23_INTR_DISABLE |
			M23_MMAP_DISABLE | M23_PROT_DISABLE);
		PC = M[(reg & 3) | 030];
		RUU &= ~RUU_RIGHT_INSTR;
		if (M[23] & M23_RIGHT_INSTR)
			RUU |= RUU_RIGHT_INSTR;
		else
			RUU &= ~RUU_RIGHT_INSTR;
		RUU = SET_SUPERVISOR (RUU,
			M[23] & (M23_EXTRACODE | M23_INTERRUPT));
		if (M[23] & M23_NEXT_RK)
			RUU |= RUU_MOD_RK;
		else
			RUU &= ~RUU_MOD_RK;
		break;
	case I_TRAP:
		/* Адрес возврата из экстракода. */
		M[26] = nextpc;
		/* Сохранённые режимы УУ. */
		M[23] = (M[17] & (M17_INTR_DISABLE | M17_MMAP_DISABLE |
			M17_PROT_DISABLE)) | IS_SUPERVISOR (RUU);
		/* Текущие режимы УУ. */
		M[17] = M17_INTR_DISABLE | M17_MMAP_DISABLE |
			M17_PROT_DISABLE | /*?*/ M17_INTR_HALT;
		M[14] = Aex;
		RUU = SET_SUPERVISOR (RUU, M23_EXTRACODE);

		PC = 0500 + ui.i_opcode;	// E20? E21?
		RUU &= ~RUU_RIGHT_INSTR;
		break;
	case I_MOD:
		if (! IS_SUPERVISOR (RUU))
			longjmp (cpu_halt, STOP_BADCMD);
		n = (addr + M[reg]) & 0377;
		switch (n) {
		case 0 ... 7:
			mmu_setcache (n, ACC);
			break;
		case 020 ... 027:
			/* Запись в регистры приписки */
			mmu_setrp (n & 7, ACC);
			break;
		case 030 ... 033:
			/* Запись в регистры защиты */
			mmu_setprotection (n & 3, ACC);
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
			if (n & 1)
				RUU |= RUU_AVOST_DISABLE;
			else
				RUU &= ~RUU_AVOST_DISABLE;
			if (n & 2)
				RUU |= RUU_CONVOL_RIGHT;
			else
				RUU &= ~RUU_CONVOL_RIGHT;
			if (n & 4)
				RUU |= RUU_CONVOL_LEFT;
			else
				RUU &= ~RUU_CONVOL_LEFT;
			break;
		case 0140 ... 0177:
			/* TODO: управление блокировкой схемы
			 * автоматического запуска */
			longjmp (cpu_halt, STOP_BADCMD);
			break;
		case 0200 ... 0207:
			/* Чтение БРЗ */
			ACC = mmu_getcache (n & 7);
			acc=toalu(ACC);
			break;
		case 0237:
			/* Чтение главного регистра прерываний */
			acc=toalu(GRP);
			break;
		default:
			/* Неиспользуемые адреса */
			if (sim_deb && cpu_dev.dctrl) {
				fprintf (sim_deb, "*** %05o%s: ", PC,
					(RUU & RUU_RIGHT_INSTR) ? "п" : "л");
				besm6_fprint_cmd (sim_deb, RK);
				fprintf (sim_deb, ": неправильный адрес спец.регистра\n");
			}
			break;
		}
		/* Режим АУ - логический, если операция была "чтение" */
		if (n & 0200)
			RAU = SET_LOGICAL (RAU);
		delay = MEAN_TIME (3, 3);
		break;
	case I_EXT:
		if (! IS_SUPERVISOR (RUU))
			longjmp (cpu_halt, STOP_BADCMD);
		n = (addr + M[reg]) & 07777;
		switch (n) {
			case 0030:
				/* гашение ПРП */
				PRP &= ACC | PRP_WIRED_BITS;
				PRP2GRP;
				break;
			case 0034:
				/* запись в МПРП */
				MPRP = ACC & 077777777;
				PRP2GRP;
				break;
			case 04030:
				/* чтение старшей половины ПРП */
				acc = toalu(PRP & 077770000);
				break;
			case 04034:
				/* чтение младшей половины ПРП */
				acc = toalu(PRP & 07777 | 0377);
				break;
		}
		/* Режим АУ - логический, если операция была "чтение" */
		if (n & 04000)
			RAU = SET_LOGICAL (RAU);
		break;
	default:
		if (op.o_flags & F_STACK) {
			CHK_STACK;
		}
		if (op.o_flags & F_OP) {
			GET_OP;
		} else if (op.o_flags & F_NAI)
			GET_NAI_OP;

		if (! IS_SUPERVISOR (RUU) && op.o_flags & F_PRIV)
			longjmp (cpu_halt, STOP_BADCMD);
		else
			(*op.o_impl)();
		break;
	}

	if ((i = op.o_flags & F_GRP))
		RAU = SET_MODE (RAU, 1<<(i+1));

	if (op.o_flags & F_AR) {
		uint32 rr = 0;
		switch ((acc.ml >> 16) & 3) {
		case 2:
		case 1:
			rnd_rq |= acc.mr & 1;
			accex.mr = (accex.mr >> 1) | (accex.ml << 23);
			accex.ml = (accex.ml >> 1) | (acc.mr << 15);
			acc.mr = (acc.mr >> 1) | (acc.ml << 23);
			acc.ml >>= 1;
			++acc.o;
			goto chk_rnd;
		}

		if (RAU & RAU_NORM_DISABLE)
			goto chk_rnd;
		if (!(i = (acc.ml >> 15) & 3)) {
			if ((r = acc.ml & 0xffff)) {
				int cnt;
				for (cnt = 0; (r & 0x8000) == 0;
							++cnt, r <<= 1);
				acc.ml = (r & 0xffff) |
						(acc.mr >> (24 - cnt));
				acc.mr = (acc.mr << cnt) |
						(rr = accex.ml >> (16 - cnt));
				accex.ml = (accex.ml << cnt) |
						(accex.mr >> (24 - cnt));
				accex.mr <<= cnt;
				acc.o -= cnt;
				goto chk_zero;
			}
			if ((r = acc.mr >> 16)) {
				int     cnt, fcnt;
				for (cnt = 0; (r & 0x80) == 0;
							++cnt, r <<= 1);
				acc.ml = acc.mr >> (8 - cnt);
				acc.mr = (acc.mr << (fcnt = 16 + cnt)) |
						(accex.ml << cnt) |
						(accex.mr >> (24 - cnt));
				accex.mr <<= fcnt;
				acc.o -= fcnt;
				rr = acc.r & ((1l << fcnt) - 1);
				goto chk_zero;
			}
			if ((r = acc.mr & 0xffff)) {
				int cnt;
				for (cnt = 0; (r & 0x8000) == 0;
							++cnt, r <<= 1);
				acc.ml = (r & 0xffff) |
						(accex.ml >> (16 - cnt));
				acc.mr = (accex.ml << (8 + cnt)) |
						(accex.mr >> (16 - cnt));
				accex.ml = accex.mr << cnt;
				accex.mr = 0;
				acc.o -= 24 + cnt;
				rr = (acc.ml & ((1 << cnt) - 1)) | acc.mr;
				goto chk_zero;
			}
			if ((r = accex.ml & 0xffff)) {
				int cnt;
				rr = accex.ml | accex.mr;
				for (cnt = 0; (r & 0x8000) == 0;
							++cnt, r <<= 1);
				acc.ml = (r & 0xffff) |
						(accex.mr >> (24 - cnt));
				acc.mr = (accex.mr << cnt);
				accex.ml = accex.mr = 0;
				acc.o -= 40 + cnt;
				goto chk_zero;
			}
			if ((r = accex.mr >> 16)) {
				int cnt;
				rr = accex.ml | accex.mr;
				for (cnt = 0; (r & 0x80) == 0;
							++cnt, r <<= 1);
				acc.ml = accex.mr >> (8 - cnt);
				acc.mr = accex.mr << (16 + cnt);
				accex.ml = accex.mr = 0;
				acc.o -= 56 + cnt;
				goto chk_zero;
			}
			if ((r = accex.mr & 0xffff)) {
				int cnt;
				rr = accex.ml | accex.mr;
				for (cnt = 0; (r & 0x8000) == 0;
							++cnt, r <<= 1);
				acc.ml = (r & 0xffff);
				acc.mr = accex.ml = accex.mr = 0;
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
						(rr = accex.ml >> (16 - cnt));
				accex.ml = ((accex.ml << cnt) |
						(accex.mr >> (24 - cnt)))
						& 0xffff;
				accex.mr <<= cnt;
				acc.o -= cnt;
				goto chk_zero;
			}
			if ((r = (~acc.mr >> 16) & 0xff)) {
				int     cnt, fcnt;
				for (cnt = 0; (r & 0x80) == 0;
							++cnt, r = (r << 1) | 1);
				acc.ml = 0x10000 | (acc.mr >> (8 - cnt));
				acc.mr = (acc.mr << (fcnt = 16 + cnt)) |
						(accex.ml << cnt) |
						(accex.mr >> (24 - cnt));
				accex.ml = ((accex.ml << fcnt) |
						(accex.mr >> (8 - cnt)))
						& 0xffff;
				accex.mr <<= fcnt;
				acc.o -= fcnt;
				rr = acc.r & ((1l << fcnt) - 1);
				goto chk_zero;
			}
			if ((r = ~acc.mr & 0xffff)) {
				int cnt;
				for (cnt = 0; (r & 0x8000) == 0;
							++cnt, r = (r << 1) | 1);
				acc.ml = 0x10000 | (~r & 0xffff) |
						(accex.ml >> (16 - cnt));
				acc.mr = (accex.ml << (8 + cnt)) |
						(accex.mr >> (16 - cnt));
				accex.ml = (accex.mr << cnt) & 0xffff;
				accex.mr = 0;
				acc.o -= 24 + cnt;
				rr = (acc.ml & ((1 << cnt) - 1)) | acc.mr;
				goto chk_zero;
			}
			if ((r = ~accex.ml & 0xffff)) {
				int cnt;
				rr = accex.ml | accex.mr;
				for (cnt = 0; (r & 0x8000) == 0;
							++cnt, r = (r << 1) | 1);
				acc.ml = 0x10000 | (~r & 0xffff) |
						(accex.mr >> (24 - cnt));
				acc.mr = (accex.mr << cnt);
				accex.ml = accex.mr = 0;
				acc.o -= 40 + cnt;
				goto chk_zero;
			}
			if ((r = (~accex.mr >> 16) & 0xff)) {
				int cnt;
				rr = accex.ml | accex.mr;
				for (cnt = 0; (r & 0x80) == 0;
							++cnt, r = (r << 1) | 1);
				acc.ml = 0x10000 | (accex.mr >> (8 - cnt));
				acc.mr = accex.mr << (16 + cnt);
				accex.ml = accex.mr = 0;
				acc.o -= 56 + cnt;
				goto chk_zero;
			}
			if ((r = ~accex.mr & 0xffff)) {
				int cnt;
				rr = accex.ml | accex.mr;
				for (cnt = 0; (r & 0x8000) == 0;
							++cnt, r = (r << 1) | 1);
				acc.ml = 0x10000 | (~r & 0xffff);
				acc.mr = accex.ml = accex.mr = 0;
				acc.o -= 64 + cnt;
				goto chk_zero;
			} else {
				rr = 1;
				acc.ml = 0x10000;
				acc.mr = accex.ml = accex.mr = 0;
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
zero:
			acc.l = acc.r = accex.l = accex.r = 0;
			goto done;
		}
		acc.l = ((uint32) (acc.o & 0x7f) << 17) | (acc.ml & 0x1ffff);
		acc.r = acc.mr & 0xffffff;

		accex.l = ((uint32) (accex.o & 0x7f) << 17) | (accex.ml & 0x1ffff);
		accex.r = accex.mr & 0xffffff;
done:
		if (op.o_inline == I_YTA)
			accex = enreg;
		rnd_rq = 0;
	}
	ACC = fromalu (acc);
	RMR = fromalu (accex);
	/*
	 * Команда выполнилась успешно: можно сбросить признаки
	 * модификации адреса, если они не были только что установлены.
	 */
	if (nextaddrmod)
		RUU |= RUU_MOD_RK;
	else {
		RUU &= ~RUU_MOD_RK;
		M[16] = 0;
	}
}

/* ОпПр1, ТО ч.9, стр. 119 */
void OpInt1 ()
{
	M[23] = (M[17] & (M17_INTR_DISABLE | M17_MMAP_DISABLE |
		M17_PROT_DISABLE)) | IS_SUPERVISOR (RUU);
	if (RUU & RUU_RIGHT_INSTR)
		M[23] |= M23_RIGHT_INSTR;
	M[27] = PC;
	M[17] |= M17_INTR_DISABLE | M17_MMAP_DISABLE | M17_PROT_DISABLE;
	if (RUU & RUU_MOD_RK) {
		M[23] |= M23_MOD_RK;
		RUU &= ~RUU_MOD_RK;
	}

	PC = 0500;
	RUU &= ~RUU_RIGHT_INSTR;
	RUU = SET_SUPERVISOR (RUU, M23_INTERRUPT);
}

/* ОпПр1, ТО ч.9, стр. 119 */
void OpInt2 ()
{
	M[23] = (M[17] & (M17_INTR_DISABLE | M17_MMAP_DISABLE |
		M17_PROT_DISABLE)) | IS_SUPERVISOR (RUU);
	M[27] = PC;
	M[17] |= M17_INTR_DISABLE | M17_MMAP_DISABLE | M17_PROT_DISABLE;
	if (RUU & RUU_MOD_RK) {
		M[23] |= M23_MOD_RK;
		RUU &= ~RUU_MOD_RK;
	}
	PC = 0501;
	RUU &= ~RUU_RIGHT_INSTR;
	RUU = SET_SUPERVISOR (RUU, M23_INTERRUPT);
}

void IllegalInsn ()
{
	OpInt1();
	// M23_NEXT_RK is not important for this interrupt
	GRP |= GRP_ILL_INSN;
}

void InsnCheck ()
{
	OpInt1();
	// M23_NEXT_RK must be 0 for this interrupt; it is already
	GRP |= GRP_INSN_CHECK;
}

void InsnProt ()
{
	OpInt1();
	// M23_NEXT_RK must be 1 for this interrupt
	M[23] |= M23_NEXT_RK;
	GRP |= GRP_INSN_PROT;
}

void OperProt ()
{
	OpInt1();
	// M23_NEXT_RK can be 0 or 1; 0 means the standard PC rollback
	GRP |= GRP_OPRND_PROT;
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
		M[15] += corr_stack;
		switch (r) {
		case STOP_STOP:				/* STOP insn */
		case STOP_IBKPT:			/* breakpoint req */
		case STOP_RUNOUT:			/* must not happen */
			return r;
		case STOP_BADCMD:
			IllegalInsn();
			break;
		case STOP_INSN_CHECK:
			InsnCheck();
			break;
		case STOP_INSN_PROT:
			InsnProt();
			break;
		case STOP_OPERAND_PROT:
			OperProt();
			break;
		}
		iintr = 1;
	}

	/* Main instruction fetch/decode loop */
	for (;;) {
		if (sim_interval <= 0) {		/* check clock queue */
			r = sim_process_event ();
			if (r)
				return r;
		}

		if (PC >= 0100000) {			/* выход за пределы памяти */
			return STOP_RUNOUT;		/* stop simulation */
		}

		if (sim_brk_summ &&			/* breakpoint? */
		    sim_brk_test (PC, SWMASK ('E'))) {
			return STOP_IBKPT;		/* stop simulation */
		}

		if (! iintr && ! (RUU & RUU_RIGHT_INSTR) &&
		    ! (M[17] & M17_INTR_DISABLE) && (GRP & MGRP)) {
			/* external interrupt */
			OpInt2();
		}
		cpu_one_inst ();			/* one instr */
		if (delay < 1)
			delay = 1;
		sim_interval -= delay;			/* count down delay */

		if (sim_step && (--sim_step <= 0))	/* do step count */
			return SCPE_STOP;
	}
}

t_stat slow_clk(UNIT * this) {
	GRP |= GRP_WATCHDOG;
	sim_activate(this, 80000);
}

/*
 * В 9-й части частота таймера 250 Гц (4 мс),
 * в жизни - 50 Гц (20 мс).
 */
t_stat fast_clk(UNIT * this) {
	GRP |= GRP_TIMER;
	sim_activate(this, 20000);
}


UNIT clocks[] = {
	{ UDATA(slow_clk, UNIT_FIX, 0) },
	{ UDATA(fast_clk, UNIT_FIX, 0) }
};

t_stat clk_reset(DEVICE * dev) {
/*
	Схема автозапуска включается по нереализованной кнопке "МР"
	sim_activate(&clocks[0], 80000);
*/
	sim_activate(&clocks[1], 20000);
}

DEVICE clock_dev = {
	"CLK", clocks, NULL, NULL,
	2, 0, 0, 0, 0, 0,
	NULL, NULL, &clk_reset,
	NULL, NULL, NULL, NULL,
	DEV_DEBUG
};
