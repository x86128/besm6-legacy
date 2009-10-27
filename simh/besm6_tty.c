/*
 * besm6_tty.c: BESM-6 teletype device
 *
 * Copyright (c) 2009, Leo Broukhis
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
#include "sim_sock.h"
#include "sim_tmxr.h"

#define TTY_MAX		24		/* Количество терминалов */

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

int tt_active [TTY_MAX], vt_active [TTY_MAX];
int tt_sym [TTY_MAX], vt_sym [TTY_MAX];
int vt_typed [TTY_MAX], vt_instate [TTY_MAX];

uint32 vt_sending, vt_receiving;

// Attachments survive the reset
uint32 tt_mask = 0, vt_mask = 0;

uint32 TTY_OUT = 0, TTY_IN = 0, vt_idle = 0;

UNIT tty_unit [] = {
	{ UDATA (NULL, UNIT_DIS, 0) },		/* fake unit */
	{ UDATA (NULL, UNIT_SEQ, 0) },
	{ UDATA (NULL, UNIT_SEQ, 0) },
	{ UDATA (NULL, UNIT_SEQ, 0) },
	{ UDATA (NULL, UNIT_SEQ, 0) },
	{ UDATA (NULL, UNIT_SEQ, 0) },
	{ UDATA (NULL, UNIT_SEQ, 0) },
	{ UDATA (NULL, UNIT_SEQ, 0) },
	{ UDATA (NULL, UNIT_SEQ, 0) },
	{ UDATA (NULL, UNIT_SEQ, 0) },
	{ UDATA (NULL, UNIT_SEQ, 0) },
	{ UDATA (NULL, UNIT_SEQ, 0) },
	{ UDATA (NULL, UNIT_SEQ, 0) },
	{ UDATA (NULL, UNIT_SEQ, 0) },
	{ UDATA (NULL, UNIT_SEQ, 0) },
	{ UDATA (NULL, UNIT_SEQ, 0) },
	{ UDATA (NULL, UNIT_SEQ, 0) },
	{ UDATA (NULL, UNIT_SEQ, 0) },
	{ UDATA (NULL, UNIT_SEQ, 0) },
	{ UDATA (NULL, UNIT_SEQ, 0) },
	{ UDATA (NULL, UNIT_SEQ, 0) },
	{ UDATA (NULL, UNIT_SEQ, 0) },
	{ UDATA (NULL, UNIT_SEQ, 0) },
	{ UDATA (NULL, UNIT_SEQ, 0) },
	{ UDATA (NULL, UNIT_SEQ, 0) },
	{ 0 }
};

REG tty_reg[] = {
	{ 0 }
};

/*
 * Дескрипторы линий для мультиплексора TMXR.
 * Поле .conn содержит номер сокета и означает занятую линию.
 * Для локальных терминалов делаем .conn = 1.
 * Чтобы нумерация линий совпадала с нумерацией терминалов
 * (с единицы), нулевую линию держим занятой (.conn = 1).
 * Поле .rcve устанавливается в 1 для сетевых соединений.
 * Для локальных терминалов оно равно 0.
 */
TMLN tty_line [TTY_MAX+1];
TMXR tty_desc = { TTY_MAX+1, 0, 0, tty_line };	/* mux descriptor */

t_stat tty_reset (DEVICE *dptr)
{
	memset(tt_active, 0, sizeof(tt_active));
	memset(tt_sym, 0, sizeof(tt_sym));
	memset(vt_active, 0, sizeof(vt_active));
	memset(vt_sym, 0, sizeof(vt_sym));
	memset(vt_typed, 0, sizeof(vt_typed));
	memset(vt_instate, 0, sizeof(vt_instate));
	vt_sending = vt_receiving = 0;
	TTY_IN = TTY_OUT = 0;
	vt_idle = 1;
	tty_line[0].conn = 1;			/* faked, always busy */
	return SCPE_OK;
}

