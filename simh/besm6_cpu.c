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

/*
 * Разряды режима АУ.
 */
#define RAU_NORM_DISABLE	001	/* блокировка нормализации */
#define RAU_ROUND_DISABLE	002	/* блокировка округления */
#define RAU_LOG			004	/* признак логической группы */
#define RAU_MULT		010	/* признак группы умножения */
#define RAU_ADD			020	/* признак группы слодения */
#define RAU_OVF_DISABLE		040	/* блокировка переполнения */

#define RAU_MODE		(RAU_LOG | RAU_MULT | RAU_ADD)
#define SET_LOGICAL(x)		(((x) & ~RAU_MODE) | RAU_LOG)
#define SET_MULTIPLICATIVE(x)	(((x) & ~RAU_MODE) | RAU_MULT)
#define SET_ADDITIVE(x)		(((x) & ~RAU_MODE) | RAU_ADD)
#define IS_LOGICAL(x)		(((x) & RAU_MODE) == RAU_LOG)
#define IS_MULTIPLICATIVE(x)	(((x) & (RAU_ADD | RAU_MULT) == RAU_MULT)
#define IS_ADDITIVE(x)		((x) & RAU_ADD)

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
uint32 PC, M [NREGS], RAU, PPK, addrmod;
uint32 supmode, convol_mode;
int m15corr;
t_value RK, ACC, RMR, RP [8];
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
	{ "CчAC", &PC,    8, 15, 0, 1 }, /* счётчик адреса команды */
	{ "ППK",  &PPK,   2, 1,  0, 1 }, /* признак правой команды */
/* TODO: добавить ПКП, ПКЛ, БРО */
	{ "PK",   &RK,    8, 24, 0, 1 }, /* регистр выполняемой команды */
	{ "CM",   &ACC,   8, 48, 0, 1 }, /* сумматор */
	{ "PMP",  &RMR,   8, 48, 0, 1 }, /* регистр младших разрядов */
	{ "PAУ",  &RAU,   2, 6,  0, 1 }, /* режимы АУ */
	{ "M1",   &M[1],  8, 15, 0, 1 }, /* регистры-модификаторы */
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
	{ "M15",  &M[15], 8, 15, 0, 1 }, /* указатель магазина */
	{ "M16",  &M[16], 8, 15, 0, 1 }, /* модификатор адреса */
	{ "M17",  &M[17], 8, 15, 0, 1 }, /* режимы УУ */
	{ "M23",  &M[23], 8, 15, 0, 1 }, /* упрятывание режимов УУ */
	{ "M26",  &M[26], 8, 15, 0, 1 }, /* адрес возврата из экстракода */
	{ "M27",  &M[27], 8, 15, 0, 1 }, /* адрес возврата из прерывания */
	{ "M28",  &M[28], 8, 15, 0, 1 }, /* адрес останова по выполнению */
	{ "M29",  &M[29], 8, 15, 0, 1 }, /* адрес останова по чтению/записи */
	{ "PП0",  &RP[0], 8, 48, 0, 1 }, /* регистры приписки страниц */
	{ "PП1",  &RP[1], 8, 48, 0, 1 },
	{ "PП2",  &RP[2], 8, 48, 0, 1 },
	{ "PП3",  &RP[3], 8, 48, 0, 1 },
	{ "PП4",  &RP[4], 8, 48, 0, 1 },
	{ "PП5",  &RP[5], 8, 48, 0, 1 },
	{ "PП6",  &RP[6], 8, 48, 0, 1 },
	{ "PП7",  &RP[7], 8, 48, 0, 1 },
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
	&mmu_dev,
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

	/* Регистр 17: БлП, БлЗ, ПОП, ПОК, БлПр */
	M[17] = M17_MMAP_DISABLE | M17_PROT_DISABLE | M17_INTR_HALT |
		M17_CHECK_HALT | M17_INTR_DISABLE;

	/* Регистр 23: БлП, БлЗ, РежЭ, БлПр */
	M[23] = M23_MMAP_DISABLE | M23_PROT_DISABLE | M23_EXTRACODE |
		M23_INTR_DISABLE;

	supmode = 1;
	M[17] = 02003;
	sim_brk_types = sim_brk_dflt = SWMASK ('E');
	return SCPE_OK;
}

/*
 * Считывание слова из памяти.
 */
t_value load (int addr)
{
	t_value val;

	addr &= BITS15;
	if (addr == 0)
		return 0;

	return mmu_load(addr);
}

/*
 * Запись слова в памяти.
 */
void store (int addr, t_value val)
{
	addr &= BITS15;
	if (addr == 0)
		return;

	mmu_store(addr, val);
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
 * Increment delay. When stopped, perform a longjmp to cpu_halt,
 * sending a stop code.
 */
void cpu_one_inst ()
{
	int reg, opcode, addr, ma;

	t_value word = mmu_fetch(PC);
	if (PPK)
		RK = word;		/* get right instruction */
	else
		RK = word >> 24;	/* get left instruction */

	RK &= BITS24;

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

	reg = RK >> 20;
	if (RK & 02000000) {
		opcode = (RK >> 15) & 037;
		addr = RK & BITS15;
	} else {
		opcode = (RK >> 12) & 077;
		addr = RK & 07777;
		if (RK & 01000000)
			addr |= 070000;
	}

	if (addrmod) {	/* TODO: addrmod - это бит 5 в регистре М[23] */
                addr = (addr + M[16]) & BITS15;
		addrmod = 0;
        }

	delay = 0;
	m15corr = 0;
	switch (opcode) {
	default:
		longjmp (cpu_halt, STOP_BADCMD);

	case 000: /* зп - запись */
		store (addr + M[reg], ACC);
		if (! addr && reg==15)
			M[15] = (M[15] + 1) & BITS15;
		/* Режим АУ не изменяется. */
		delay = MAX (3, 3);
		break;

	case 001: /* зпм - запись магазинная */
		store (addr + M[reg], ACC);
		M[15] = (M[15] - 1) & BITS15;
		/* Если прерывание по защите случится при чтении магазина,
		 * нужно откатить магазин к состоянию на начало
		 * выполнения команды перед уходом на обработку прерывания.
		 */
		m15corr = 1;
		ACC = load (M[15]);
		/* Режим АУ - логический. */
		RAU = SET_LOGICAL (RAU);
		delay = MAX (6, 6);
		break;

	/*TODO*/
	}
}

/*
 * Main instruction fetch/decode loop
 */
t_stat sim_instr (void)
{
	t_stat r;

	/* Restore register state */
	PC = PC & BITS15;				/* mask PC */
	sim_cancel_step ();				/* defang SCP step */

	/* To stop execution, jump here. */
	r = setjmp (cpu_halt);
	if (r) {
		M[15] += m15corr;
		return r;
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

		cpu_one_inst ();			/* one instr */
		if (delay < 1)
			delay = 1;
		sim_interval -= delay;			/* count down delay */

		if (sim_step && (--sim_step <= 0))	/* do step count */
			return SCPE_STOP;
	}
}
