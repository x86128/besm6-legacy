/*
 * besm6_printer.c: BESM-6 line printer device
 *
 * Copyright (c) 2009, Leo Broukhis
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

t_stat printer_event (UNIT *u);
void offset_gost_write (char *line, int n, FILE *fout);

/*
 * Printer data structures
 *
 * printer_dev	PRINTER device descriptor
 * printer_unit	PRINTER unit descriptor
 * printer_reg	PRINTER register list
 */
UNIT printer_unit [] = {
	{ UDATA (printer_event, UNIT_FIX+UNIT_ATTABLE, 0) },
	{ UDATA (printer_event, UNIT_FIX+UNIT_ATTABLE, 0) },
};

int curchar[2], feed[2], rampup[2];
char line[2][128];

#define PRN1_NOT_READY (1<<19)
#define PRN2_NOT_READY (1<<18)

#define PRN1_LINEFEED (1<<23)
#define PRN2_LINEFEED (1<<22)

#define SLOW_START	1000000
#define FAST_START	10000
#define LINEFEED_SYNC	17	/* 20-25 мс / 1.4 мс */

REG printer_reg[] = {
{ "Готов",	&READY, 2,  2, 18, 1 },
{ "Прогон",	&READY, 2,  2, 22, 1 },
{ 0 }
};

MTAB printer_mod[] = {
	{ 0 }
};

t_stat printer_reset (DEVICE *dptr);
t_stat printer_attach (UNIT *uptr, char *cptr);
t_stat printer_detach (UNIT *uptr);

DEVICE printer_dev = {
	"PRN", printer_unit, printer_reg, printer_mod,
	2, 8, 19, 1, 8, 50,
	NULL, NULL, &printer_reset, NULL, &printer_attach, &printer_detach,
	NULL, DEV_DISABLE | DEV_DEBUG
};

/*
 * Reset routine
 */
t_stat printer_reset (DEVICE *dptr)
{
	feed[0] = feed[1] = 0;
	rampup[0] = rampup[1] = SLOW_START;
	memset(line[0], 0, 128);
	memset(line[0], 0, 128);
	sim_cancel (&printer_unit[0]);
	sim_cancel (&printer_unit[1]);
	READY = PRN1_NOT_READY | PRN2_NOT_READY;
	if (printer_unit[0].flags & UNIT_ATT)
		READY &= ~PRN1_NOT_READY;
	if (printer_unit[1].flags & UNIT_ATT)
		READY &= ~PRN2_NOT_READY;
	return SCPE_OK;
}

t_stat printer_attach (UNIT *u, char *cptr)
{
	t_stat s;
	int num = u - printer_unit;
	s = attach_unit (u, cptr);
	if (s != SCPE_OK)
		return s;
	READY &= ~(PRN1_NOT_READY >> num);
	return SCPE_OK;
}

t_stat printer_detach (UNIT *u)
{
	int num = u - printer_unit;
	READY |= PRN1_NOT_READY >> num;
	return detach_unit (u);
}

/*
 * Управление двигателями, прогон
 */
void printer_control (int num, uint32 cmd)
{
	UNIT *u = &printer_unit[num];

	if (printer_dev.dctrl)
		besm6_debug(">>> АЦПУ%d команда %o", num, cmd);
	if (READY & (PRN1_NOT_READY >> num)) {
		if (printer_dev.dctrl)
			besm6_debug(">>> АЦПУ%d не готово", num, cmd);
		return;
	}
	switch (cmd) {
	case 1:		/* linefeed */
		READY |= PRN1_LINEFEED >> num;
		feed[num] = LINEFEED_SYNC;
		offset_gost_write (line[num], 128, stdout);
		puts("\r\n");
		memset(line[num], 0, 128);
		break;
	case 4: 	/* start */
		/* стартуем из состояния прогона для надежности */
		feed[num] = LINEFEED_SYNC;
		if (rampup[num])
			sim_activate (u, rampup[num]);
		rampup[num] = 0;
		break;
	case 10:	/* motor and ribbon off */
	case 8:		/* motor off? (undocumented) */
	case 2:		/* ribbon off */
		rampup[num] = cmd == 2 ? FAST_START : SLOW_START;
		sim_cancel (u);
		break;
	}
}

/*
 * Управление молоточками
 */
void printer_hammer (int num, int pos, uint32 mask)
{
	while (mask) {
		if (mask & 1)
			line[num][pos] = curchar[num];
		mask >>= 1;
		pos += 8;
	}
}

/*
 * Событие: вращение барабана АЦПУ.
 * Устанавливаем флаг прерывания.
 */
t_stat printer_event (UNIT *u)
{
	int num = u - printer_unit;
	switch (curchar[num]) {
	case 0 ... 0137:
		GRP |= GRP_PRN1_SYNC >> num;
		++curchar[num];
		/* For next char */
		sim_activate (u, 1400);
		if (feed[num] && --feed[num] == 0) {
			READY &= ~(040000000 >> num);
		}
		break;
	case 0140:
		/* For "zero" */
		curchar[num] = 0;
		GRP |= GRP_PRN1_ZERO >> num;
		if (printer_dev.dctrl)
			besm6_debug(">>> АЦПУ%d 'ноль'", num);
		/* For first sync after "zero" */
		sim_activate (u, 1000);
		break;
	}
	return SCPE_OK;
}