#define TTY_UNICODE_CHARSET	0
#define TTY_KOI7_JCUKEN_CHARSET	(1<<UNIT_V_UF)
#define TTY_KOI7_QWERTY_CHARSET	(2<<UNIT_V_UF)
#define TTY_CHARSET_MASK	(3<<UNIT_V_UF)
#define TTY_OFFLINE_STATE	0
#define TTY_TELETYPE_STATE	(1<<(UNIT_V_UF+2))
#define TTY_VT340_STATE		(2<<(UNIT_V_UF+2))
#define TTY_STATE_MASK		(3<<(UNIT_V_UF+2))
#define TTY_DESTRUCTIVE_BSPACE	0
#define TTY_AUTHENTIC_BSPACE	(1<<(UNIT_V_UF+4))
#define TTY_BSPACE_MASK		(1<<(UNIT_V_UF+4))

t_stat tty_setmode (UNIT *u, int32 val, char *cptr, void *desc)
{
	int num = u - tty_unit;
	TMLN *t = &tty_line [num];
	uint32 mask = 1 << (TTY_MAX - num);

	switch (val & TTY_STATE_MASK) {
	case TTY_OFFLINE_STATE:
		if (t->conn) {
			if (t->rcve) {
				tmxr_reset_ln (t);
				t->rcve = 0;
			} else
				t->conn = 0;
			if (vt_mask & mask) {
				vt_sym[num] =
				vt_active[num] =
				vt_typed[num] =
				vt_instate[num] = 0;
				vt_mask &= ~mask;
			} else {
				tt_sym[num] =
				tt_active[num] = 0;
				tt_mask &= ~mask;
			}
		}
		break;
	case TTY_TELETYPE_STATE:
		if (t->conn && ! (tt_mask & mask))
			return SCPE_ALATT;
		t->conn = 1;
		t->rcve = 0;
		tt_mask |= mask;
		break;
	case TTY_VT340_STATE:
		if (t->conn && ! (vt_mask & mask))
			return SCPE_ALATT;
		t->conn = 1;
		t->rcve = 0;
		vt_mask |= mask;
		break;
	}
	return SCPE_OK;
}

/*
 * Разрешение подключения к терминалам через telnet.
 * Делается командой:
 *	attach tty <порт>
 * Здесь <порт> - номер порта telnet, например 4199.
 */
t_stat tty_attach (UNIT *u, char *cptr)
{
	int num = u - tty_unit;
	int r, m, n;

	if (*cptr >= '0' && *cptr <= '9') {
		/* Сохраняем и восстанавливаем все .conn,
		 * так как tmxr_attach() их обнуляет. */
		for (m=0, n=1; n<=TTY_MAX; ++n)
			if (tty_line[n].conn)
				m |= 1 << (TTY_MAX-n);
		/* Неважно, какой номер порта указывать в команде задания
		 * порта telnet. Можно tty, можно tty1 - без разницы. */
		r = tmxr_attach (&tty_desc, &tty_unit[0], cptr);
		for (n=1; n<=TTY_MAX; ++n)
			if (m >> (TTY_MAX-n) & 1)
				tty_line[n].conn = 1;
		return r;
	}
	if (strcmp (cptr, "/dev/tty") == 0) {
		/* Консоль. */
		u->flags &= ~TTY_STATE_MASK;
		u->flags |= TTY_VT340_STATE;
		tty_line[num].conn = 1;
		tty_line[num].rcve = 0;
		vt_mask |= 1 << (TTY_MAX - num);
		besm6_debug ("*** консоль на T%03o", num);
		return 0;
	}
	if (strcmp (cptr, "/dev/null") == 0) {
		/* Запрещаем терминал. */
		tty_line[num].conn = 1;
		tty_line[num].rcve = 0;
		besm6_debug ("*** отключение терминала T%03o", num);
		return 0;
	}
	return SCPE_ALATT;
}

t_stat tty_detach (UNIT *u)
{
	return tmxr_detach (&tty_desc, &tty_unit[0]);
}

