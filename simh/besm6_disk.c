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
 * "Хороший" статус чтения/записи.
 * Вычислено по текстам ОС Дубна.
 * Диспак доволен.
 */
#define STATUS_GOOD	014000400

/*
 * Параметры обмена с внешним устройством.
 */
typedef struct {
	int op;				/* Условное слово обмена */
	int dev;			/* Номер устройства, 0..7 */
	int zone;			/* Номер зоны на диске */
	int track;			/* Выбор половины зоны на диске */
	int memory;			/* Начальный адрес памяти */
	int format;			/* Флаг разметки */
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
{ "ДОРОЖКА_0",	&controller[0].track,	8, 2,  0, 1 },
{ "МОЗУ_0",	&controller[0].memory,	8, 20, 0, 1 },
{ "РС_0",	&controller[0].status,	8, 24, 0, 1 },
{ "КУС_1",	&controller[1].op,	8, 24, 0, 1 },
{ "УСТР_1",	&controller[1].dev,	8, 3, 0, 1 },
{ "ЗОНА_1",	&controller[1].zone,	8, 10,  0, 1 },
{ "ДОРОЖКА_1",	&controller[1].track,	8, 2,  0, 1 },
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

t_value spread (t_value val)
{
	int i, j;
	t_value res = 0;
	for (i = 0; i < 5; i++) for (j = 0; j < 9; j++)
		if (val & (1LL<<(i+j*5)))
			res |= 1LL << (i*9+j);
	return res & BITS48;
}

/*
 * Отладочная печать массива данных обмена.
 */
static void log_data (t_value *data, int nwords)
{
	int i;
	t_value val;

	for (i=0; i<nwords; ++i) {
		val = data[i];
		fprintf (sim_log, " %04o-%04o-%04o-%04o",
			(int) (val >> 36) & 07777,
			(int) (val >> 24) & 07777,
			(int) (val >> 12) & 07777,
			(int) val & 07777);
		if ((i & 3) == 3)
			fprintf (sim_log, "\n");
	}
	if ((i & 3) != 0)
		fprintf (sim_log, "\n");
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

void disk_write_track (UNIT *u)
{
	KMD *c = unit_to_ctlr (u);

	if (disk_dev.dctrl)
		besm6_debug ("::: запись МД %o полузона %04o.%d память %05o-%05o",
			c->dev, c->zone, c->track, c->memory, c->memory + 511);
	fseek (u->fileref, (ZONE_SIZE*c->zone + 4*c->track) * 8, SEEK_SET);
	sim_fwrite (c->sysdata + 4*c->track, 8, 4, u->fileref);
	fseek (u->fileref, (8 + ZONE_SIZE*c->zone + 512*c->track) * 8,
		SEEK_SET);
	sim_fwrite (&memory [c->memory], 8, 512, u->fileref);
	if (ferror (u->fileref))
		longjmp (cpu_halt, SCPE_IOERR);
}

void disk_format (UNIT *u)
{
	KMD *c = unit_to_ctlr (u);

	t_value * ptr = &memory[c->memory];
	t_value fmtbuf[5];
	int i;
	if (disk_dev.dctrl) {
		besm6_debug ("::: формат МД %o полузона %04o.%d память %05o-%05o",
			c->dev, c->zone, c->track, c->memory, c->memory + 511);
		log_data (&memory [c->memory], 48);
	}
	/* Находим начало записываемого заголовка */
	while ((*ptr & BITS48) == 0) ptr++;

	/* Декодируем из гребенки в нормальный вид */
	for (i = 0; i < 5; i++) {
		fmtbuf[i] = spread(ptr[i]);
	}
	/* При первой попытке разметки адресный маркер начинается в старшем 5-разрядном слоге,
	 * пропускаем первый слог.
	 */
	for (i = 0; i < 4; i++) {
		fmtbuf[i] = ((fmtbuf[i] & BITS48) << 5) | ((fmtbuf[i+1] >> 40) & BITS(5));
	}
	/* Сохраняем в нормальном виде - но с контролем числа - для последующего чтения заголовка */
	fseek (u->fileref, (ZONE_SIZE*c->zone + 4*c->track) * 8, SEEK_SET);
	sim_fwrite (fmtbuf, 8, 4, u->fileref);
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
		besm6_debug ((c->op & DISK_READ_SYSDATA) ?
			"::: чтение МД %o зона %04o служебные слова" :
			"::: чтение МД %o зона %04o память %05o-%05o",
			c->dev, c->zone, c->memory, c->memory + 1023);
	fseek (u->fileref, ZONE_SIZE * c->zone * 8, SEEK_SET);
	if (sim_fread (c->sysdata, 8, 8, u->fileref) != 8) {
		/* Чтение неинициализированного диска */
		disk_fail |= c->mask_fail;
		return;
	}
	if (! (c->op & DISK_READ_SYSDATA) &&
	    sim_fread (&memory [c->memory], 8, 1024, u->fileref) != 1024) {
		/* Чтение неинициализированного диска */
		disk_fail |= c->mask_fail;
		return;
	}
	if (ferror (u->fileref))
		longjmp (cpu_halt, SCPE_IOERR);
}

t_value collect (t_value val)
{
	int i, j;
	t_value res = 0;
	for (i = 0; i < 5; i++) for (j = 0; j < 9; j++)
		if (val & (1LL<<(i*9+j)))
			res |= 1LL << (i+j*5);
	return res & BITS48;
}

void disk_read_track (UNIT *u)
{
	KMD *c = unit_to_ctlr (u);

	if (disk_dev.dctrl)
		besm6_debug ((c->op & DISK_READ_SYSDATA) ?
			"::: чтение МД %o полузона %04o.%d служебные слова" :
			"::: чтение МД %o полузона %04o.%d память %05o-%05o",
			c->dev, c->zone, c->track, c->memory, c->memory + 511);
	fseek (u->fileref, (ZONE_SIZE*c->zone + 4*c->track) * 8, SEEK_SET);
	if (sim_fread (c->sysdata + 4*c->track, 8, 4, u->fileref) != 4) {
		/* Чтение неинициализированного диска */
		disk_fail |= c->mask_fail;
		return;
	}
	if (c->format) {
		/* Чтение заголовка дорожки: отдаем заголовок, записанный предшествующей
		 * командой разметки, в формате гребенки.
		 */
		int i;
		t_value * sysdata = c->sysdata + 4*c->track;
		for (i = 0; i < 4; i++) {
			sysdata[i] = collect(sysdata[i]) | (2LL<<48);
		}
		return;
	} 
	if (! (c->op & DISK_READ_SYSDATA)) {
		fseek (u->fileref, (8 + ZONE_SIZE*c->zone + 512*c->track) * 8,
			SEEK_SET);
		if (sim_fread (&memory [c->memory], 8, 512, u->fileref) != 512) {
			/* Чтение неинициализированного диска */
			disk_fail |= c->mask_fail;
			return;
		}
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
	c->format = 0;
	if (c->op & DISK_PAGE_MODE) {
		/* Обмен страницей */
		c->memory = (cmd & DISK_PAGE) >> 2 | (cmd & DISK_BLOCK) >> 8;
	} else {
		/* Обмен половиной страницы (дорожкой) */
		c->memory = (cmd & (DISK_PAGE | DISK_HALFPAGE)) >> 2 | (cmd & DISK_BLOCK) >> 8;
	}
#if 0
	if (disk_dev.dctrl)
		besm6_debug ("::: КМД %c: задание на %s %08o", ctlr + '3',
			(c->op & DISK_READ) ? "чтение" : "запись", cmd);
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
/*static t_value buf [512];*/

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
		c->track = cmd & 1;
#if 0
		if (disk_dev.dctrl)
			besm6_debug ("::: КМД %c: выдача адреса дорожки %04o.%d",
				ctlr + '3', c->zone, c->track);
#endif
		disk_fail &= ~c->mask_fail;
		if (c->op & DISK_READ) {
			if (c->op & DISK_PAGE_MODE)
				disk_read (u);
			else
				disk_read_track (u);
		} else {
			if (u->flags & UNIT_RO) {
				/* Read only. */
				/*longjmp (cpu_halt, SCPE_RO);*/
				disk_fail |= c->mask_fail;
				return;
			}
			if (c->format) {
				disk_format (u);
/*memcpy (buf, &memory [c->memory], 512 * 8);*/
			} else if (c->op & DISK_PAGE_MODE)
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
#if 0
			if (disk_dev.dctrl)
				besm6_debug ("::: КМД %c: недокументированная команда 00",
					ctlr + '3');
#endif
			break;
		case 001: /* сброс на 0 цилиндр */
#if 0
			if (disk_dev.dctrl)
				besm6_debug ("::: КМД %c: сброс на 0 цилиндр",
					ctlr + '3');
#endif
			break;
		case 002: /* подвод */
			if (disk_dev.dctrl)
				besm6_debug ("::: КМД %c: подвод", ctlr + '3');
			break;
		case 003: /* чтение (НСМД-МОЗУ) */
		case 043: /* резервной дорожки */
#if 0
			if (disk_dev.dctrl)
				besm6_debug ("::: КМД %c: чтение", ctlr + '3');
#endif
			break;
		case 004: /* запись (МОЗУ-НСМД) */
		case 044: /* резервной дорожки */
#if 0
			if (disk_dev.dctrl)
				besm6_debug ("::: КМД %c: запись", ctlr + '3');
#endif
			break;
		case 005: /* разметка */
			c->format = 1;
			break;
		case 006: /* сравнение кодов (МОЗУ-НСМД) */
#if 0
			if (disk_dev.dctrl)
				besm6_debug ("::: КМД %c: сравнение кодов", ctlr + '3');
#endif
			break;
		case 007: /* чтение заголовка */
		case 047: /* резервной дорожки */
			if (disk_dev.dctrl)
				besm6_debug ("::: КМД %c: чтение %s заголовка", ctlr + '3',
					cmd & 040 ? "резервного" : "");
			disk_fail &= ~c->mask_fail;
			c->format = 1;
			if (c->op & DISK_PAGE_MODE)
				disk_read (u);
			else
				disk_read_track (u);
/*memcpy (&memory [c->memory], buf, 512 * 8);*/

			/* Ждём события от устройства. */
			sim_activate (u, 20*USEC);	/* Ускорим для отладки. */
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
			if (disk_unit[c->dev].fileref)
				c->status = STATUS_GOOD & BITS(12);
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
				c->status = (STATUS_GOOD >> 12) & BITS(12);
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
			GRP |= c->mask_grp;	/* чтобы не зависало */
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
#if 0
	if (disk_dev.dctrl)
		besm6_debug ("::: КМД: опрос шкалы ошибок = %04o", disk_fail);
#endif
	return disk_fail;
}
