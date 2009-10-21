/*
 * BESM-6 magnetic disk device
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
 * Управляющее слово обмена с магнитным диском.
 */
#define DISK_BLOCK	       0740000000	/* номер блока памяти - 27-24 рр */
#define DISK_READ_SYSDATA	004000000	/* считывание только служебных слов */
#define DISK_PAGE_MODE		001000000	/* обмен целой страницей */
#define DISK_READ		000400000	/* чтение с диска в память */
#define DISK_PAGE		000370000	/* номер страницы памяти */
#define DISK_HALFPAGE		000004000	/* выбор половины листа */
#define DISK_UNIT		000001600	/* номер устройства */
#define DISK_HALFZONE		000000001	/* выбор половины зоны */

/*
 * Параметры обмена с внешним устройством.
 */
int disk_op;			/* Условное слово обмена */
int disk_no;			/* Номер устройства, 0..15 */
int disk_zone;			/* Номер зоны на диске */
int disk_track;			/* Выбор половины зоны на диске */
int disk_memory;		/* Начальный адрес памяти */
int disk_nwords;		/* Количество слов обмена */
int disk_status;		/* Регистр состояния */
int disk_fail;			/* Маска ошибок по направлениям */

t_stat disk_event (UNIT *u);

/*
 * DISK data structures
 *
 * disk_dev	DISK device descriptor
 * disk_unit	DISK unit descriptor
 * disk_reg	DISK register list
 */
UNIT disk_unit [16] = {
	{ UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE, DISK_SIZE) },
	{ UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE, DISK_SIZE) },
	{ UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE, DISK_SIZE) },
	{ UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE, DISK_SIZE) },
	{ UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE, DISK_SIZE) },
	{ UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE, DISK_SIZE) },
	{ UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE, DISK_SIZE) },
	{ UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE, DISK_SIZE) },
	{ UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE, DISK_SIZE) },
	{ UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE, DISK_SIZE) },
	{ UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE, DISK_SIZE) },
	{ UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE, DISK_SIZE) },
	{ UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE, DISK_SIZE) },
	{ UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE, DISK_SIZE) },
	{ UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE, DISK_SIZE) },
	{ UDATA (disk_event, UNIT_FIX+UNIT_ATTABLE+UNIT_ROABLE, DISK_SIZE) },
};

REG disk_reg[] = {
{ "УС",     &disk_op,		8, 24, 0, 1 },
{ "ЗОНА",   &disk_zone,		8, 10, 0, 1 },
{ "ДОРОЖКА",&disk_track,	8, 2,  0, 1 },
{ "МОЗУ",   &disk_memory,	8, 15, 0, 1 },
{ "СЧСЛОВ", &disk_nwords,	8, 11, 0, 1 },
{ 0 }
};

MTAB disk_mod[] = {
	{ 0 }
};

t_stat disk_reset (DEVICE *dptr);
t_stat disk_attach (UNIT *uptr, char *cptr);
t_stat disk_detach (UNIT *uptr);

DEVICE disk_dev = {
	"DISK", disk_unit, disk_reg, disk_mod,
	16, 8, 21, 1, 8, 50,
	NULL, NULL, &disk_reset, NULL, &disk_attach, &disk_detach,
	NULL, DEV_DISABLE | DEV_DEBUG
};

/*
 * Reset routine
 */
t_stat disk_reset (DEVICE *dptr)
{
	int i;

	disk_op = 0;
	disk_zone = 0;
	disk_track = 0;
	disk_memory = 0;
	disk_nwords = 0;
	for (i=0; i<16; ++i)
		sim_cancel (&disk_unit[i]);
	return SCPE_OK;
}

t_stat disk_attach (UNIT *u, char *cptr)
{
	t_stat s;

	s = attach_unit (u, cptr);
	if (s != SCPE_OK)
		return s;
	if (u < &disk_unit[8])
		GRP |= GRP_CHAN3_FREE;
	else
		GRP |= GRP_CHAN4_FREE;
	return SCPE_OK;
}

t_stat disk_detach (UNIT *u)
{
	/* TODO: сброс бита ГРП готовности направления при отключении последнего диска. */
	return detach_unit (u);
}

