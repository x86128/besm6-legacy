/*
 * besm6_defs.h: BESM-6 simulator definitions
 *
 * Copyright (c) 2009, Serge Vakulenko
 */
#ifndef _BESM6_DEFS_H_
#define _BESM6_DEFS_H_    0

#include "sim_defs.h"				/* simulator defns */
#include <setjmp.h>

/*
 * Memory
 */
#define NREGS		30			/* number of registers-modifiers */
#define MEMSIZE		(128*1024)		/* memory size, words */
#define DRUM_SIZE	(256*(1024+8))		/* drum size, words */
#define DISK_SIZE	(1024*(1024+8))		/* disk size, words */

/*
 * Simulator stop codes
 */
enum {
	STOP_STOP = 1,				/* STOP */
	STOP_IBKPT,				/* breakpoint */
	STOP_RUNOUT,				/* run out end of memory limits */
	STOP_BADCMD,				/* invalid instruction */
	STOP_INSN_CHECK,			/* not an instruction */
	STOP_INSN_PROT,				/* fetch from blocked page */
	STOP_OPERAND_PROT,			/* load from blocked page */
	STOP_RAM_CHECK,				/* RAM parity error */
	STOP_CACHE_CHECK,			/* data cache parity error */
	STOP_OVFL,				/* arith. overflow */
	STOP_DIVZERO,				/* division by 0 or denorm */
};

/*
 * Разряды машинного слова.
 */
#define BITS12		07777			/* биты 12..1 */
#define BITS15		077777			/* биты 15..1 */
#define BITS24		077777777		/* биты 24..1 */
#define BIT19		001000000		/* бит 19 - расширение адреса короткой команды */
#define BIT20		002000000		/* бит 20 - признак длинной команды */
#define WORD		07777777777777777LL	/* биты 48..1 */
#define MANTISSA	00037777777777777LL	/* биты 41..1 */
#define SIGN		00020000000000000LL	/* 41-й бит-знак */

/*
 * Работа со сверткой. Значение разрядов свертки слова равно значению
 * регистров ПКЛ и ПКП при записи слова.
 * 00 - командная свертка
 * 01 или 10 - контроль числа
 * 11 - числовая свертка
 */
#define CONVOL_INSN(x, ruu)	(((x) & WORD) | (((ruu ^ 1) & 3LL) << 48))
#define CONVOL_NUMBER(x, ruu)	(((x) & WORD) | (((ruu ^ 2) & 3LL) << 48))
#define IS_INSN(x)		(((x) >> 48) == 1)
#define IS_NUMBER(x)		(((x) >> 48) == 1 || ((x) >> 48) == 2)

/*
 * Вычисление правдоподобного времени выполнения команды,
 * зная количество тактов в УУ и среднее в АУ.
 * Предполагаем, что в 50% случаев происходит совмещение
 * выполнения, поэтому суммируем большее и половину
 * от меньшего значения.
 */
#define MEAN_TIME(x,y)	(x>y ? x+y/2 : x/2+y)

extern uint32 sim_brk_types, sim_brk_dflt, sim_brk_summ; /* breakpoint info */
extern int32 sim_interval, sim_step;
extern FILE *sim_deb, *sim_log;

extern UNIT cpu_unit;
extern t_value memory [MEMSIZE];
extern t_value pult [8];
extern uint32 PC, RAU, RUU;
extern uint32 M[NREGS];
extern t_value BRZ[8], RP[8];
extern uint32 BAZ[8], TABST, RZ;
extern DEVICE cpu_dev, drum_dev, mmu_dev;
extern DEVICE clock_dev;
extern jmp_buf cpu_halt;

/*
 * Разряды режима АУ.
 */
#define RAU_NORM_DISABLE        001     /* блокировка нормализации */
#define RAU_ROUND_DISABLE       002     /* блокировка округления */
#define RAU_LOG                 004     /* признак логической группы */
#define RAU_MULT                010     /* признак группы умножения */
#define RAU_ADD                 020     /* признак группы слодения */
#define RAU_OVF_DISABLE         040     /* блокировка переполнения */

