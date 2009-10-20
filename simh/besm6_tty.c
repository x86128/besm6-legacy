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

uint32 TTY_OUT = 0, TTY_IN = 0;

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
                break;
        case 1: /* start bit */
                vt_active = 1;
                break;
        case 18: /* stop bit */
		vt_sym = ~vt_sym & 0x7f;
		if (vt_sym < 0x60)
	                fputc(vt_sym, stdout);
		else
			fputs(koi7_rus_to_unicode[vt_sym - 0x60], stdout);
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
}

/* Терминал Ф4 - операторский */
#define OPER 004000000

void vt_receive()
{
	t_stat r;
	switch (vt_instate) {
	case 0:
		r = sim_poll_kbd();
		if (r == SCPE_STOP) {
			sim_interval = 0;
		} else if (r != SCPE_OK) {
			vt_typed = r - SCPE_KFLAG;
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