/*
 * Запись на диск.
 */
void disk_write (UNIT *u)
{
	int ctlr;
	t_value *sysdata;

	if (disk_dev.dctrl)
		besm6_debug ("::: запись МД %o зона %04o память %05o-%05o",
			disk_no, disk_zone, disk_memory,
			disk_memory + disk_nwords - 1);
	ctlr = (u >= &disk_unit[8]);
	sysdata = ctlr ? &memory [040] : &memory [030];
	fseek (u->fileref, ZONE_SIZE * disk_zone * 8, SEEK_SET);
	sim_fwrite (sysdata, 8, 8, u->fileref);
	sim_fwrite (&memory [disk_memory], 8, 1024, u->fileref);
	if (ferror (u->fileref))
		longjmp (cpu_halt, SCPE_IOERR);
}

void disk_write_track (UNIT *u)
{
	int ctlr;
	t_value *sysdata;

	if (disk_dev.dctrl)
		besm6_debug ("::: запись МД %o зона %04o дорожка %d память %05o-%05o",
			disk_no, disk_zone, disk_track, disk_memory,
			disk_memory + disk_nwords - 1);
	ctlr = (u >= &disk_unit[8]);
	sysdata = ctlr ? &memory [040] : &memory [030];
	fseek (u->fileref, (ZONE_SIZE*disk_zone + disk_track*4) * 8,
		SEEK_SET);
	sim_fwrite (&sysdata [disk_track*4], 8, 4, u->fileref);
	fseek (u->fileref, (ZONE_SIZE*disk_zone + 8 + disk_track*512) * 8,
		SEEK_SET);
	sim_fwrite (&memory [disk_memory], 8, 512, u->fileref);
	if (ferror (u->fileref))
		longjmp (cpu_halt, SCPE_IOERR);
}

/*
 * Чтение с диска.
 */
void disk_read (UNIT *u)
{
	int ctlr;
	t_value *sysdata;

	if (disk_dev.dctrl)
		besm6_debug ("::: чтение МД %o зона %04o память %05o-%05o",
			disk_no, disk_zone, disk_memory,
			disk_memory + disk_nwords - 1);
	ctlr = (u >= &disk_unit[8]);
	sysdata = ctlr ? &memory [040] : &memory [030];
	fseek (u->fileref, ZONE_SIZE * disk_zone * 8, SEEK_SET);
	if (sim_fread (sysdata, 8, 8, u->fileref) != 8) {
		/* Чтение неинициализированного диска */
		disk_fail |= 020 >> ctlr;
		return;
	}
	if (! (disk_op & DISK_READ_SYSDATA) &&
	    sim_fread (&memory[disk_memory], 8, 1024, u->fileref) != 1024) {
		/* Чтение неинициализированного диска */
		disk_fail |= 020 >> ctlr;
		return;
	}
	if (ferror (u->fileref))
		longjmp (cpu_halt, SCPE_IOERR);
}

void disk_read_track (UNIT *u)
{
	int ctlr;
	t_value *sysdata;

	if (disk_dev.dctrl)
		besm6_debug ("::: чтение МД %o зона %04o дорожка %d память %05o-%05o",
			disk_no, disk_zone, disk_track, disk_memory,
			disk_memory + disk_nwords - 1);
	ctlr = (u >= &disk_unit[8]);
	sysdata = ctlr ? &memory [040] : &memory [030];
	fseek (u->fileref, (ZONE_SIZE*disk_zone + disk_track*4) * 8, SEEK_SET);
	if (sim_fread (&sysdata [disk_track*4], 8, 4, u->fileref) != 4) {
		/* Чтение неинициализированного диска */
		disk_fail |= 020 >> ctlr;
		return;
	}
	if (! (disk_op & DISK_READ_SYSDATA)) {
		fseek (u->fileref, (ZONE_SIZE*disk_zone + 8 + disk_track*512) * 8,
			SEEK_SET);
		if (sim_fread (&memory[disk_memory], 8, 512, u->fileref) != 512) {
			/* Чтение неинициализированного диска */
			disk_fail |= 020 >> ctlr;
			return;
		}
	}
	if (ferror (u->fileref))
		longjmp (cpu_halt, SCPE_IOERR);
}

