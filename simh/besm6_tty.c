/*
 * besm6_tty.c: BESM-6 teletype device
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

/*
 * Согласно таблице в http://ru.wikipedia.org/wiki/МТК-2
 */
char * rus[] = { 0, "Т", "\r", "О", " ", "Х", "Н", "М", "\n", "Л", "Р", "Г", "И", "П", "Ц", "Ж",
		 "Е", "З", "Д", "Б", "С", "Ы", "Ф", "Ь", "А", "В", "Й", 0, "У", "Я", "К", 0 };

char * lat[] = { 0, "T", "\r", "O", " ", "H", "N", "M", "\n", "L", "R", "G", "I", "P", "C", "V",
		 "E", "Z", "D", "B", "S", "Y", "F", "X", "A", "W", "J", 0, "U", "Q", "K", 0 };

/* $ = Кто там? */
char * dig[] = { 0, "5", "\r", "9", " ", "Щ", ",", ".", "\n", ")", "4", "Ш", "8", "0", ":", "=",
		 "3", "+", "$", "?", "'", "6", "Э", "/", "-", "2", "Ю", 0, "7", "1", "(", 0 };

char ** reg = 0;

void process (int sym)
{
	/* Требуется инверсия */
	sym ^= 31;
	switch (sym) {
	case 0:
		reg = rus;
		break;
	case 27:
		reg = dig;
		break;
	case 31:
		reg = lat;
		break;
	default:
		fputs (reg[sym], stdout);
	}
}

int tt_active = 0, vt_active = 0;
int tt_sym = 0, vt_sym = 0;
int vt_typed = 0, vt_instate = 0;
uint32 TTY_OUT = 0, TTY_IN = 0, vt_idle = 0;

void tty_send (uint32 mask)
{
	/* besm6_debug ("*** телетайпы: передача %08o", mask); */

	TTY_OUT = mask;
}

void tt_print()
{
	/* Пока работаем только с одним (любым) устройством */
	int c = TTY_OUT != 0;
	switch (tt_active*2+c) {
	case 0:	/* idle */
		break;
	case 1: /* start bit */
		tt_active = 1;
		break;
	case 12: /* stop bit */
		process (tt_sym);
		fflush (stdout);
		tt_active = 0;
		tt_sym = 0;
		break;
	case 13: /* framing error */
		putchar ('#');
		fflush (stdout);
		break;
	default:
		/* big endian ordering */
		if (c) {
			tt_sym |= 1 << (5-tt_active);
		}
		++tt_active;
		break;
	}
}

const char * koi7_rus_to_unicode [32] = {
	"Ю", "А", "Б", "Ц", "Д", "Е", "Ф", "Г",
	"Х", "И", "Й", "К", "Л", "М", "Н", "О",
	"П", "Я", "Р", "С", "Т", "У", "Ж", "В",
	"Ь", "Ы", "З", "Ш", "Э", "Щ", "Ч", "\0x7f",
};



void vt_print()
{
	/* Пока работаем только с одним (любым) устройством */
	int c = TTY_OUT != 0;
	switch (vt_active*2+c) {
	case 0: /* idle */
		++vt_idle;
		return;
	case 1: /* start bit */
		vt_active = 1;
		break;
	case 18: /* stop bit */
		vt_sym = ~vt_sym & 0x7f;
		if (vt_sym < 0x60) {
			if (vt_sym < ' ')
				switch (vt_sym) {
				case '\a':
				case '\b':
				case '\t':
				case '\n':
				case '\v':
				case '\f':
				case '\r':
				case '\033':
					break;
				default:
					/* Пропускаем нетекстовые символы */
					vt_sym = ' ';
				}
			fputc (vt_sym, stdout);
		} else
			fputs (koi7_rus_to_unicode[vt_sym - 0x60], stdout);
		fflush(stdout);
		vt_active = 0;
		vt_sym = 0;
		break;
	case 19: /* framing error */
		putchar ('#');
		fflush (stdout);
		break;
	default:
		/* little endian ordering */
		if (c) {
			vt_sym |= 1 << (vt_active-1);
		}
		++vt_active;
		break;
	}
	vt_idle = 0;
}

static int unicode_to_koi7 (unsigned val)
{
	switch (val >> 8) {
	case 0x00:
		if (val <= '_' || val == 0x7f)
			return val;
		if (val >= 'a' && val <= 'z')
			return val + 'Z' - 'z';
		break;
	case 0x04:
		switch ((unsigned char) val) {
		case 0x10: case 0x30: return 0x61;
		case 0x11: case 0x31: return 0x62;
		case 0x12: case 0x32: return 0x77;
		case 0x13: case 0x33: return 0x67;
		case 0x14: case 0x34: return 0x64;
		case 0x15: case 0x35: return 0x65;
		case 0x16: case 0x36: return 0x76;
		case 0x17: case 0x37: return 0x7a;
		case 0x18: case 0x38: return 0x69;
		case 0x19: case 0x39: return 0x6a;
		case 0x1a: case 0x3a: return 0x6b;
		case 0x1b: case 0x3b: return 0x6c;
		case 0x1c: case 0x3c: return 0x6d;
		case 0x1d: case 0x3d: return 0x6e;
		case 0x1e: case 0x3e: return 0x6f;
		case 0x1f: case 0x3f: return 0x70;
		case 0x20: case 0x40: return 0x72;
		case 0x21: case 0x41: return 0x73;
		case 0x22: case 0x42: return 0x74;
		case 0x23: case 0x43: return 0x75;
		case 0x24: case 0x44: return 0x66;
		case 0x25: case 0x45: return 0x68;
		case 0x26: case 0x46: return 0x63;
		case 0x27: case 0x47: return 0x7e;
		case 0x28: case 0x48: return 0x7b;
		case 0x29: case 0x49: return 0x7d;
		case 0x2b: case 0x4b: return 0x79;
		case 0x2c: case 0x4c: return 0x78;
		case 0x2d: case 0x4d: return 0x7c;
		case 0x2e: case 0x4e: return 0x60;
		case 0x2f: case 0x4f: return 0x71;
		}
		break;
	}
	return -1;
}