MTAB tty_mod[] = {
        { TTY_CHARSET_MASK, TTY_UNICODE_CHARSET, "UNICODE input",
		"UNICODE" },
        { TTY_CHARSET_MASK, TTY_KOI7_JCUKEN_CHARSET, "KOI7 (jcuken) input",
		"JCUKEN" },
        { TTY_CHARSET_MASK, TTY_KOI7_QWERTY_CHARSET, "KOI7 (qwerty) input",
		"QWERTY" },
	{ TTY_STATE_MASK, TTY_OFFLINE_STATE, "offline",
		"OFF", &tty_setmode },
	{ TTY_STATE_MASK, TTY_TELETYPE_STATE, "Teletype",
		"TT", &tty_setmode },
	{ TTY_STATE_MASK, TTY_VT340_STATE, "Videoton-340",
		"VT", &tty_setmode },
	{ TTY_BSPACE_MASK, TTY_DESTRUCTIVE_BSPACE, "destructive backspace",
		"DESTRBS" },
	{ TTY_BSPACE_MASK, TTY_AUTHENTIC_BSPACE, NULL,
		"AUTHBS" },
        { MTAB_XTD | MTAB_VDV, 1, NULL,
		"DISCONNECT", &tmxr_dscln, NULL, (void*) &tty_desc },
        { UNIT_ATT, UNIT_ATT, "connections",
		NULL, NULL, &tmxr_show_summ, (void*) &tty_desc },
        { MTAB_XTD | MTAB_VDV | MTAB_NMO, 1, "CONNECTIONS",
		NULL, NULL, &tmxr_show_cstat, (void*) &tty_desc },
	{ 0 }
};

DEVICE tty_dev = {
	"TTY", tty_unit, tty_reg, tty_mod,
	25, 2, 1, 1, 2, 1,
	NULL, NULL, &tty_reset, NULL, &tty_attach, &tty_detach,
	NULL, DEV_NET|DEV_DEBUG
};

void tty_send (uint32 mask)
{
	/* besm6_debug ("*** телетайпы: передача %08o", mask); */

	TTY_OUT = mask;
}

/* Ввод с телетайпа не реализован,
 * вывод на телетайп должен вызываться по 50 Гц таймеру
 * или после каждого tty_send.
 */
void tt_print()
{
	/* Пока работаем только с одним (любым) устройством */
	int c = TTY_OUT != 0;
	switch (tt_active[0]*2+c) {
	case 0:	/* idle */
		break;
	case 1: /* start bit */
		tt_active[0] = 1;
		break;
	case 12: /* stop bit */
		process (tt_sym[0]);
		fflush (stdout);
		tt_active[0] = 0;
		tt_sym[0] = 0;
		break;
	case 13: /* framing error */
		fputc ('#', stdout);
		fflush (stdout);
		break;
	default:
		/* big endian ordering */
		if (c) {
			tt_sym[0] |= 1 << (5-tt_active[0]);
		}
		++tt_active[0];
		break;
	}
}

/*
 * Выдача символа на терминал с указанным номером.
 */
void vt_putc (int num, int c)
{
	TMLN *t = &tty_line [num];

	if (! t->conn)
		return;
	if (t->rcve) {
		/* Передача через telnet. */
		tmxr_putc_ln (t, c);
	} else {
		/* Вывод на консоль. */
		fputc (c, stdout);
		fflush (stdout);
	}
}

/*
 * Выдача строки на терминал с указанным номером.
 */
void vt_puts (int num, const char *s)
{
	while (*s)
		vt_putc (num, *s++);
}

const char * koi7_rus_to_unicode [32] = {
	"Ю", "А", "Б", "Ц", "Д", "Е", "Ф", "Г",
	"Х", "И", "Й", "К", "Л", "М", "Н", "О",
	"П", "Я", "Р", "С", "Т", "У", "Ж", "В",
	"Ь", "Ы", "З", "Ш", "Э", "Щ", "Ч", "\0x7f",
};

/*
 * Обработка выдачи на все подключенные терминалы.
 */