/*
 * Обращение к диску.
 */
void disk_io (int ctlr, uint32 cmd)
{
	UNIT *u;

	disk_op = cmd;
	disk_no = (cmd & DISK_UNIT) >> 7 | ctlr << 3;
	u = &disk_unit [disk_no];
	if (disk_op & DISK_PAGE_MODE) {
		/* Обмен страницей */
		disk_nwords = 1024;
		disk_track = 0;
		disk_memory = (cmd & DISK_PAGE) >> 2 | (cmd & DISK_BLOCK) >> 8;
	} else {
		/* Обмен половиной страницы (дорожкой) */
		disk_nwords = 512;
		disk_track = cmd & DISK_HALFZONE;
		disk_memory = (cmd & (DISK_PAGE | DISK_HALFPAGE)) >> 2 | (cmd & DISK_BLOCK) >> 8;
	}
	if (disk_dev.dctrl)
		besm6_debug ("::: КМД %c: задание на %s", ctlr + '3',
			(disk_op & DISK_READ) ? "чтение" : "запись");

	if (! (disk_op & DISK_READ) && (u->flags & UNIT_RO)) {
		/* Read only. */
		longjmp (cpu_halt, SCPE_RO);
/*		disk_fail |= 020 >> ctlr;*/
/*		return;*/
	}
	if ((disk_dev.flags & DEV_DIS) || ! u->fileref) {
		/* Device not attached. */
		disk_fail |= 020 >> ctlr;
		return;
	}
	disk_fail &= ~(020 >> ctlr);

	/* Гасим главный регистр прерываний. */
	if (u < &disk_unit[8])
		GRP &= ~GRP_CHAN3_FREE;
	else
		GRP &= ~GRP_CHAN4_FREE;
}

/*
 * Управление диском: команда 00 033 0023(0024).
 */
