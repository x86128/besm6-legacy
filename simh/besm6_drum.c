/*
 * besm6_drum.c: BESM-6 magnetic drum device
 *
 * Copyright (c) 2009, Serge Vakulenko
 */
#include "besm6_defs.h"

/*
 * Параметры обмена с внешним устройством.
 */
int ext_op;			/* УЧ - условное число */
int ext_disk_addr;		/* А_МЗУ - начальный адрес на барабане/ленте */
int ext_ram_start;		/* α_МОЗУ - начальный адрес памяти */
int ext_ram_finish;		/* ω_МОЗУ - конечный адрес памяти */

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
{ "УЧ",     &ext_op,         8, 12, 0, 1 },
{ "А_МЗУ",  &ext_disk_addr,  8, 12, 0, 1 },
{ "α_МОЗУ", &ext_ram_start,  8, 12, 0, 1 },
{ "ω_МОЗУ", &ext_ram_finish, 8, 12, 0, 1 },
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
	ext_op = 07777;
	ext_disk_addr = 0;
	ext_ram_start = 0;
	ext_ram_finish = 0;
	sim_cancel (&drum_unit);
	return SCPE_OK;
}

/*
 * Подсчет контрольной суммы, как в команде СЛЦ.
 */
t_value compute_checksum (t_value x, t_value y)
{
	/*TODO*/
	return 0;
#if 0
	t_value sum;

	sum = (x & ~MANTISSA) + (y & ~MANTISSA);
	if (sum & BIT46)
		sum += BIT37;
	y = (x & MANTISSA) + (y & MANTISSA);
	if (y & BIT37)
		y += 1;
	return (sum & ~MANTISSA) | (y & MANTISSA);
#endif
}

/*
 * Запись на барабан.
 * Если параметр sum ненулевой, посчитываем и кладём туда контрольную
 * сумму массива. Также запмсываем сумму в слово last+1 на барабане.
 */
void drum_write (int addr, int first, int last, t_value *sum)
{
	int nwords, i;

#if 0
	nwords = last - first + 1;
	if (nwords <= 0 || nwords+addr > DRUM_SIZE) {
		/* Неверная длина записи на МБ */
		longjmp (cpu_halt, STOP_BADWLEN);
	}
	if (sim_deb && drum_dev.dctrl)
		fprintf (sim_deb, "*** запись МБ %05o память %04o-%04o\n",
			addr, first, last);
	fseek (drum_unit.fileref, addr*8, SEEK_SET);
	fxwrite (&memory[first], 8, nwords, drum_unit.fileref);
	if (ferror (drum_unit.fileref))
		longjmp (cpu_halt, SCPE_IOERR);
	if (sum) {
		/* Подсчитываем и записываем контрольную сумму. */
		*sum = 0;
		for (i=first; i<=last; ++i)
			*sum = compute_checksum (*sum, memory[i]);
		fxwrite (sum, 8, 1, drum_unit.fileref);
	}
#endif
}

/*
 * Чтение с барабана.
 */
void drum_read (int addr, int first, int last, t_value *sum)
{
	int nwords, i;
	t_value old_sum;

#if 0
	nwords = last - first + 1;
	if (nwords <= 0 || nwords+addr > DRUM_SIZE) {
		/* Неверная длина чтения МБ */
		longjmp (cpu_halt, STOP_BADRLEN);
	}
	if (sim_deb && drum_dev.dctrl)
		fprintf (sim_deb, "*** чтение МБ %05o память %04o-%04o\n",
			addr, first, last);
	fseek (drum_unit.fileref, addr*8, SEEK_SET);
	i = fxread (&memory[first], 8, nwords, drum_unit.fileref);
	if (ferror (drum_unit.fileref))
		longjmp (cpu_halt, SCPE_IOERR);
	if (i != nwords) {
		/* Чтение неинициализированного барабана */
		longjmp (cpu_halt, STOP_DRUMINVDATA);
	}
	if (sum) {
		/* Считываем и проверяем контрольную сумму. */
		fxread (&old_sum, 8, 1, drum_unit.fileref);
		*sum = 0;
		for (i=first; i<=last; ++i)
			*sum = compute_checksum (*sum, memory[i]);
		if (old_sum != *sum)
			longjmp (cpu_halt, STOP_READERR);
	}
#endif
}

/*
 * Выполнение обращения к барабану.
 * Все параметры находятся в регистрах УЧ, А_МЗУ, α_МОЗУ, ω_МОЗУ.
 */
void drum (t_value *sum)
{
	if (drum_dev.flags & DEV_DIS) {
		/* Device not attached. */
		longjmp (cpu_halt, SCPE_UNATT);
	}
	/*TODO*/
}