#define RAU_MODE                (RAU_LOG | RAU_MULT | RAU_ADD)
#define SET_MODE(x,m)           (((x) & ~RAU_MODE) | (m))
#define SET_LOGICAL(x)          (((x) & ~RAU_MODE) | RAU_LOG)
#define SET_MULTIPLICATIVE(x)   (((x) & ~RAU_MODE) | RAU_MULT)
#define SET_ADDITIVE(x)         (((x) & ~RAU_MODE) | RAU_ADD)
#define IS_LOGICAL(x)           (((x) & RAU_MODE) == RAU_LOG)
#define IS_MULTIPLICATIVE(x)    (((x) & (RAU_ADD | RAU_MULT)) == RAU_MULT)
#define IS_ADDITIVE(x)          ((x) & RAU_ADD)

/*
 * Искусственный регистр режимов УУ, в реальной машине отсутствует.
 */
#define RUU_CONVOL_RIGHT	000001	/* ПКП - признак контроля правой половины */
#define RUU_CONVOL_LEFT		000002	/* ПКЛ - признак контроля левой половины */
#define RUU_EXTRACODE		000004	/* РежЭ - режим экстракода */
#define RUU_INTERRUPT		000010	/* РежПр - режим прерывания */
#define RUU_MOD_RK		000020	/* ПрИК - модификация регистром М[16] */
#define RUU_AVOST_DISABLE	000040	/* БРО - блокировка режима останова */
#define RUU_RIGHT_INSTR		000400	/* ПрК - признак правой команды */

#define IS_SUPERVISOR(x)	((x) & (RUU_EXTRACODE | RUU_INTERRUPT))
#define SET_SUPERVISOR(x,m)	(((x) & ~(RUU_EXTRACODE | RUU_INTERRUPT)) | (m))

/*
 * Специальные регистры.
 */
#define MOD	020	/* модификатор адреса */
#define PSW	021     /* режимы УУ */
#define SPSW	027     /* упрятывание режимов УУ */
#define ERET	032     /* адрес возврата из экстракода */
#define IRET	033     /* адрес возврата из прерывания */
#define IBP	034     /* адрес останова по выполнению */
#define DWP	035     /* адрес останова по чтению/записи */

/*
 * Регистр 021: режимы УУ.
 * PSW: program status word.
 */
#define PSW_MMAP_DISABLE	000001	/* БлП - блокировка приписки */
#define PSW_PROT_DISABLE	000002	/* БлЗ - блокировка защиты */
#define PSW_INTR_HALT		000004	/* ПоП - признак останова при
					   любом внутреннем прерывании */
#define PSW_CHECK_HALT		000010	/* ПоК - признак останова при
					   прерывании по контролю */
#define PSW_WRITE_WATCH		000020	/* Зп(М29) - признак совпадения адреса
					   операнда прии записи в память
					   с содержанием регистра М29 */
#define PSW_INTR_DISABLE	002000	/* БлПр - блокировка внешнего прерывания */
#define PSW_AUT_B		004000	/* АвтБ - признак режима Автомат Б */

/*
 * Регистр 027: сохранённые режимы УУ.
 * SPSW: saved program status word.
 */
#define SPSW_MMAP_DISABLE	000001	/* БлП - блокировка приписки */
#define SPSW_PROT_DISABLE	000002	/* БлЗ - блокировка защиты */
#define SPSW_EXTRACODE		000004	/* РежЭ - режим экстракода */
#define SPSW_INTERRUPT		000010	/* РежПр - режим прерывания */
#define SPSW_MOD_RK		000020	/* ПрИК(РК) - на регистр РК принята
					   команда, которая должна быть
					   модифицирована регистром М[16] */
#define SPSW_MOD_RR		000040	/* ПрИК(РР) - на регистре РР находится
					   команда, выполненная с модификацией */
#define SPSW_UNKNOWN		000100	/* НОК? вписано карандашом в 9 томе */
#define SPSW_RIGHT_INSTR	000400	/* ПрК - признак правой команды */
#define SPSW_NEXT_RK		001000	/* ГД./ДК2 - на регистр РК принята
					   команда, следующая после вызвавшей
					   прерывание */
#define SPSW_INTR_DISABLE	002000	/* БлПр - блокировка внешнего прерывания */

/*
 * Кириллица Unicode.
 */