void vt_print()
{
	uint32 workset = (TTY_OUT & vt_mask) | vt_sending;
	int num;
	TMLN *t;

	/* Есть новые сетевые подключения? */
	num = tmxr_poll_conn (&tty_desc);
	if (num > 0 && num <= TTY_MAX) {
		t = &tty_line [num];
		besm6_debug ("*** tty%d: новое подключение от %d.%d.%d.%d",
			num, (unsigned char) (t->ipad >> 24),
			(unsigned char) (t->ipad >> 16),
			(unsigned char) (t->ipad >> 8),
			(unsigned char) t->ipad);
		t->rcve = 1;
		tty_unit[num].flags &= ~TTY_STATE_MASK;
		tty_unit[num].flags |= TTY_VT340_STATE;
		vt_mask |= 1 << (TTY_MAX - num);
	}

	if (workset == 0) {
		++vt_idle;
		return;
 	}
	for (num = besm6_highest_bit (workset) - TTY_MAX;
		workset; num = besm6_highest_bit (workset) - TTY_MAX) {
	    int mask = 1 << (TTY_MAX - num);
	    int c = (TTY_OUT & mask) != 0;
	    switch (vt_active[num]*2+c) {
	    case 0: /* idle */
		besm6_debug ("Warning: inactive ttys should have been screened");
		continue;
	    case 1: /* start bit */
		vt_sending |= mask;
		vt_active[num] = 1;
		break;
	    case 18: /* stop bit */
		vt_sym[num] = ~vt_sym[num] & 0x7f;
		if (vt_sym[num] < 0x60) {
			switch (vt_sym[num]) {
			case '\a':
			case '\b':
			case '\t':
			case '\n':
			case '\v':
			case '\r':
			case '\033':
			case '\0':
				/* Выдаём управляющий символ. */
				break;
			case '\f':
				/* На VDT-340 это был переход в начало экрана
				 * (home). Затем обычно выдавались три возврата
				 * каретки (backspace) и ERR, которые
				 * оказывались в правом нижнем углу экрана.
				 * На современных терминалах такой трюк не
				 * проходит, поэтому просто переходим в начало
				 * следующей строки. */
				vt_putc (num, '\n');
				vt_sym[num] = '\r';
				break;
			case '\032':
				/* На Видеотоне ^Z = забой.
				 * Стираем предыдущий символ. */
				if ((tty_unit[num].flags & TTY_BSPACE_MASK) ==
					TTY_DESTRUCTIVE_BSPACE) {
					vt_puts (num, "\b ");
				}
				vt_sym[num] = '\b';
				break;
				vt_sym[num] = 'm';
				break;
			case '\003':
				/* Неотображаемые символы */
				vt_sym[num] = 0;
				break;
			default:
				if (vt_sym[num] < ' ') {
					/* Нефункциональные ctrl-символы были видны в половинной яркости */
					vt_puts (num, "\033[2m");
					vt_putc (num, vt_sym[num] | 0x40);
					vt_puts (num, "\033[");
					/* Завершаем ESC-последовательность */
					vt_sym[num] = 'm';
				}
			}
			if (vt_sym[num])
				vt_putc (num, vt_sym[num]);
		} else
			vt_puts (num, koi7_rus_to_unicode[vt_sym[num] - 0x60]);
		vt_active[num] = 0;
		vt_sym[num] = 0;
		vt_sending &= ~mask;
		break;
	    case 19: /* framing error */
		vt_putc (num, '#');
		break;
	    default:
		/* little endian ordering */
		if (c) {
			vt_sym[num] |= 1 << (vt_active[num]-1);
		}
		++vt_active[num];
		break;
	    }
	    workset &= ~mask;
	}
	vt_idle = 0;

	/* Опрашиваем сокеты на передачу. */
	tmxr_poll_tx (&tty_desc);
}

/*
 * Перекодировка из Unicode в КОИ-7.
 * Если нет соответствия, возвращает -1.
 */
static int unicode_to_koi7 (unsigned val)
{
	switch (val) {
	case '\0'... '_':	  return val;
	case 'a' ... 'z':	  return val + 'Z' - 'z';
	case 0x007f:		  return 0x7f;
        case 0x0410: case 0x0430: return 0x61;
        case 0x0411: case 0x0431: return 0x62;
        case 0x0412: case 0x0432: return 0x77;
        case 0x0413: case 0x0433: return 0x67;
        case 0x0414: case 0x0434: return 0x64;
        case 0x0415: case 0x0435: return 0x65;
        case 0x0416: case 0x0436: return 0x76;
        case 0x0417: case 0x0437: return 0x7a;
        case 0x0418: case 0x0438: return 0x69;
        case 0x0419: case 0x0439: return 0x6a;
        case 0x041a: case 0x043a: return 0x6b;
        case 0x041b: case 0x043b: return 0x6c;
        case 0x041c: case 0x043c: return 0x6d;
        case 0x041d: case 0x043d: return 0x6e;
        case 0x041e: case 0x043e: return 0x6f;
        case 0x041f: case 0x043f: return 0x70;
        case 0x0420: case 0x0440: return 0x72;
        case 0x0421: case 0x0441: return 0x73;
        case 0x0422: case 0x0442: return 0x74;
        case 0x0423: case 0x0443: return 0x75;
        case 0x0424: case 0x0444: return 0x66;
        case 0x0425: case 0x0445: return 0x68;
        case 0x0426: case 0x0446: return 0x63;
        case 0x0427: case 0x0447: return 0x7e;
        case 0x0428: case 0x0448: return 0x7b;
        case 0x0429: case 0x0449: return 0x7d;
        case 0x042b: case 0x044b: return 0x79;
        case 0x042c: case 0x044c: return 0x78;
        case 0x042d: case 0x044d: return 0x7c;
        case 0x042e: case 0x044e: return 0x60;
        case 0x042f: case 0x044f: return 0x71;
	}
	return -1;
}

