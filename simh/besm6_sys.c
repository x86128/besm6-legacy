/*
 * besm6_sys.c: BESM-6 simulator interface
 *
 * Copyright (c) 2009, Serge Vakulenko
 *
 * This file implements three essential functions:
 *
 * sim_load()   - loading and dumping memory and CPU state
 *		  in a way, specific for BESM-6 architecture
 * fprint_sym() - print a machune instruction using
 *  		  opcode mnemonic or in a digital format
 * parse_sym()	- scan a string and build an instruction
 *		  word from it
 */
#include "besm6_defs.h"
#include <math.h>

/*
 * Преобразование вещественного числа в формат БЭСМ-6.
 *
 * Представление чисел в IEEE 754 (double):
 *	64   63———53 52————–1
 *	знак порядок мантисса
 * Старший (53-й) бит мантиссы не хранится и всегда равен 1.
 *
 * Представление чисел в БЭСМ-6:
 *      48——–42 41   40————————————————–1
 *      порядок знак мантисса в доп. коде
 */
t_value ieee_to_besm6 (double d)
{
	t_value word;
	int exponent;
	int sign;

	sign = d < 0;
	if (sign)
		d = -d;
	d = frexp (d, &exponent);
	/* 0.5 <= d < 1.0 */
	d = ldexp (d, 40);
	word = d;
	if (d - word >= 0.5)
		word += 1;		/* Округление. */
	if (exponent < -64)
		return 0LL;		/* Близкое к нулю число */
	if (exponent > 63) {
		return sign ?
		0xFEFFFFFFFFFFLL :	/* Максимальное число */
		0xFF0000000000LL;	/* Минимальное число */
	}
	if (sign) word = 0x20000000000LL-word;	/* Знак. */
	word |= ((t_value) (exponent + 64)) << 41;
	return word;
}

/*
 * Пропуск пробелов.
 */
char *skip_spaces (char *p)
{
	if (*p == (char) 0xEF && p[1] == (char) 0xBB && p[2] == (char) 0xBF) {
		/* Skip zero width no-break space. */
		p += 3;
	}
	while (*p == ' ' || *p == '\t')
		++p;
	return p;
}

/*
 * Чтение строки входного файла.
 */
t_stat besm6_read_line (FILE *input, int *type, t_value *val)
{
	char buf [512], *p;
	int i;
again:
	if (! fgets (buf, sizeof (buf), input)) {
		*type = 0;
		return SCPE_OK;
	}
	p = skip_spaces (buf);
	if (*p == '\n' || *p == ';')
		goto again;
	if (*p == ':') {
		/* Адрес размещения данных. */
		*type = ':';
		*val = strtol (p+1, 0, 8);
		return SCPE_OK;
	}
	if (*p == '@') {
		/* Стартовый адрес. */
		*type = '@';
		*val = strtol (p+1, 0, 8);
		return SCPE_OK;
	}
	if (*p == '=') {
		/* Вещественное число. */
		*type = '=';
		*val = ieee_to_besm6 (strtod (p+1, 0));
		return SCPE_OK;
	}
	if (*p < '0' || *p > '7') {
		/* неверная строка входного файла */
		return SCPE_FMT;
	}

	/* Слово. */
	*type = '=';
	*val = *p - '0';
	for (i=0; i<16; ++i) {
		p = skip_spaces (p + 1);
		if (*p < '0' || *p > '7') {
			/* слишком короткое слово */
			return SCPE_FMT;
		}
		*val = *val << 3 | (*p - '0');
	}
	return SCPE_OK;
}

/*
 * Load memory from file.
 */
t_stat besm6_load (FILE *input)
{
	int addr, type;
	t_value word;
	t_stat err;

	addr = 1;
	PC = 1;
	PPK = 0;
	for (;;) {
		err = besm6_read_line (input, &type, &word);
		if (err)
			return err;
		switch (type) {
		case 0:			/* EOF */
			return SCPE_OK;
		case ':':		/* address */
			addr = word;
			break;
		case '=':		/* word */
			if (addr < 010)
				pult[addr] = word;
			else
				memory [addr] = word;
			/* ram_dirty [addr] = 1; */
			++addr;
			break;
		case '@':		/* start address */
			PC = word;
			break;
		}
		if (addr > MEMSIZE)
			return SCPE_FMT;
	}
	return SCPE_OK;
}

