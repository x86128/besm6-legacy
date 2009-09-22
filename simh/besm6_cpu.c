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
#include <math.h>
#include <float.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

t_value memory [MEMSIZE];
uint32 PC, M [17], RAU, PPK;
t_value RK, ACC, RMR;
double delay;

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
	{ "CчAC", &PC,    8, 15, 0, 1 },	/* счётчик адреса команды */
	{ "ППK",  &PPK,   8, 1,  0, 1 },	/* признак правой команды */
	{ "PK",   &RK,    8, 24, 0, 1 },	/* регистр выполняемой команды */
	{ "CM",   &ACC,   8, 48, 0, 1 },	/* сумматор */
	{ "PMP",  &RMR,   8, 48, 0, 1 },	/* регистр младших разрядов */
	{ "PAУ",  &RAU,   8, 6,  0, 1 },	/* режим АУ */
	{ "M1",   &M[1],  8, 15, 0, 1 },	/* регистры-модификаторы */
	{ "M2",   &M[2],  8, 15, 0, 1 },
	{ "M3",   &M[3],  8, 15, 0, 1 },
	{ "M4",   &M[4],  8, 15, 0, 1 },
	{ "M5",   &M[5],  8, 15, 0, 1 },
	{ "M6",   &M[6],  8, 15, 0, 1 },
	{ "M7",   &M[7],  8, 15, 0, 1 },
	{ "M8",   &M[8],  8, 15, 0, 1 },
	{ "M9",   &M[9],  8, 15, 0, 1 },
	{ "M10",  &M[10], 8, 15, 0, 1 },
	{ "M11",  &M[11], 8, 15, 0, 1 },
	{ "M12",  &M[12], 8, 15, 0, 1 },
	{ "M13",  &M[13], 8, 15, 0, 1 },
	{ "M14",  &M[14], 8, 15, 0, 1 },
	{ "M15",  &M[15], 8, 15, 0, 1 },	/* указатель магазина */
	{ "M16",  &M[16], 8, 15, 0, 1 },
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
	&drum_dev,
	0
};

const char *sim_stop_messages[] = {
	"Неизвестная ошибка",				/* Unknown error */
	"Останов",					/* STOP */
	"Точка останова",				/* Breakpoint */
	"Выход за пределы памяти",			/* Run out end of memory */
	"Неверный код команды",				/* Invalid instruction */
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
	if (vptr)
		*vptr = memory [addr];
	return SCPE_OK;
}

/*
 * Memory deposit
 */
t_stat cpu_deposit (t_value val, t_addr addr, UNIT *uptr, int32 sw)
{
	if (addr >= MEMSIZE)
		return SCPE_NXM;
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
	PPK = 0;
	for (i=0; i<NREGS; ++i)
		M[i] = 0;
	sim_brk_types = sim_brk_dflt = SWMASK ('E');
	return SCPE_OK;
}

/*
 * Считывание слова из памяти.
 */
t_value load (int addr)
{
	t_value val;

	addr &= MEMSIZE-1;
	if (addr == 0)
		return 0;

	val = memory [addr];
	return val;
}

/*
 * Запись слова в памяти.
 */
void store (int addr, t_value val)
{
	addr &= MEMSIZE-1;
	if (addr == 0)
		return;

	memory [addr] = val;
}

double besm6_to_ieee (t_value word)
{
	/*TODO*/
	return 0;
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

/*
 * Execute one instruction, placed on address PC:PPK.
 * Increment delay. When stopped, return a nonzero stop code.
 */
t_stat cpu_one_inst ()
{
	int ir, opcode, ma;

	if (PPK)
		RK = memory [PC];		/* get instruction */
	else
		RK = memory [PC] >> 24;
	RK &= 077777777;

	if (sim_deb && cpu_dev.dctrl) {
		fprintf (sim_deb, "*** %05o.%o: ", PC, PPK);
		fprint_sym (sim_deb, PC, &RK, 0, SWMASK ('M'));
		fprintf (sim_deb, "\n");
	}
	if (PPK) {
		PC += 1;			/* increment PC */
		PPK = 0;
	} else {
		PPK = 1;
	}

	ir = RK >> 20;
	if (RK & 02000000) {
		opcode = (RK >> 15) & 037;
		ma = RK & 077777;
	} else {
		opcode = (RK >> 12) & 077;
		ma = RK & 07777;
		if (RK & 01000000)
			ma |= 070000;
	}
	switch (opcode) {
	default:
		return STOP_BADCMD;

	case 000: /* зп - запись */
		/*Типа: store (ACC, ma);*/
		/* Режим АУ не изменяется. */
		/*delay += 24;*/
		break;

	/*TODO*/
	}
	return 0;
}

/*
 * Main instruction fetch/decode loop
 */
t_stat sim_instr (void)
{
	t_stat r;
	int ticks;

	/* Restore register state */
	PC = PC & 077777;				/* mask PC */
	sim_cancel_step ();				/* defang SCP step */
	delay = 0;

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

		r = cpu_one_inst ();
		if (r)					/* one instr; error? */
			return r;

		ticks = 1;
		if (delay > 0)				/* delay to next instr */
			ticks += delay - DBL_EPSILON;
		delay -= ticks;				/* count down delay */
		sim_interval -= ticks;

		if (sim_step && (--sim_step <= 0))	/* do step count */
			return SCPE_STOP;
	}
}