void disk_ctl (int ctlr, uint32 cmd)
{
	if (cmd & BIT(12)) {
		UNIT *u = &disk_unit[disk_no];

		/* Выдача в КМД адреса дорожки. */
		if ((disk_dev.flags & DEV_DIS) || ! u->fileref) {
			/* Device not attached. */
			disk_fail |= 020 >> ctlr;
			return;
		}
		disk_fail &= ~(020 >> ctlr);
		disk_zone = (cmd >> 1) & BITS(10);
		if (disk_dev.dctrl)
			besm6_debug ("::: КМД %c: выдача адреса дорожки %04o",
				ctlr + '3', disk_zone);
		if (disk_op & DISK_READ) {
			if (disk_op & DISK_PAGE_MODE)
				disk_read (u);
			else
				disk_read_track (u);
		} else {
			if (disk_op & DISK_PAGE_MODE)
				disk_write (u);
			else
				disk_write_track (u);
		}

		/* Ждём события от устройства. */
		sim_activate (u, 20*USEC);	/* Ускорим для отладки. */

	} else if (cmd & BIT(11)) {
		/* Выбора номера устройства и занесение в регистр маски КМД.
		 * Бит 8 - устройство 0, бит 7 - устройство 1, ... бит 1 - устройство 7.
		 * Также установлен бит 9 - что он означает? */
		if      (cmd & BIT(8)) disk_no = 7;
		else if (cmd & BIT(7)) disk_no = 6;
		else if (cmd & BIT(6)) disk_no = 5;
		else if (cmd & BIT(5)) disk_no = 4;
		else if (cmd & BIT(4)) disk_no = 3;
		else if (cmd & BIT(3)) disk_no = 2;
		else if (cmd & BIT(2)) disk_no = 1;
		else if (cmd & BIT(1)) disk_no = 0;
		else {
			/* Неверная маска выбора устройства. */
			return;
		}
		disk_no += ctlr << 3;
		if (disk_dev.dctrl)
			besm6_debug ("::: КМД %c: выбор устройства %d",
				ctlr + '3', disk_no);

	} else if (cmd & BIT(9)) {
		/* Проверка прерывания от КМД? */
		if (disk_dev.dctrl)
			besm6_debug ("::: КМД %c: проверка готовности",
				ctlr + '3');
		if (ctlr == 0)
			GRP |= GRP_CHAN3_FREE;
		else
			GRP |= GRP_CHAN4_FREE;

	} else {
		/* Команда, выдаваемая в КМД. */
		switch (cmd & 077) {
		case 000: /* диспак выдаёт эту команду один раз в начале загрузки */
			if (disk_dev.dctrl)
				besm6_debug ("::: КМД %c: недокументированная команда 00",
					ctlr + '3');
			break;
		case 001: /* сброс на 0 цилиндр */
			if (disk_dev.dctrl)
				besm6_debug ("::: КМД %c: сброс на 0 цилиндр",
					ctlr + '3');
			break;
		case 002: /* подвод */
			if (disk_dev.dctrl)
				besm6_debug ("::: КМД %c: подвод", ctlr + '3');
			break;
		case 003: /* чтение (НСМД-МОЗУ) */
			if (disk_dev.dctrl)
				besm6_debug ("::: КМД %c чтение", ctlr + '3');
			break;
		case 004: /* запись (МОЗУ-НСМД) */
			if (disk_dev.dctrl)
				besm6_debug ("::: КМД %c: запись", ctlr + '3');
			break;
		case 005: /* разметка */
			if (disk_dev.dctrl)
				besm6_debug ("::: КМД %c: разметка", ctlr + '3');
			break;
		case 006: /* сравнение кодов (МОЗУ-НСМД) */
			if (disk_dev.dctrl)
				besm6_debug ("::: КМД %c: сравнение кодов", ctlr + '3');
			break;
		case 007: /* чтение заголовка */
			if (disk_dev.dctrl)
				besm6_debug ("::: КМД %c: чтение заголовка", ctlr + '3');
			break;
		case 010: /* гашение PC */
			if (disk_dev.dctrl)
				besm6_debug ("::: КМД %c: гашение регистра состояния",
					ctlr + '3');
			disk_status = 0;
			break;
		case 011: /* опрос 1÷12 разрядов PC */
			if (disk_dev.dctrl)
				besm6_debug ("::: КМД %c: опрос младших разрядов состояния",
					ctlr + '3');
/* Вычислено по текстам ОС Дубна.
 * Диспак доволен. */
#define STATUS_GOOD	0600001
			if (disk_unit[disk_no].fileref)
				disk_status = (STATUS_GOOD << 8) & BITS(12);
			else
				disk_status = 0;
			break;
		case 031: /* опрос 13÷24 разрядов РС */
			if (disk_dev.dctrl)
				besm6_debug ("::: КМД %c: опрос старших разрядов состояния",
					ctlr + '3');
			if (disk_unit[disk_no].fileref)
				disk_status = (STATUS_GOOD >> 8) & BITS(12);
			else
				disk_status = 0;
			break;
		case 050: /* освобождение НМД */
			if (disk_dev.dctrl)
				besm6_debug ("::: КМД %c: освобождение накопителя",
					ctlr + '3');
			break;
		default:
			besm6_debug ("::: КМД %c: неизвестная команда %02o",
				ctlr + '3', cmd & 077);
			break;
		}
	}
}

/*
 * Запрос состояния контроллера.
 */
int disk_state (int ctlr)
{
	if (disk_dev.dctrl)
		besm6_debug ("::: КМД %c: опрос состояния = %04o",
			ctlr + '3', disk_status);
	return disk_status;
}

/*
 * Событие: закончен обмен с МД.
 * Устанавливаем флаг прерывания.
 */
t_stat disk_event (UNIT *u)
{
	if (u < &disk_unit[8])
		GRP |= GRP_CHAN3_FREE; /* _DONE? */
	else
		GRP |= GRP_CHAN4_FREE;
	return SCPE_OK;
}

/*
 * Опрос ошибок обмена командой 033 4035.
 */
int disk_errors ()
{
	if (disk_dev.dctrl)
		besm6_debug ("::: КМД: опрос шкалы ошибок = %04o", disk_fail);
	return disk_fail;
}