/*
 * Ввод символа с терминала с указанным номером.
 * Если нет приёма, возвращает -1.
 * В случае прерывания возвращает 0400 (только для консоли).
 */
int vt_getc (num)
{
	TMLN *t = &tty_line [num];
	int c;

	if (! t->conn) {
		/* Пользователь отключился. */
		if (t->ipad) {
			besm6_debug ("*** tty%d: отключение %d.%d.%d.%d",
				num, (unsigned char) (t->ipad >> 24),
				(unsigned char) (t->ipad >> 16),
				(unsigned char) (t->ipad >> 8),
				(unsigned char) t->ipad);
			t->ipad = 0;
		}
		tty_setmode (tty_unit+num, TTY_OFFLINE_STATE, 0, 0);
		tty_unit[num].flags &= ~TTY_STATE_MASK;
		return -1;
	}
	if (t->rcve) {
		/* Приём через telnet. */
		c = tmxr_getc_ln (t);
		if (! (c & TMXR_VALID))
			return -1;
	} else {
		/* Ввод с клавиатуры. */
		c = sim_poll_kbd();
		if (c == SCPE_STOP)
			return 0400;		/* прерывание */
		if (! (c & SCPE_KFLAG))
			return -1;
	}
	return c & 0377;
}

/*
 * Ввод символа с клавиатуры.
 * Перекодировка из UTF-8 в КОИ-7.
 * Полученный символ находится в диапазоне 0..0177.
 * Если нет ввода, возвращает -1.
 * В случае прерывания (^E) возвращает 0400.
 */
static int vt_kbd_input_unicode (int num)
{
	int c1, c2, c3, r;
again:
	r = vt_getc (num);
	if (r < 0 || r > 0377)
		return r;
	c1 = r & 0377;
	if (! (c1 & 0x80))
		return unicode_to_koi7 (c1);

	r = vt_getc (num);
	if (r < 0 || r > 0377)
		return r;
	c2 = r & 0377;
	if (! (c1 & 0x20))
		return unicode_to_koi7 ((c1 & 0x1f) << 6 | (c2 & 0x3f));

	r = vt_getc (num);
	if (r < 0 || r > 0377)
		return r;
	c3 = r & 0377;
	if (c1 == 0xEF && c2 == 0xBB && c3 == 0xBF) {
		/* Skip zero width no-break space. */
		goto again;
	}
	return unicode_to_koi7 ((c1 & 0x0f) << 12 | (c2 & 0x3f) << 6 |
		(c3 & 0x3f));
}

/*
 * Альтернативный вариант ввода, не требующий переключения на русскую клавиатуру.
 * Символы "точка" и "запятая" вводятся через shift, "больше-меньше" - "тильда-гравис".
 * "точка с запятой" - "закр. фиг. скобка", "апостроф" - "верт. черта".
 */
static int vt_kbd_input_koi7 (int num)
{
	int r;

	r = vt_getc (num);
	if (r < 0 || r > 0377)
		return r;
	r &= 0377;
	switch (r) {
	case '\r': return '\003';
	case 'q': return 'j';
	case 'w': return 'c';
	case 'e': return 'u';
	case 'r': return 'k';
	case 't': return 'e';
	case 'y': return 'n';
	case 'u': return 'g';
	case 'i': return '{';
	case 'o': return '}';
	case 'p': return 'z';
	case '[': return 'h';
	case '{': return '[';
	case 'a': return 'f';
	case 's': return 'y';
	case 'd': return 'w';
	case 'f': return 'a';
	case 'g': return 'p';
	case 'h': return 'r';
	case 'j': return 'o';
	case 'k': return 'l';
	case 'l': return 'd';
	case ';': return 'v';
	case '}': return ';';
	case '\'': return '|';
	case '|': return '\'';
	case 'z': return 'q';
	case 'x': return '~';
	case 'c': return 's';
	case 'v': return 'm';
	case 'b': return 'i';
	case 'n': return 't';
	case 'm': return 'x';
	case ',': return 'b';
	case '<': return ',';
	case '.': return '`';
	case '>': return '.';
	case '~': return '>';
	case '`': return '<';
	default: return r;
	}
}