/*
 * Dump memory to file.
 */
t_stat besm6_dump (FILE *of, char *fnam)
{
	int i, last_addr = -1;
	t_value cmd;

	fprintf (of, "; %s\n", fnam);
	for (i=1; i<MEMSIZE; ++i) {
		if (memory [i] == 0)
			continue;
		if (i != last_addr+1) {
			fprintf (of, "\n:%04o\n", i);
		}
		last_addr = i;
		cmd = memory [i];
		fprintf (of, "%o %02o %04o %04o %04o\n",
			(int) (cmd >> 42) & 7,
			(int) (cmd >> 36) & 077,
			(int) (cmd >> 24) & 07777,
			(int) (cmd >> 12) & 07777,
			(int) cmd & 07777);
	}
	return SCPE_OK;
}

/*
 * Loader/dumper
 */
t_stat sim_load (FILE *fi, char *cptr, char *fnam, int dump_flag)
{
	if (dump_flag)
		return besm6_dump (fi, fnam);

	return besm6_load (fi);
}

const char *besm6_opname [] = {
	"зп",	"зпм",	"рег",	"счм",	"сл",	"вч",	"вчоб",	"вчаб",
	"сч",	"и",	"нтж",	"слц",	"знак",	"или",	"дел",	"умн",
	"сбр",	"рзб",	"чед",	"нед",  "слп",  "вчп",  "сд",	"рж",
	"счрж",	"счмр",	"увв",	"УВВ",	"слпа",	"вчпа",	"сда",	"ржа",
	"уи",	"уим",	"счи",	"счим", "уии",	"сли",  "УИИ",	"СЛИ",
	"э50",	"э51",	"э52",	"э53",	"э54",	"э55",	"э56",	"э57",
	"э60",	"э61",  "э62",  "э63",  "э64",  "э65",  "э66",  "э67",
	"э70",	"э71",	"э72",	"э73",	"э74",	"э75",	"э76",	"э77",
	"э20",  "э21",  "мода", "мод",  "уиа",  "слиа", "по",   "пе",
	"пб",   "пв",   "выпр", "стоп", "пио",  "пино", "ПИО", "цикл"
};

int besm6_instr_to_opcode (char *instr)
{
	int i;

	for (i=0; i<64; ++i)
		if (strcmp (besm6_opname[i], instr) == 0)
			return i;
	return -1;
}

/*
 * Печать машинной инструкции.
 */
void besm6_fprint_cmd (FILE *of, uinstr_t ui)
{
	const char *m;

	m = besm6_opname [ui.i_opcode];
	fprintf (of, "%s ", m);
	if (ui.i_addr)
		fprintf (of, "%o", ui.i_addr);
	if (ui.i_reg)
		fprintf (of, "(%o)", ui.i_reg);
	fprintf (of, ", ");
}

void besm6_fprint_insn (FILE *of, uint insn)
{
	if (insn & 002000000)
		fprintf (of, "%02o %02o %05o ",
			insn >> 20, (insn >> 15) & 037, insn & BITS15);
	else
		fprintf (of, "%02o %03o %04o ",
			insn >> 20, (insn >> 12) & 0177, insn & 07777);
}

/*
 * Symbolic decode
 *
 * Inputs:
 *	*of	= output stream
 *	addr	= current PC
 *	*val	= pointer to data
 *	*uptr	= pointer to unit
 *	sw	= switches
 * Outputs:
 *	return	= status code
 */
