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

#define STACKREG	15
#define MODREG		16
#define PSREG		17
#define PSSREG		23
#define TRAPRETREG	26

t_value memory [MEMSIZE];
uint32 PC, M [NREGS], RAU, PPK, addrmod;
uint32 supmode, convol_mode;
t_value GRP, MGRP;
/* нехранящие биты ГРП должны сбрасываться путем обнуления тех регистров,
 * сборкой которых они являются
 */
#define GRP_WIRED_BITS 01400743700000000LL

int corr_stack;
t_value RK, ACC, RMR;
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
	{ "GRP",  &GRP,   8, 48, 0, 1 }, /* главный регистр прерываний */
	{ "MGRP", &MGRP,  8, 48, 0, 1 }, /* маска ГРП */
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

	GRP = MGRP = 0;
	supmode = M23_EXTRACODE;
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
        return ldexp(mantissa, exponent - 64 - 63);
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

uinstr_t unpack(t_value rk) {
	uinstr_t ui;
	ui.i_reg = rk >> 20;
	if (rk & 02000000) {
		ui.i_opcode = (rk >> 15) & 037;
		ui.i_opcode += 060;
		ui.i_addr = rk & BITS15;
	} else {
		ui.i_opcode = (rk >> 12) & 077;
		ui.i_addr = rk & 07777;
		if (rk & 01000000)
			ui.i_addr |= 070000;
	}
	return ui;
}

alureg_t toalu(t_value val) {
        alureg_t ret;
        ret.l = val >> 24;
        ret.r = val & BITS24;
	return ret;
}

t_value fromalu(alureg_t reg) {
        return (t_value) reg.l << 24 | reg.r;
}

#define JMP(addr) (PC=(addr),PPK=0)
#define effaddr ADDR(addr + M[reg])
	
/*
 * Execute one instruction, placed on address PC:PPK.
 * Increment delay. When stopped, perform a longjmp to cpu_halt,
 * sending a stop code.
 */