/*
 * Ввод символа с клавиатуры.
 * Перекодировка из UTF-8 в КОИ-7.
 * Полученный символ находится в диапазоне 0..0177.
 * Если нет ввода, возвращает -1.
 * В случае прерывания (^E) возвращает 0400.
 */
static int vt_kbd_input ()
{
	int c1, c2, c3, r;
again:
	r = sim_poll_kbd();
	if (r == SCPE_STOP)
		return 0400;
	if (! (r & SCPE_KFLAG))
		return -1;
	c1 = r & 0377;
	if (! (c1 & 0x80))
		return unicode_to_koi7 (c1);

	r = sim_poll_kbd();
	if (r == SCPE_STOP)
		return 0400;
	if (! (r & SCPE_KFLAG))
		return -1;
	c2 = r & 0377;
	if (! (c1 & 0x20))
		return unicode_to_koi7 ((c1 & 0x1f) << 6 | (c2 & 0x3f));

	r = sim_poll_kbd();
	if (r == SCPE_STOP)
		return 0400;
	if (! (r & SCPE_KFLAG))
		return -1;
	c3 = r & 0377;
	if (c1 == 0xEF && c2 == 0xBB && c3 == 0xBF) {
		/* Skip zero width no-break space. */
		goto again;
	}
	return unicode_to_koi7 ((c1 & 0x0f) << 12 | (c2 & 0x3f) << 6 |
		(c3 & 0x3f));
}

/* Терминал Ф4 - операторский */
#define OPER 004000000

void vt_receive()
{
	switch (vt_instate) {
	case 0:
		vt_typed = vt_kbd_input();
		if (vt_typed < 0) {
			sim_interval = 0;
			return;
		}
		if (vt_typed <= 0177) {
			if (vt_typed == '\r')
				vt_typed = 3;	/* ^C - конец строки */
			vt_instate = 1;
			TTY_IN = OPER;		/* start bit */
			GRP |= GRP_TTY_START;	/* не используется ? */
		}
		break;
	case 1 ... 7:
		TTY_IN = (vt_typed & (1 << (vt_instate-1))) ? 0 : OPER;
		vt_instate++;
		break;
	case 8:
		vt_typed = (vt_typed & 0x55) + ((vt_typed >> 1) & 0x55);
		vt_typed = (vt_typed & 0x33) + ((vt_typed >> 2) & 0x33);
		vt_typed = (vt_typed & 0x0F) + ((vt_typed >> 4) & 0x0F);
		TTY_IN = vt_typed & 1 ? 0 : OPER;	/* even parity */
		vt_instate++;
		break;
	case 9 ... 11:
		TTY_IN = 0;	/* stop bits */
		vt_instate++;
		break;
	case 12:
		vt_instate = 0;	/* ready for the next char */
		break;
	}
	vt_idle = 0;
}

/*
 * Выясняем, остановлены ли терминалы. Нужно для входа в "спящий" режим.
 */
int vt_is_idle ()
{
	return (vt_idle > 10);
}

int tty_query ()
{
/*	besm6_debug ("*** телетайпы: приём");*/
	return TTY_IN;
}

t_stat console_event (UNIT *u)
{
	return 0;
}

UNIT console_unit [] = {
	{ UDATA (console_event, UNIT_FIX, 0) },
	{ UDATA (console_event, UNIT_FIX, 0) },
};

REG console_reg[] = {
{ 0 }
};

MTAB console_mod[] = {
	{ 0 }
};

t_stat console_reset (DEVICE *dptr);

DEVICE console_dev = {
	"CONS", console_unit, console_reg, console_mod,
	2, 8, 19, 1, 8, 50,
	NULL, NULL, &console_reset, NULL, NULL, NULL,
	NULL, DEV_DEBUG
};

#define CONS_READY 0200
#define CONS_ERROR 0100

#define CONS_CAN_PRINT 01000

/*
 * Reset routine
 */
t_stat console_reset (DEVICE *dptr)
{
	sim_cancel (&console_unit[0]);
	sim_cancel (&console_unit[1]);
	READY2 = CONS_READY;
	PRP |= CONS_CAN_PRINT;
	return SCPE_OK;
}

void console_print (uint32 cmd)
{
	besm6_debug(">>> CONSUL: %03o", cmd & 0377);
	PRP |= CONS_CAN_PRINT;
}