t_stat fprint_sym (FILE *of, t_addr addr, t_value *val,
	UNIT *uptr, int32 sw)
{
	t_value cmd;

	if (uptr && (uptr != &cpu_unit))		/* must be CPU */
		return SCPE_ARG;

	cmd = val[0];

	if (sw & SWMASK ('M')) {			/* symbolic decode? */
		uinstr_t uil = unpack(cmd >> 24);
		uinstr_t uir = unpack(cmd & BITS24);

		besm6_fprint_cmd (of, uil);
		besm6_fprint_cmd (of, uir);
	} else if (sw & SWMASK ('I')) {
		besm6_fprint_insn (of, cmd >> 24);
		besm6_fprint_insn (of, cmd & BITS24);
	} else
	fprintf (of, "%04o %04o %04o %04o",
		(int) (cmd >> 36) & 07777,
		(int) (cmd >> 24) & 07777,
		(int) (cmd >> 12) & 07777,
		(int) cmd & 07777);
	return SCPE_OK;
}

char *besm6_parse_offset (char *cptr, int *offset)
{
	char *tptr, gbuf[CBUFSIZE];

	cptr = get_glyph (cptr, gbuf, 0);	/* get address */
	*offset = strtotv (gbuf, &tptr, 8);
	if ((tptr == gbuf) || (*tptr != 0) || (*offset > 07777))
		return 0;
	return cptr;
}

char *besm6_parse_address (char *cptr, int *address, int *relative)
{
	cptr = skip_spaces (cptr);			/* absorb spaces */
	if (*cptr >= '0' && *cptr <= '7')
		return besm6_parse_offset (cptr, address); /* get address */

	if (*cptr != '@')
		return 0;
	*relative |= 1;
	cptr = skip_spaces (cptr+1);			/* next char */
	if (*cptr == '+') {
		cptr = skip_spaces (cptr+1);		/* next char */
		cptr = besm6_parse_offset (cptr, address);
		if (! cptr)
			return 0;
	} else if (*cptr == '-') {
		cptr = skip_spaces (cptr+1);		/* next char */
		cptr = besm6_parse_offset (cptr, address);
		if (! cptr)
			return 0;
		*address = (- *address) & 07777;
	} else
		return 0;
	return cptr;
}

/*
 * Instruction parse
 */
t_stat parse_instruction (char *cptr, t_value *val, int32 sw)
{
	int opcode, ra, a1, a2, a3;
	char gbuf[CBUFSIZE];

	cptr = get_glyph (cptr, gbuf, 0);		/* get opcode */
	opcode = besm6_instr_to_opcode (gbuf);
	if (opcode < 0)
		return SCPE_ARG;
	ra = 0;
	cptr = besm6_parse_address (cptr, &a1, &ra);	/* get address 1 */
	if (! cptr)
		return SCPE_ARG;
	ra <<= 1;
	cptr = besm6_parse_address (cptr, &a2, &ra);	/* get address 1 */
	if (! cptr)
		return SCPE_ARG;
	ra <<= 1;
	cptr = besm6_parse_address (cptr, &a3, &ra);	/* get address 1 */
	if (! cptr)
		return SCPE_ARG;

	val[0] = (t_value) opcode << 36 | (t_value) ra << 42 |
		(t_value) a1 << 24 | a2 << 12 | a3;
	if (*cptr != 0)
		return SCPE_2MARG;
	return SCPE_OK;
}

/*
 * Symbolic input
 *
 * Inputs:
 *	*cptr   = pointer to input string
 *	addr    = current PC
 *	*uptr   = pointer to unit
 *	*val    = pointer to output values
 *	sw      = switches
 * Outputs:
 *	status  = error status
 */
t_stat parse_sym (char *cptr, t_addr addr, UNIT *uptr, t_value *val, int32 sw)
{
	int32 i;

	if (uptr && (uptr != &cpu_unit))		/* must be CPU */
		return SCPE_ARG;
	cptr = skip_spaces (cptr);			/* absorb spaces */
	if (! parse_instruction (cptr, val, sw))	/* symbolic parse? */
		return SCPE_OK;

	val[0] = 0;
	for (i=0; i<14; i++) {
		if (*cptr == 0)
			return SCPE_OK;
		if (*cptr < '0' || *cptr > '7')
			return SCPE_ARG;
		val[0] = (val[0] << 3) | (*cptr - '0');
		cptr = skip_spaces (cptr+1);		/* next char */
	}
	if (*cptr != 0)
		return SCPE_ARG;
	return SCPE_OK;
}