void cpu_one_inst ()
{
	int reg, opcode, addr, n, i, r, nextpc, nextaddrmod = 0;
	uinstr_t ui;
	optab_t op;

	t_value word = mmu_fetch(PC);
	if (PPK)
		RK = word;		/* get right instruction */
	else
		RK = word >> 24;	/* get left instruction */

	RK &= BITS24;

	ui = unpack(RK);
	op = optab[ui.i_opcode];
	reg = ui.i_reg;

	if (sim_deb && cpu_dev.dctrl) {
		fprintf (sim_deb, "*** %05o.%o: ", PC, PPK);
		fprint_sym (sim_deb, PC, &RK, 0, SWMASK ('M'));
		fprintf (sim_deb, "\n");
	}
	nextpc = PC + 1;
	if (PPK) {
		PC += 1;			/* increment PC */
		PPK = 0;
	} else {
		PPK = 1;
	}

	if (addrmod) {
                addr = ADDR(ui.i_addr + M[16]);
        } else
                addr = ui.i_addr;

	delay = 0;
	corr_stack = 0;
	
	acc = toalu(ACC);
	accex = toalu(RMR);

	switch (op.o_inline) {
	case I_ATX:
		mmu_store(effaddr, ACC);
		if (!addr && (reg == STACKREG))
			M[STACKREG] = ADDR(M[STACKREG] + 1);
		break;
	case I_STX:
		mmu_store(effaddr, ACC);
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
		if (supmode && reg == 0) {
			M[PSREG] &= ~02003;
			M[PSREG] |= addr & 02003;
		}
		break;
	case I_UTM:
		M[reg] = effaddr;
		M[0] = 0;
		if (supmode && reg == 0) {
			M[PSREG] &= ~02003;
			M[PSREG] |= addr & 02003;
		}
		break;
	case I_VLM:
		if (!M[reg])
			break;
		M[reg] = ADDR(M[reg] + 1);
		JMP(addr);
		break;
	case I_UJ:
		JMP(effaddr);
		break;
	case I_STOP:
		longjmp(cpu_halt, STOP_STOP);
		break;
	case I_ITS:
		STK_PUSH;
		/*      fall    thru    */
	case I_ITA:
		acc.l = 0;
		acc.r = M[effaddr & (supmode ? 0x1f : 0xf)];
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
		if (NEGATIVE(acc))
			NEGATE(acc);
		if (!NEGATIVE(enreg))
			NEGATE(enreg);
		goto common_add;
	case I_RSUB:
		CHK_STACK;
		GET_OP;
		NEGATE(acc);
		goto common_add;
	case I_SUB:
		CHK_STACK;
		GET_OP;
		NEGATE(enreg);
		goto common_add;
	case I_ADD:
		CHK_STACK;
		GET_OP;
common_add:
		add();
		break;
	case I_YTA:
		if (IS_LOGICAL(RAU)) {
			acc = accex;
			break;
		}
		UNPCK(accex);
		UNPCK(acc);
		acc.mr = accex.mr;
		acc.ml = accex.ml & 0xffff;
		acc.o += (effaddr & 0x7f) - 64;
		op.o_flags |= F_AR;
		enreg = accex;
		accex = zeroword;
		PACK(enreg);
		break;
	case I_UZA:
		accex = acc;
		if (IS_ADDITIVE(RAU)) {
			if (acc.l & 0x10000)
					break;
		} else if (IS_MULTIPLICATIVE(RAU)) {
			if (!(acc.l & 0x800000))
					break;
		} else if (IS_LOGICAL(RAU)) {
			if (acc.l | acc.r)
					break;
		} else
			break;
		JMP(effaddr);
		break;
	case I_UIA:
		accex = acc;
		if (IS_ADDITIVE(RAU)) {
			if (!(acc.l & 0x10000))
					break;
		} else if (IS_MULTIPLICATIVE(RAU)) {
			if (acc.l & 0x800000)
					break;
		} else if (IS_LOGICAL(RAU)) {
			if (!(acc.l | acc.r))
					break;
		} else
			/* fall thru, i.e. branch */;
		JMP(effaddr);
		break;
	case I_UTC:
		M[MODREG] = effaddr;
		nextaddrmod = 1;
		break;
	case I_WTC:
		CHK_STACK;
		GET_OP;
		M[MODREG] = ADDR(enreg.r);
		nextaddrmod = 1;
		break;
	case I_VZM:
		if (ui.i_opcode & 1) {
			if (M[reg]) {
				JMP(addr);
			}
		} else {
			if (!M[reg]) {
				JMP(addr);
			}
		}
		break;
	case I_VJM:
		M[reg] = nextpc;
		M[0] = 0;
		JMP(addr);
		break;
	case I_ATI:
		if (supmode) {
			M[effaddr & 0x1f] = ADDR(acc.r);
		} else
			M[effaddr & 0xf] = ADDR(acc.r);
		M[0] = 0;
		break;
	case I_STI: {
		uint8   rg = effaddr & (supmode ? 0x1f : 0xf);
		uint16  ad = ADDR(acc.r);

		M[rg] = ad;
		M[0] = 0;
		if (rg != STACKREG)
			M[STACKREG] = ADDR(M[STACKREG] - 1);
		LOAD(acc, M[STACKREG]);
		break;
	}
	case I_MTJ:
		if (supmode) {
mtj:
			M[addr & 0x1f] = M[reg];
		} else
			M[addr & 0xf] = M[reg];
		M[0] = 0;
		break;
	case I_MPJ: {
		uint8 rg = addr & 0xf;
		if (rg & 020 && supmode)
			goto mtj;
		M[rg] = ADDR(M[rg] + M[reg]);
		M[0] = 0;
		break;
	}
	case I_IRET:
		if (!supmode) {
			longjmp(cpu_halt, STOP_BADCMD);
		}
		M[PSREG] = M[PSSREG] & 02003;
		JMP(M[(reg & 3) | 030]);
		PPK = !!(M[PSSREG] & 0400);
		supmode = M[PSSREG] & (M23_EXTRACODE|M23_INTERRUPT);
		addrmod = M[PSSREG] & M23_NEXT_RK ? 1 : 0;
		break;
	case I_TRAP:
		M[TRAPRETREG] = nextpc;
		M[PSSREG] = (M[PSREG] & 02003) | supmode;
		M[016] = effaddr;
		supmode = M23_EXTRACODE;
		M[PSREG] = 02007; // 2003 ?
		JMP(0500 + ui.i_opcode); // E20? E21?
		break;
	case I_MOD:
		if (!supmode)
			longjmp(cpu_halt, STOP_BADCMD);
		n = (addr + M[reg]) & 0377;
		switch (n) {
		case 0 ... 7:
			mmu_setcache(n, ACC);
			break;
		case 020 ... 027:
			/* Запись в регистры приписки */
			mmu_settlb(n & 7, ACC);
			break;
		case 030 ... 033:
			/* Запись в регистры защиты */
			mmu_setprotection(n & 3, ACC);
			break;
		case 036:
			/* TODO: запись в главный регистр маски */
			longjmp (cpu_halt, STOP_BADCMD);
			break;
		case 037:
			/* TODO: гашение главного регистра прерываний */
			longjmp (cpu_halt, STOP_BADCMD);
			break;
		case 0100 ... 0137:
			/* TODO: управление блокировкой режима останова БРО
			 * (бит 1)
			 * Биты 2 и 3 - признаки формирования контрольных  
			 * разрядов (ПКП и ПКЛ).
			 */
			convol_mode = (n >> 1) & 3;
			break;
		case 0140 ... 0177:
			/* TODO: управление блокировкой схемы
			 * автоматического запуска */
			longjmp (cpu_halt, STOP_BADCMD);
			break;
		case 0200 ... 0207:
			/* Чтение БРЗ */
			ACC = mmu_getcache(n & 7);
			break;
		case 0237:
			/* TODO: чтение главного регистра прерываний */
			longjmp (cpu_halt, STOP_BADCMD);
			break;
		}
		/* Режим АУ - логический, если операция была "чтение" */
		if (n & 0200)
			RAU = SET_LOGICAL (RAU);
		delay = MEAN_TIME (3, 3);
		break;
	default:
		if (op.o_flags & F_STACK) {
			CHK_STACK;
		}
		if (op.o_flags & F_OP) {
			GET_OP;
		} else if (op.o_flags & F_NAI)
			GET_NAI_OP;

		if (!supmode && op.o_flags & F_PRIV)
			longjmp(cpu_halt, STOP_BADCMD);
		else
			(*op.o_impl)();
		break;
	}

	if ((i = op.o_flags & F_GRP))
		RAU = SET_MODE(RAU, 1<<(i+1));

	if (op.o_flags & F_AR) {
		uint    rr = 0;
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

		if (dis_norm)
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
			if (!dis_exc)
				longjmp(cpu_halt, STOP_OVFL);
		}
		if (!dis_round && rnd_rq)
			acc.mr |= 1;

		if (!acc.ml && !acc.mr && !dis_norm) {
zero:
			acc.l = acc.r = accex.l = accex.r = 0;
			goto done;
		}
		acc.l = ((uint) (acc.o & 0x7f) << 17) | (acc.ml & 0x1ffff);
		acc.r = acc.mr & 0xffffff;

		accex.l = ((uint) (accex.o & 0x7f) << 17) | (accex.ml & 0x1ffff);
		accex.r = accex.mr & 0xffffff;
done:
		if (op.o_inline == I_YTA)
			accex = enreg;
		rnd_rq = 0;
	}
	ACC = fromalu(acc);
	RMR = fromalu(accex);
	/*
	 * Команда выполнилась успешно: можно сбросить признаки
	 * модификации адреса, если они не были только что установлены.
	 */
	addrmod = nextaddrmod;
	if (addrmod == 0) {
		M[16] = 0;
	}
}

