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
typedef struct {
	int op;				/* Условное слово обмена */
	int dev;			/* Номер устройства, 0..7 */
	int zone;			/* Номер зоны на диске */
	int memory;			/* Начальный адрес памяти */
	int status;			/* Регистр состояния */
	t_value mask_grp;		/* Маска готовности для ГРП */
	int mask_fail;			/* Маска ошибки обмена */
	t_value *sysdata;		/* Буфер системных данных */
} KMD;

static KMD controller [2];		/* Две стойки КМД */
int disk_fail;				/* Маска ошибок по направлениям */

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
{ "КУС_0",	&controller[0].op,	8, 24, 0, 1 },
{ "УСТР_0",	&controller[0].dev,	8, 3, 0, 1 },
{ "ЗОНА_0",	&controller[0].zone,	8, 10,  0, 1 },
{ "МОЗУ_0",	&controller[0].memory,	8, 20, 0, 1 },
{ "РС_0",	&controller[0].status,	8, 24, 0, 1 },
{ "КУС_1",	&controller[1].op,	8, 24, 0, 1 },
{ "УСТР_1",	&controller[1].dev,	8, 3, 0, 1 },
{ "ЗОНА_1",	&controller[1].zone,	8, 10,  0, 1 },
{ "МОЗУ_1",	&controller[1].memory,	8, 20, 0, 1 },
{ "РС_1",	&controller[1].status,	8, 24, 0, 1 },
{ "ОШ",		&disk_fail,		8, 6, 0, 1 },
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
 * Определение контроллера по устройству.
 */
static KMD *unit_to_ctlr (UNIT *u)
{
	if (u < &disk_unit[8])
		return &controller[0];
	else
		return &controller[1];
}

/*
 * Reset routine
 */
t_stat disk_reset (DEVICE *dptr)
{
	int i;

	memset (&controller, 0, sizeof (controller));
	controller[0].sysdata = &memory [030];
	controller[1].sysdata = &memory [040];
	controller[0].mask_grp = GRP_CHAN3_FREE;
	controller[1].mask_grp = GRP_CHAN4_FREE;
	controller[0].mask_fail = 020;
	controller[1].mask_fail = 010;
	for (i=0; i<16; ++i)
		sim_cancel (&disk_unit[i]);
	return SCPE_OK;
}

t_stat disk_attach (UNIT *u, char *cptr)
{
	KMD *c = unit_to_ctlr (u);
	t_stat s;

	s = attach_unit (u, cptr);
	if (s != SCPE_OK)
		return s;
	GRP |= c->mask_grp;
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
	KMD *c = unit_to_ctlr (u);

	if (disk_dev.dctrl)
		besm6_debug ("::: запись МД %o зона %04o память %05o-%05o",
			c->dev, c->zone, c->memory, c->memory + 1023);
	fseek (u->fileref, ZONE_SIZE * c->zone * 8, SEEK_SET);
	sim_fwrite (c->sysdata, 8, 8, u->fileref);
	sim_fwrite (&memory [c->memory], 8, 1024, u->fileref);
	if (ferror (u->fileref))
		longjmp (cpu_halt, SCPE_IOERR);
}

/*
 * Чтение с диска.
 */
void disk_read (UNIT *u)
{
	KMD *c = unit_to_ctlr (u);

	if (disk_dev.dctrl)
		besm6_debug ("::: чтение МД %o зона %04o память %05o-%05o",
			c->dev, c->zone, c->memory, c->memory + 1023);
	fseek (u->fileref, ZONE_SIZE * c->zone * 8, SEEK_SET);
	if (sim_fread (c->sysdata, 8, 8, u->fileref) != 8) {
		/* Чтение неинициализированного диска */
		disk_fail |= c->mask_fail;
		return;
	}
	if (! (c->op & DISK_READ_SYSDATA) &&
	    sim_fread (&memory[c->memory], 8, 1024, u->fileref) != 1024) {
		/* Чтение неинициализированного диска */
		disk_fail |= c->mask_fail;
		return;
	}
	if (ferror (u->fileref))
		longjmp (cpu_halt, SCPE_IOERR);
}

/*
 * Задание адреса памяти и длины массива для последующего обращения к диску.
 * Номера дисковода и дорожки будут выданы позже, командой 033 0023(0024).
 */
void disk_io (int ctlr, uint32 cmd)
{
	KMD *c = &controller [ctlr];

	c->op = cmd;
	if (! (c->op & DISK_PAGE_MODE) &&
	    ! (c->op & DISK_READ_SYSDATA)) {
		besm6_debug ("::: КМД %c: обмен полузоной не реализован, КУС=%08o",
			ctlr + '3', cmd);
		disk_fail |= c->mask_fail;
		return;
	}
	/* Обмен страницей */
	c->memory = (cmd & DISK_PAGE) >> 2 | (cmd & DISK_BLOCK) >> 8;
#if 0
	if (disk_dev.dctrl)
		besm6_debug ("::: КМД %c: задание на %s", ctlr + '3',
			(c->op & DISK_READ) ? "чтение" : "запись");
#endif
	disk_fail &= ~c->mask_fail;

	/* Гасим главный регистр прерываний. */
	GRP &= ~c->mask_grp;
}

/*
 * Управление диском: команда 00 033 0023(0024).
 */
