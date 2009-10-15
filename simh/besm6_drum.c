/*
 * besm6_drum.c: BESM-6 magnetic drum device
 *
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
#define DRUM_BLOCK	       0140000000	/* номер блока памяти - 25-24 рр */
#define DRUM_PARAGRAF		000006000	/* номер абзаца */
#define DRUM_UNIT		000001600	/* номер барабана */
#define DRUM_CYLINDER		000000174	/* номер тракта на барабане */
#define DRUM_SECTOR		000000003	/* номер сектора */

/*
 * Параметры обмена с внешним устройством.
 */
int drum_op;			/* Условное слово обмена */
int drum_zone;			/* Номер зоны на барабане */
int drum_sector;		/* Начальный номер сектора на барабане */
int drum_memory;		/* Начальный адрес памяти */
int drum_nwords;		/* Количество слов обмена */

t_stat drum_event (UNIT *u);

/*
 * DRUM data structures
 *
 * drum_dev	DRUM device descriptor
 * drum_unit	DRUM unit descriptor
 * drum_reg	DRUM register list
 */
UNIT drum_unit [] = {
	{ UDATA (drum_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE, DRUM_SIZE) },
	{ UDATA (drum_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE, DRUM_SIZE) },
};

REG drum_reg[] = {
{ "УС",     &drum_op,		8, 24, 0, 1 },
{ "ЗОНА",   &drum_zone,		8, 10, 0, 1 },
{ "СЕКТОР", &drum_sector,	8, 2,  0, 1 },
{ "МОЗУ",   &drum_memory,	8, 15, 0, 1 },
{ "СЧСЛОВ", &drum_nwords,	8, 11, 0, 1 },
{ 0 }
};

MTAB drum_mod[] = {
	{ 0 }
};

t_stat drum_reset (DEVICE *dptr);
t_stat drum_attach (UNIT *uptr, char *cptr);
t_stat drum_detach (UNIT *uptr);

DEVICE drum_dev = {
	"DRUM", drum_unit, drum_reg, drum_mod,
	2, 8, 19, 1, 8, 50,
	NULL, NULL, &drum_reset, NULL, &drum_attach, &drum_detach,
	NULL, DEV_DISABLE | DEV_DEBUG
};

/*
 * Reset routine
 */
t_stat drum_reset (DEVICE *dptr)
{
	drum_op = 0;
	drum_zone = 0;
	drum_sector = 0;
	drum_memory = 0;
	drum_nwords = 0;
	sim_cancel (&drum_unit[0]);
	sim_cancel (&drum_unit[1]);
	return SCPE_OK;
}

t_stat drum_attach (UNIT *u, char *cptr)
{
	t_stat s;

	s = attach_unit (u, cptr);
	if (s != SCPE_OK)
		return s;
	if (u == &drum_unit[0])
		GRP |= GRP_DRUM1_FREE;
	else
		GRP |= GRP_DRUM2_FREE;
	return SCPE_OK;
}

t_stat drum_detach (UNIT *u)
{
	if (u == &drum_unit[0])
		GRP &= ~GRP_DRUM1_FREE;
	else
		GRP &= ~GRP_DRUM2_FREE;
	return detach_unit (u);
}

/*
 * Отладочная печать массива данных обмена.
 */
static void log_io (UNIT *u)
{
	t_value *data, *sysdata;
	int i;
	void print_word (t_value val) {
		fprintf (sim_log, " %o-%04o-%04o-%04o-%04o",
			(int) (val >> 48) & 07,
			(int) (val >> 36) & 07777,
			(int) (val >> 24) & 07777,
			(int) (val >> 12) & 07777, (int) val & 07777);
	}

	data = &memory [drum_memory];
	sysdata = (u == &drum_unit[0]) ? &memory [010] : &memory [020];
	if (drum_nwords == 1024) {
		fprintf (sim_log, "=== зона МБ %d.%03o:",
			(u == &drum_unit[0]) ? 1 : 2, drum_zone);
		for (i=0; i<8; ++i)
			print_word (sysdata[i]);
	} else {
		sysdata += drum_sector*2;
		fprintf (sim_log, "=== сектор МБ %d.%03o.%o:",
			(u == &drum_unit[0]) ? 1 : 2,
			drum_zone, drum_sector);
		for (i=0; i<2; ++i)
			print_word (sysdata[i]);
	}
	if (! (drum_op & DRUM_READ_SYSDATA)) {
		fprintf (sim_log, "\n\t\t  ");
		for (i=0; i<drum_nwords; ++i)
			print_word (data[i]);
	}
        fprintf (sim_log, "\n");
}

/*
 * Запись на барабан.
 */
void drum_write (UNIT *u)
{
	t_value *sysdata;

	sysdata = (u == &drum_unit[0]) ? &memory [010] : &memory [020];
	fseek (u->fileref, ZONE_SIZE * drum_zone * 8, SEEK_SET);
	sim_fwrite (sysdata, 8, 8, u->fileref);
	sim_fwrite (&memory [drum_memory], 8, 1024, u->fileref);
	if (ferror (u->fileref))
		longjmp (cpu_halt, SCPE_IOERR);
}

void drum_write_sector (UNIT *u)
{
	t_value *sysdata;

	sysdata = (u == &drum_unit[0]) ? &memory [010] : &memory [020];
	fseek (u->fileref, (ZONE_SIZE*drum_zone + drum_sector*2) * 8,
		SEEK_SET);
	sim_fwrite (&sysdata [drum_sector*2], 8, 2, u->fileref);
	fseek (u->fileref, (ZONE_SIZE*drum_zone + 8 + drum_sector*256) * 8,
		SEEK_SET);
	sim_fwrite (&memory [drum_memory], 8, 256, u->fileref);
	if (ferror (u->fileref))
		longjmp (cpu_halt, SCPE_IOERR);
}