int gost_latin = 0; /* default cyrillics */

static int (*local_getc) (FILE *fin);
static void (*local_putc) (unsigned short ch, FILE *fout);

/*
 * GOST-10859 encoding.
 * Documentation: http://en.wikipedia.org/wiki/GOST_10859
 */
static const unsigned short gost_to_unicode_cyr [256] = {
/* 000-007 */   0x30,   0x31,   0x32,   0x33,   0x34,   0x35,   0x36,   0x37,
/* 010-017 */   0x38,   0x39,   0x2b,   0x2d,   0x2f,   0x2c,   0x2e,   0x2423,
/* 020-027 */   0x65,   0x2191, 0x28,   0x29,   0xd7,   0x3d,   0x3b,   0x5b,
/* 030-037 */   0x5d,   0x2a,   0x2018, 0x2019, 0x2260, 0x3c,   0x3e,   0x3a,
/* 040-047 */   0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417,
/* 050-057 */   0x0418, 0x0419, 0x041a, 0x041b, 0x041c, 0x041d, 0x041e, 0x041f,
/* 060-067 */   0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427,
/* 070-077 */   0x0428, 0x0429, 0x042b, 0x042c, 0x042d, 0x042e, 0x042f, 0x44,
/* 100-107 */   0x46,   0x47,   0x49,   0x4a,   0x4c,   0x4e,   0x51,   0x52,
/* 110-117 */   0x53,   0x55,   0x56,   0x57,   0x5a,   0x203e, 0x2264, 0x2265,
/* 120-127 */   0x2228, 0x2227, 0x2283, 0xac,   0xf7,   0x2261, 0x25,   0x25c7,
/* 130-137 */   0x7c,   0x2015, 0x5f,   0x21,   0x22,   0x042a, 0xb0,   0x2032,
};

static const unsigned short gost_to_unicode_lat [256] = {
/* 000-007 */   0x30,   0x31,   0x32,   0x33,   0x34,   0x35,   0x36,   0x37,
/* 010-017 */   0x38,   0x39,   0x2b,   0x2d,   0x2f,   0x2c,   0x2e,   0x2423,
/* 020-027 */   0x65,   0x2191, 0x28,   0x29,   0xd7,   0x3d,   0x3b,   0x5b,
/* 030-037 */   0x5d,   0x2a,   0x2018, 0x2019, 0x2260, 0x3c,   0x3e,   0x3a,
/* 040-047 */   0x41,   0x0411, 0x42,   0x0413, 0x0414, 0x45,   0x0416, 0x0417,
/* 050-057 */   0x0418, 0x0419, 0x4b,   0x041b, 0x4d,   0x48,   0x4f,   0x041f,
/* 060-067 */   0x50,   0x43,   0x54,   0x59,   0x0424, 0x58,   0x0426, 0x0427,
/* 070-077 */   0x0428, 0x0429, 0x042b, 0x042c, 0x042d, 0x042e, 0x042f, 0x44,
/* 100-107 */   0x46,   0x47,   0x49,   0x4a,   0x4c,   0x4e,   0x51,   0x52,
/* 110-117 */   0x53,   0x55,   0x56,   0x57,   0x5a,   0x203e, 0x2264, 0x2265,
/* 120-127 */   0x2228, 0x2227, 0x2283, 0xac,   0xf7,   0x2261, 0x25,   0x25c7,
/* 130-137 */   0x7c,   0x2015, 0x5f,   0x21,   0x22,   0x042a, 0xb0,   0x2032,
};
/*
 * Write Unicode symbol to file.
 * Convert to UTF-8 encoding:
 * 00000000.0xxxxxxx -> 0xxxxxxx
 * 00000xxx.xxyyyyyy -> 110xxxxx, 10yyyyyy
 * xxxxyyyy.yyzzzzzz -> 1110xxxx, 10yyyyyy, 10zzzzzz
 */
static void
utf8_putc (unsigned short ch, FILE *fout)
{
        static int initialized = 0;

        if (! initialized) {
                /* Write UTF-8 tag: zero width no-break space. */
                putc (0xEF, fout);
                putc (0xBB, fout);
                putc (0xBF, fout);
                initialized = 1;
        }
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

unsigned short
gost_to_unicode (unsigned char ch)
{
        return gost_latin ? gost_to_unicode_lat [ch] :
                gost_to_unicode_cyr [ch];
}

/*
 * Write GOST-10859 symbol to file.
 * Convert to local encoding (UTF-8, KOI8-R, CP-1251, CP-866).
 */
void
gost_putc (unsigned char ch, FILE *fout)
{
        unsigned short u;

        u = gost_to_unicode (ch);
        if (! u)
                u = ' ';
        utf8_putc (u, fout);
}

/*
 * Write GOST-10859 string to file in UTF-8.
 */
void
offset_gost_write (char *line, int n, FILE *fout)
{
        unsigned short u;

        while (n-- > 0) {
		unsigned char ch = *line++;
                u = ch ? gost_to_unicode (ch-1) : 0;
                if (! u)
                        u = ' ';
                utf8_putc (u, fout);
        }
}