#define CYRILLIC_CAPITAL_LETTER_A		0x0410
#define CYRILLIC_CAPITAL_LETTER_BE		0x0411
#define CYRILLIC_CAPITAL_LETTER_VE		0x0412
#define CYRILLIC_CAPITAL_LETTER_GHE		0x0413
#define CYRILLIC_CAPITAL_LETTER_DE		0x0414
#define CYRILLIC_CAPITAL_LETTER_IE		0x0415
#define CYRILLIC_CAPITAL_LETTER_ZHE		0x0416
#define CYRILLIC_CAPITAL_LETTER_ZE		0x0417
#define CYRILLIC_CAPITAL_LETTER_I		0x0418
#define CYRILLIC_CAPITAL_LETTER_SHORT_I		0x0419
#define CYRILLIC_CAPITAL_LETTER_KA		0x041a
#define CYRILLIC_CAPITAL_LETTER_EL		0x041b
#define CYRILLIC_CAPITAL_LETTER_EM		0x041c
#define CYRILLIC_CAPITAL_LETTER_EN		0x041d
#define CYRILLIC_CAPITAL_LETTER_O		0x041e
#define CYRILLIC_CAPITAL_LETTER_PE		0x041f
#define CYRILLIC_CAPITAL_LETTER_ER		0x0420
#define CYRILLIC_CAPITAL_LETTER_ES		0x0421
#define CYRILLIC_CAPITAL_LETTER_TE		0x0422
#define CYRILLIC_CAPITAL_LETTER_U		0x0423
#define CYRILLIC_CAPITAL_LETTER_EF		0x0424
#define CYRILLIC_CAPITAL_LETTER_HA		0x0425
#define CYRILLIC_CAPITAL_LETTER_TSE		0x0426
#define CYRILLIC_CAPITAL_LETTER_CHE		0x0427
#define CYRILLIC_CAPITAL_LETTER_SHA		0x0428
#define CYRILLIC_CAPITAL_LETTER_SHCHA		0x0429
#define CYRILLIC_CAPITAL_LETTER_HARD_SIGN	0x042a
#define CYRILLIC_CAPITAL_LETTER_YERU		0x042b
#define CYRILLIC_CAPITAL_LETTER_SOFT_SIGN	0x042c
#define CYRILLIC_CAPITAL_LETTER_E		0x042d
#define CYRILLIC_CAPITAL_LETTER_YU		0x042e
#define CYRILLIC_CAPITAL_LETTER_YA		0x042f
#define CYRILLIC_SMALL_LETTER_A			0x0430
#define CYRILLIC_SMALL_LETTER_BE		0x0431
#define CYRILLIC_SMALL_LETTER_VE		0x0432
#define CYRILLIC_SMALL_LETTER_GHE		0x0433
#define CYRILLIC_SMALL_LETTER_DE		0x0434
#define CYRILLIC_SMALL_LETTER_IE		0x0435
#define CYRILLIC_SMALL_LETTER_ZHE		0x0436
#define CYRILLIC_SMALL_LETTER_ZE		0x0437
#define CYRILLIC_SMALL_LETTER_I			0x0438
#define CYRILLIC_SMALL_LETTER_SHORT_I		0x0439
#define CYRILLIC_SMALL_LETTER_KA		0x043a
#define CYRILLIC_SMALL_LETTER_EL		0x043b
#define CYRILLIC_SMALL_LETTER_EM		0x043c
#define CYRILLIC_SMALL_LETTER_EN		0x043d
#define CYRILLIC_SMALL_LETTER_O			0x043e
#define CYRILLIC_SMALL_LETTER_PE		0x043f
#define CYRILLIC_SMALL_LETTER_ER		0x0440
#define CYRILLIC_SMALL_LETTER_ES		0x0441
#define CYRILLIC_SMALL_LETTER_TE		0x0442
#define CYRILLIC_SMALL_LETTER_U			0x0443
#define CYRILLIC_SMALL_LETTER_EF		0x0444
#define CYRILLIC_SMALL_LETTER_HA		0x0445
#define CYRILLIC_SMALL_LETTER_TSE		0x0446
#define CYRILLIC_SMALL_LETTER_CHE		0x0447
#define CYRILLIC_SMALL_LETTER_SHA		0x0448
#define CYRILLIC_SMALL_LETTER_SHCHA		0x0449
#define CYRILLIC_SMALL_LETTER_HARD_SIGN		0x044a
#define CYRILLIC_SMALL_LETTER_YERU		0x044b
#define CYRILLIC_SMALL_LETTER_SOFT_SIGN		0x044c
#define CYRILLIC_SMALL_LETTER_E			0x044d
#define CYRILLIC_SMALL_LETTER_YU		0x044e
#define CYRILLIC_SMALL_LETTER_YA		0x044f

