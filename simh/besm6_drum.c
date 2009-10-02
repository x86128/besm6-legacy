/*
 * besm6_drum.c: BESM-6 magnetic drum device
 *
 * Copyright (c) 2009, Serge Vakulenko
 */
#include "besm6_defs.h"

/*
 * Управляющее слово обмена с магнитным барабаном.
 */
#define DRUM_READ_OVERLAY	020000000	/* считывание с наложением */
#define DRUM_PARITY_FLAG	010000000	/* блокировка считывания слов с неверной
						 * чётностью или запись с неверной чётностью */
#define DRUM_READ_SYSDATA	004000000	/* считывание только служебных слов */
#define DRUM_PAGE_MODE		001000000	/* обмен целой страницей */
#define DRUM_READ		000400000	/* чтение с барабана в память */
#define DRUM_PAGE		000370000	/* номер страницы памяти */
#define DRUM_PARAGRAF		000006000	/* номер абзаца */
#define DRUM_UNIT		000001600	/* номер барабана */
#define DRUM_CYLINDER		000000174	/* номер тракта на барабане */
#define DRUM_SECTOR		000000003	/* номер сектора */

/*
 * Параметры обмена с внешним устройством.
 */
int drum_op;			/* УЧ - условное число */
int drum_sector_start;		/* А_МЗУ - начальный адрес на барабане */
int drum_memory_start;		/* α_МОЗУ - начальный адрес памяти */
int drum_nwords;		/* ω_МОЗУ - конечный адрес памяти */

/*
 * DRUM data structures
 *
 * drum_dev	DRUM device descriptor
 * drum_unit	DRUM unit descriptor
 * drum_reg	DRUM register list
 */
UNIT drum_unit = {
	UDATA (NULL, UNIT_FIX+UNIT_ATTABLE, DRUM_SIZE)
};

REG drum_reg[] = {
{ "УС",     &drum_op,            8, 24, 0, 1 },
{ "СЕКТОР", &drum_sector_start,  8, 12, 0, 1 },
{ "МОЗУ",   &drum_memory_start,  8, 15, 0, 1 },
{ "СЧСЛОВ", &drum_nwords,        8, 12, 0, 1 },
{ 0 }
};

MTAB drum_mod[] = {
	{ 0 }
};

t_stat drum_reset (DEVICE *dptr);

DEVICE drum_dev = {
	"DRUM", &drum_unit, drum_reg, drum_mod,
	1, 8, 19, 1, 8, 50,
	NULL, NULL, &drum_reset,
	NULL, NULL, NULL, NULL,
	DEV_DISABLE | DEV_DEBUG
};

/*
 * Reset routine
 */
t_stat drum_reset (DEVICE *dptr)
{
	drum_op = 0;
	drum_sector_start = 0;
	drum_memory_start = 0;
	drum_nwords = 0;
	sim_cancel (&drum_unit);
	return SCPE_OK;
}

/*
 * Запись на барабан.
 */
void drum_write (int addr, int first, int nwords, int invert_parity)
{
	int i;

	if (sim_deb && drum_dev.dctrl)
		fprintf (sim_deb, "*** запись МБ %04o память %05o-%05o\n",
			addr, first, first+nwords-1);
	fseek (drum_unit.fileref, addr*8, SEEK_SET);
	fxwrite (&memory[first], 8, nwords, drum_unit.fileref);
	if (ferror (drum_unit.fileref))
		longjmp (cpu_halt, SCPE_IOERR);
	/* TODO: запись служебных слов */
}

/*
 * Чтение с барабана.
 */
void drum_read (int addr, int first, int nwords, int sysdata_only)
{
	int i;
	t_value old_sum;

	if (sim_deb && drum_dev.dctrl)
		fprintf (sim_deb, "*** чтение МБ %04o память %05o-%05o\n",
			addr, first, first+nwords-1);
	fseek (drum_unit.fileref, addr*8, SEEK_SET);
	i = fxread (&memory[first], 8, nwords, drum_unit.fileref);
	if (ferror (drum_unit.fileref))
		longjmp (cpu_halt, SCPE_IOERR);
	if (i != nwords) {
		/* Чтение неинициализированного барабана */
		longjmp (cpu_halt, STOP_DRUMINVDATA);
	}
	/* TODO: чтение служебных слов */
}

/*
 * Выполнение обращения к барабану.
 */
void drum (int ctlr, uint32 cmd)
{
	if (drum_dev.flags & DEV_DIS) {
		/* Device not attached. */
		longjmp (cpu_halt, SCPE_UNATT);
	}
	drum_op = cmd;
	if (drum_op & DRUM_PAGE_MODE) {
		/* Обмен страницей */
		drum_nwords = 1024;
		drum_sector_start = (ctlr << 10) |
			(cmd & (DRUM_UNIT | DRUM_CYLINDER));
		drum_memory_start = (cmd & DRUM_PAGE) >> 2;
	} else {
		/* Обмен сектором */
		drum_nwords = 256;
		drum_sector_start = (ctlr << 10) |
			(cmd & (DRUM_UNIT | DRUM_CYLINDER | DRUM_SECTOR));
		drum_memory_start = (cmd & (DRUM_PAGE | DRUM_PARAGRAF)) >> 2;
	}
	if (drum_op & DRUM_READ)
		drum_read (drum_sector_start, drum_memory_start, drum_nwords,
			drum_op & DRUM_READ_SYSDATA);
	else
		drum_write (drum_sector_start, drum_memory_start, drum_nwords,
			drum_op & DRUM_PARITY_FLAG);
}