void disk_ctl (int ctlr, uint32 cmd)
{
	KMD *c = &controller [ctlr];
	UNIT *u = &disk_unit [c->dev];

	if (cmd & BIT(12)) {
		/* Выдача в КМД адреса дорожки.
		 * Здесь же выполняем обмен с диском.
		 * Номер дисковода к этому моменту уже известен. */
		if ((disk_dev.flags & DEV_DIS) || ! u->fileref) {
			/* Device not attached. */
			disk_fail |= c->mask_fail;
			return;
		}
		c->zone = (cmd >> 1) & BITS(10);
		if (disk_dev.dctrl)
			besm6_debug ("::: КМД %c: выдача адреса дорожки, зона %04o",
				ctlr + '3', c->zone);
		disk_fail &= ~c->mask_fail;
		if (c->op & DISK_READ) {
			disk_read (u);
		} else {
			if (u->flags & UNIT_RO) {
				/* Read only. */
/*				longjmp (cpu_halt, SCPE_RO);*/
				disk_fail |= c->mask_fail;
				return;
			}
			disk_write (u);
		}

		/* Ждём события от устройства. */
		sim_activate (u, 20*USEC);	/* Ускорим для отладки. */

	} else if (cmd & BIT(11)) {
		/* Выбора номера устройства и занесение в регистр маски КМД.
		 * Бит 8 - устройство 0, бит 7 - устройство 1, ... бит 1 - устройство 7.
		 * Также установлен бит 9 - что он означает? */
		if      (cmd & BIT(8)) c->dev = 7;
		else if (cmd & BIT(7)) c->dev = 6;
		else if (cmd & BIT(6)) c->dev = 5;
		else if (cmd & BIT(5)) c->dev = 4;
		else if (cmd & BIT(4)) c->dev = 3;
		else if (cmd & BIT(3)) c->dev = 2;
		else if (cmd & BIT(2)) c->dev = 1;
		else if (cmd & BIT(1)) c->dev = 0;
		else {
			/* Неверная маска выбора устройства. */
			return;
		}
		c->dev += ctlr << 3;
		u = &disk_unit[c->dev];
#if 0
		if (disk_dev.dctrl)
			besm6_debug ("::: КМД %c: выбор устройства %d",
				ctlr + '3', c->dev);
#endif
		if ((disk_dev.flags & DEV_DIS) || ! u->fileref) {
			/* Device not attached. */
			disk_fail |= c->mask_fail;
		}
	} else if (cmd & BIT(9)) {
		/* Проверка прерывания от КМД? */
#if 0
		if (disk_dev.dctrl)
			besm6_debug ("::: КМД %c: проверка готовности",
				ctlr + '3');
#endif
		GRP |= c->mask_grp;

	} else {
		/* Команда, выдаваемая в КМД. */
		switch (cmd & 077) {
		case 000: /* диспак выдаёт эту команду один раз в начале загрузки */
			if (disk_dev.dctrl)
				besm6_debug ("::: КМД %c: недокументированная команда 00",
					ctlr + '3');
			break;
		case 001: /* сброс на 0 цилиндр */
#if 0
			if (disk_dev.dctrl)
				besm6_debug ("::: КМД %c: сброс на 0 цилиндр",
					ctlr + '3');
#endif
			break;
		case 002: /* подвод */
#if 0
			if (disk_dev.dctrl)
				besm6_debug ("::: КМД %c: подвод", ctlr + '3');
#endif
			break;
		case 003: /* чтение (НСМД-МОЗУ) */
#if 0
			if (disk_dev.dctrl)
				besm6_debug ("::: КМД %c чтение", ctlr + '3');
#endif
			break;
		case 004: /* запись (МОЗУ-НСМД) */
#if 0
			if (disk_dev.dctrl)
				besm6_debug ("::: КМД %c: запись", ctlr + '3');
#endif
			break;
		case 005: /* разметка */
			if (disk_dev.dctrl)
				besm6_debug ("::: КМД %c: разметка", ctlr + '3');
			break;
		case 006: /* сравнение кодов (МОЗУ-НСМД) */
#if 0
			if (disk_dev.dctrl)
				besm6_debug ("::: КМД %c: сравнение кодов", ctlr + '3');
#endif
			break;
		case 007: /* чтение заголовка */
#if 0
			if (disk_dev.dctrl)
				besm6_debug ("::: КМД %c: чтение заголовка", ctlr + '3');
#endif
			break;
		case 010: /* гашение PC */
#if 0
			if (disk_dev.dctrl)
				besm6_debug ("::: КМД %c: гашение регистра состояния",
					ctlr + '3');
#endif
			c->status = 0;
			break;
		case 011: /* опрос 1÷12 разрядов PC */
#if 0
			if (disk_dev.dctrl)
				besm6_debug ("::: КМД %c: опрос младших разрядов состояния",
					ctlr + '3');
#endif
/* Вычислено по текстам ОС Дубна.
 * Диспак доволен. */
#define STATUS_GOOD	0600001
			if (disk_unit[c->dev].fileref)
				c->status = (STATUS_GOOD << 8) & BITS(12);
			else
				c->status = 0;
			break;
		case 031: /* опрос 13÷24 разрядов РС */
#if 0
			if (disk_dev.dctrl)
				besm6_debug ("::: КМД %c: опрос старших разрядов состояния",
					ctlr + '3');
#endif
			if (disk_unit[c->dev].fileref)
				c->status = (STATUS_GOOD >> 8) & BITS(12);
			else
				c->status = 0;
			break;
		case 050: /* освобождение НМД */
#if 0
			if (disk_dev.dctrl)
				besm6_debug ("::: КМД %c: освобождение накопителя",
					ctlr + '3');
#endif
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
	KMD *c = &controller [ctlr];
#if 0
	if (disk_dev.dctrl)
		besm6_debug ("::: КМД %c: опрос состояния = %04o",
			ctlr + '3', c->status);
#endif
	return c->status;
}

/*
 * Событие: закончен обмен с МД.
 * Устанавливаем флаг прерывания.
 */
t_stat disk_event (UNIT *u)
{
	KMD *c = unit_to_ctlr (u);

	GRP |= c->mask_grp;
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