/*
 * Чтение с барабана.
 */
void drum_read (UNIT *u)
{
	t_value *sysdata;

	sysdata = (u == &drum_unit[0]) ? &memory [010] : &memory [020];
	fseek (u->fileref, ZONE_SIZE * drum_zone * 8, SEEK_SET);
	if (sim_fread (sysdata, 8, 8, u->fileref) != 8) {
		/* Чтение неинициализированного барабана */
		longjmp (cpu_halt, STOP_DRUMINVDATA);
	}
	if (! (drum_op & DRUM_READ_SYSDATA) &&
	    sim_fread (&memory[drum_memory], 8, 1024, u->fileref) != 1024) {
		/* Чтение неинициализированного барабана */
		longjmp (cpu_halt, STOP_DRUMINVDATA);
	}
	if (ferror (u->fileref))
		longjmp (cpu_halt, SCPE_IOERR);
}

void drum_read_sector (UNIT *u)
{
	t_value *sysdata;

	sysdata = (u == &drum_unit[0]) ? &memory [010] : &memory [020];
	fseek (u->fileref, (ZONE_SIZE*drum_zone + drum_sector*2) * 8, SEEK_SET);
	if (sim_fread (&sysdata [drum_sector*2], 8, 2, u->fileref) != 2) {
		/* Чтение неинициализированного барабана */
		longjmp (cpu_halt, STOP_DRUMINVDATA);
	}
	if (! (drum_op & DRUM_READ_SYSDATA)) {
		fseek (u->fileref, (ZONE_SIZE*drum_zone + 8 + drum_sector*256) * 8,
			SEEK_SET);
		if (sim_fread (&memory[drum_memory], 8, 256, u->fileref) != 256) {
			/* Чтение неинициализированного барабана */
			longjmp (cpu_halt, STOP_DRUMINVDATA);
		}
	}
	if (ferror (u->fileref))
		longjmp (cpu_halt, SCPE_IOERR);
}

/*
 * Выполнение обращения к барабану.
 */
void drum (int ctlr, uint32 cmd)
{
	UNIT *u = &drum_unit[ctlr];

	drum_op = cmd;
	if (drum_op & DRUM_PAGE_MODE) {
		/* Обмен страницей */
		drum_nwords = 1024;
		drum_zone = (cmd & (DRUM_UNIT | DRUM_CYLINDER)) >> 2;
		drum_sector = 0;
		drum_memory = (cmd & DRUM_PAGE) >> 2 | (cmd & DRUM_BLOCK) >> 8;
		if (drum_dev.dctrl)
			besm6_debug ("### %s МБ %c%d зона %02o память %05o-%05o",
				(drum_op & DRUM_READ) ? "чтение" : "запись",
				ctlr + '1', (drum_zone >> 5 & 7), drum_zone & 037,
				drum_memory, drum_memory + drum_nwords - 1);
	} else {
		/* Обмен сектором */
		drum_nwords = 256;
		drum_zone = (cmd & (DRUM_UNIT | DRUM_CYLINDER)) >> 2;
		drum_sector = cmd & DRUM_SECTOR;
		drum_memory = (cmd & (DRUM_PAGE | DRUM_PARAGRAF)) >> 2 | (cmd & DRUM_BLOCK) >> 8;
		if (drum_dev.dctrl)
			besm6_debug ("### %s МБ %c%d зона %02o сектор %d память %05o-%05o",
				(drum_op & DRUM_READ) ? "чтение" : "запись",
				ctlr + '1', (drum_zone >> 5 & 7), drum_zone & 037,
				drum_sector & 3,
				drum_memory, drum_memory + drum_nwords - 1);
	}
	if ((drum_dev.flags & DEV_DIS) || ! u->fileref) {
		/* Device not attached. */
		longjmp (cpu_halt, SCPE_UNATT);
	}
	if (drum_op & DRUM_READ_OVERLAY) {
		/* Not implemented. */
		longjmp (cpu_halt, SCPE_NOFNC);
	}
	if (drum_op & DRUM_READ) {
		if (drum_op & DRUM_PAGE_MODE)
			drum_read (u);
		else
			drum_read_sector (u);
	} else {
		if (drum_op & DRUM_PARITY_FLAG) {
			besm6_log ("### запись МБ с неправильной чётностью не реализована");
			longjmp (cpu_halt, SCPE_NOFNC);
		}
		if (u->flags & UNIT_RO) {
			/* Read only. */
			longjmp (cpu_halt, SCPE_RO);
		}
		if (drum_op & DRUM_PAGE_MODE)
			drum_write (u);
		else
			drum_write_sector (u);
	}
	/*if (drum_dev.dctrl && sim_log)
		log_io (u);*/

	/* Гасим главный регистр прерываний. */
	if (u == &drum_unit[0])
		GRP &= ~GRP_DRUM1_FREE;
	else
		GRP &= ~GRP_DRUM2_FREE;

	/* Ждём события от устройства.
	 * Согласно данным из книжки Мазного Г.Л.,
	 * даём 20 мсек на обмен, или 200 тыс.тактов. */
/*	sim_activate (u, 200000);*/

	/* Ускорим для отладки. */
	sim_activate (u, 2000);
}

/*
 * Событие: закончен обмен с МБ.
 * Устанавливаем флаг прерывания.
 */
t_stat drum_event (UNIT *u)
{
	if (u == &drum_unit[0])
		GRP |= GRP_DRUM1_FREE;
	else
		GRP |= GRP_DRUM2_FREE;
	return SCPE_OK;
}