int odd_parity(unsigned char c)
{
	c = (c & 0x55) + ((c >> 1) & 0x55);
	c = (c & 0x33) + ((c >> 2) & 0x33);
	c = (c & 0x0F) + ((c >> 4) & 0x0F);
	return c & 1;
}

/*
 * Обработка ввода со всех подключенных терминалов.
 */
void vt_receive()
{
    uint32 workset = vt_mask;
    int num;

    /* Опрашиваем сокеты на приём. */
    tmxr_poll_rx (&tty_desc);

    TTY_IN = 0;
    for (num = besm6_highest_bit (workset) - TTY_MAX;
		workset; num = besm6_highest_bit (workset) - TTY_MAX) {
	uint32 mask = 1 << (TTY_MAX - num);
	switch (vt_instate[num]) {
	case 0:
		switch (tty_unit[num].flags & TTY_CHARSET_MASK) {
		case TTY_KOI7_JCUKEN_CHARSET:
			vt_typed[num] = vt_kbd_input_koi7 (num);
			break;
		case TTY_KOI7_QWERTY_CHARSET:
			vt_typed[num] = vt_getc (num);
			break;
		case TTY_UNICODE_CHARSET:
			vt_typed[num] = vt_kbd_input_unicode (num);
			break;
		default:
			vt_typed[num] = '?';
			break;
		}
		if (vt_typed[num] < 0) {
			/* TODO: обработать исключение от "неоператорского" терминала */
			sim_interval = 0;
			break;
		}
		if (vt_typed[num] <= 0177) {
			if (vt_typed[num] == '\r' || vt_typed[num] == '\n')
				vt_typed[num] = 3;	/* ^C - конец строки */
			if (vt_typed[num] == '\b' || vt_typed[num] == '\177')
				vt_typed[num] = 26;	/* ^Z - забой */
			vt_instate[num] = 1;
			TTY_IN |= mask;		/* start bit */
			GRP |= GRP_TTY_START;	/* не используется ? */
			vt_receiving |= mask;
		}
		break;
	case 1 ... 7:
		/* need inverted byte */
		TTY_IN |= (vt_typed[num] & (1 << (vt_instate[num]-1))) ? 0 : mask;
		vt_instate[num]++;
		break;
	case 8:
		TTY_IN |= odd_parity(vt_typed[num]) ? 0 : mask;	/* even parity of inverted */
		vt_instate[num]++;
		break;
	case 9 ... 11:
		/* stop bits are 0 */
		vt_instate[num]++;
		break;
	case 12:
		vt_instate[num] = 0;	/* ready for the next char */
		vt_receiving &= ~mask;
		break;
	}
	workset &= ~mask;
    }
    if (vt_receiving)
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
	{ UDATA (console_event, UNIT_SEQ, 0) },
	{ UDATA (console_event, UNIT_SEQ, 0) },
};

REG console_reg[] = {
{ 0 }
};

MTAB console_mod[] = {
	{ 0 }
};

t_stat console_reset (DEVICE *dptr);

DEVICE console_dev = {
	"CONSUL", console_unit, console_reg, console_mod,
	2, 8, 19, 1, 8, 50,
	NULL, NULL, &console_reset, NULL, NULL, NULL,
	NULL, DEV_DEBUG
};

#define CONS_READY 0100
#define CONS_ERROR 0100

#define CONS_CAN_PRINT 01000

/*
 * Reset routine
 */
t_stat console_reset (DEVICE *dptr)
{
	sim_cancel (&console_unit[0]);
	sim_cancel (&console_unit[1]);
	READY2 |= CONS_READY;
	PRP |= CONS_CAN_PRINT;
	return SCPE_OK;
}

void console_print (uint32 cmd)
{
	besm6_debug(">>> CONSUL: %03o", cmd & 0377);
	PRP |= CONS_CAN_PRINT;
}