/*
 * Процедуры работы с памятью
 */
extern void mmu_store (int addr, t_value word);
extern t_value mmu_load (int addr);
extern t_value mmu_fetch (int addr);
extern void mmu_setcache (int idx, t_value word);
extern t_value mmu_getcache (int idx);
extern void mmu_setrp (int idx, t_value word);
extern void mmu_setup (void);
extern void mmu_setprotection (int idx, t_value word);
extern void mmu_print_brz (void);

/*
 * Выполнение обращения к барабану.
 * Все параметры находятся в регистрах.
 */
void drum (t_value *sum);

void besm6_fprint_cmd (FILE *of, uint32 cmd);
double besm6_to_ieee (t_value word);

// Unpacked instruction
typedef struct  {
        uint8   i_reg;                  /* register #                   */
        uint8   i_opcode;               /* opcode                       */
        uint16  i_addr;                 /* address field                */
}       uinstr_t;

uinstr_t unpack(t_value rk);

/*
 * Разряды главного регистра прерываний (ГРП)
 * Внешние:
 */
#define GRP_PRN1_SYNC	04000000000000000LL	/* 48 */
#define GRP_PRN2_SYNC	02000000000000000LL	/* 47 */
#define GRP_DRUM1_FREE	01000000000000000LL	/* 46 */
#define GRP_DRUM2_FREE	00400000000000000LL	/* 45 */
#define GRP_VNIIEM	00360000000000000LL	/* 44-41, placeholder */
#define	GRP_TIMER	00010000000000000LL	/* 40 */
#define GRP_PRN1_ZERO	00004000000000000LL	/* 39 */
#define GRP_PRN2_ZERO	00002000000000000LL	/* 38 */
#define GRP_SLAVE	00001000000000000LL	/* 37 */
#define GRP_CHAN3_DONE	00000400000000000LL	/* 36 */
#define GRP_CHAN4_DONE	00000200000000000LL	/* 35 */
#define GRP_CHAN5_DONE	00000100000000000LL	/* 34 */
#define GRP_CHAN6_DONE	00000040000000000LL	/* 33 */
#define GRP_PANEL_REQ	00000020000000000LL	/* 32 */
#define GRP_TTY_START	00000010000000000LL	/* 31 */
#define GRP_IMITATION	00000004000000000LL	/* 30 */
#define GRP_CHAN3_FREE	00000002000000000LL	/* 29 */
#define GRP_CHAN4_FREE	00000001000000000LL	/* 28 */
#define GRP_CHAN5_FREE	00000000400000000LL	/* 27 */
#define GRP_CHAN6_FREE	00000000200000000LL	/* 26 */
#define GRP_CHAN7_FREE	00000000100000000LL	/* 25 */
#define GRP_WATCHDOG	00000000000002000LL	/* 11 */
/* Внутренние: */
#define GRP_DIVZERO	00000000034000000LL	/* 23-21 */
#define GRP_OVERFLOW	00000000014000000LL	/* 22-21 */
#define GRP_CHECK 	00000000004000000LL	/* 21 */
#define GRP_OPRND_PROT	00000000002000000LL	/* 20 */
#define GRP_WATCHPT_W	00000000000200000LL	/* 17 */
#define GRP_WATCHPT_R	00000000000100000LL	/* 16 */
#define GRP_INSN_CHECK	00000000000040000LL	/* 15 */
#define GRP_INSN_PROT	00000000000020000LL	/* 14 */
#define GRP_ILL_INSN	00000000000010000LL	/* 13 */
#define GRP_BREAKPOINT	00000000000004000LL	/* 12 */
#define GRP_RAM_CHECK	00000000000000010LL	/* 4 */
#endif