/* ОпПр1, ТО ч.9, стр. 119 */
void OpInt1() {
	M[PSSREG] = (M[PSREG] & 02003) | supmode;
	if (PPK)
		M[PSSREG] |= M23_RIGHT_INSTR;
	M[27] = PC;
	M[PSREG] |= 02003;
	if (addrmod) {
		M[PSSREG] |= M23_MOD_RK;
		addrmod = 0;
	}

	PC = 0500;
	PPK = 0;
	supmode = M23_INTERRUPT;
}

/* ОпПр1, ТО ч.9, стр. 119 */
void OpInt2() {
	M[PSSREG] = (M[PSREG] & 02003) | supmode;
	M[27] = PC;
	M[PSREG] |= 02003;
	if (addrmod) {
		M[PSSREG] |= M23_MOD_RK;
		addrmod = 0;
	}
	PC = 0501;
	PPK = 0;
	supmode = M23_INTERRUPT;
}

void IllegalInsn() {
	OpInt1();
	// M23_NEXT_RK is not important for this interrupt
	GRP |= 1 << 12;
}

void InsnCheck() {
	OpInt1();
	// M23_NEXT_RK must be 0 for this interrupt; it is already
	GRP |= 1 << 14;
}

void InsnProt() {
	OpInt1();
	// M23_NEXT_RK must be 1 for this interrupt
	M[PSSREG] |= M23_NEXT_RK;
	GRP |= 1 << 13;
}

void OperProt() {
	OpInt1();
	// M23_NEXT_RK can be 0 or 1; 0 means the standard PC rollback
	GRP |= 1 << 19;
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

		if (!iintr && !PPK && !M[17] & M17_INTR_DISABLE &&
			(GRP & MGRP)) {
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
