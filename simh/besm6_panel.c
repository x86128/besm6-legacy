/*
 * Panel of BESM-6, displayed as a graphics window.
 * Using libSDL for graphics and libSDL_ttf for fonts.
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
#ifdef HAVE_LIBSDL
#include <stdlib.h>
#include <ftw.h>
#include <SDL/SDL.h>
#include <SDL/SDL_ttf.h>
#include "besm6_defs.h"

/*
 * Use a 640x480 window with 32 bit pixels.
 */
#define WIDTH		800
#define HEIGHT		400
#define DEPTH		32

#define STEPX		14
#define STEPY		16

#define FONTNAME	"LucidaSansRegular.ttf"
#define FONTPATH1	"/usr/share/fonts"
#define FONTPATH2	"/usr/lib/jvm"
#define FONTPATH3	"/System/Library/Frameworks/JavaVM.framework/Versions"

static SDL_Surface *screen;
static char *font_path;
static TTF_Font *font_big;
static TTF_Font *font_small;
static SDL_Color foreground;
static SDL_Color background;
static const SDL_Color white = { 255, 255, 255, 0 };
static const SDL_Color black = { 0,   0,   0,   0 };
static const SDL_Color cyan  = { 0,   128, 128, 0 };
static const SDL_Color grey  = { 64,  64,  64,  0 };
static t_value old_BRZ [8], old_GRP [2];
static t_value old_M [NREGS];

static const int regnum[] = {
	013, 012, 011, 010, 7, 6, 5, 4,
	027, 016, 015, 014, 3, 2, 1, 020,
};

/*
 * Рисование текста в кодировке UTF-8, с антиалиасингом.
 * Параметр halign задаёт выравнивание по горизонтали.
 * Цвета заданы глобальными переменными foreground и background.
 */
static void render_utf8 (TTF_Font *font, int x, int y, int halign, char *message)
{
	SDL_Surface *text;
	SDL_Rect area;

	/* Build image from text */
	text = TTF_RenderUTF8_Shaded (font, message, foreground, background);

	area.x = x;
	if (halign < 0)
		area.x -= text->w;		/* align left */
	else if (halign == 0)
		area.x -= text->w / 2;		/* center */
	area.y = y;
	area.w = text->w;
	area.h = text->h;

	/* Put text image to screen */
	SDL_BlitSurface (text, 0, screen, &area);
	SDL_FreeSurface (text);
}

/*
 * Рисуем неонку.
 */
static void draw_lamp (int left, int top, int on)
{
	/* Images created by GIMP: save as C file without alpha channel. */
	static const int lamp_width = 12;
	static const int lamp_height = 12;
	static const unsigned char lamp_on [12 * 12 * 3 + 1] =
	  "\0\0\0\0\0\0\0\0\0\13\2\2-\14\14e\31\31e\31\31-\14\14\13\2\2\0\0\0\0\0\0"
	  "\0\0\0\0\0\0\0\0\0D\20\20\313,,\377??\377CC\377CC\377DD\31333D\21\21\0\0"
	  "\0\0\0\0\0\0\0D\20\20\357LL\377\243\243\376~~\37699\376@@\376@@\377AA\357"
	  "<<D\21\21\0\0\0\13\2\2\313,,\377\243\243\377\373\373\377\356\356\377NN\377"
	  ">>\377@@\377@@\377AA\31333\13\2\2-\14\14\377??\376~~\377\356\356\377\321"
	  "\321\377<<\377??\377@@\377@@\376@@\377DD-\14\14e\31\31\377CC\37699\377NN"
	  "\377<<\377??\377@@\377@@\377@@\376??\377CCe\31\31e\31\31\377CC\376@@\377"
	  ">>\377??\377@@\377@@\377@@\377@@\376??\377CCe\31\31-\14\14\377DD\376@@\377"
	  "@@\377@@\377@@\377@@\377@@\377@@\376@@\377DD-\14\14\13\2\2\31333\377AA\377"
	  "@@\377@@\377@@\377@@\377@@\377@@\377AA\31333\13\2\2\0\0\0D\21\21\357<<\377"
	  "AA\376@@\376??\376??\376@@\377AA\357<<D\21\21\0\0\0\0\0\0\0\0\0D\21\21\313"
	  "33\377DD\377CC\377CC\377DD\31333D\21\21\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\13"
	  "\2\2-\14\14e\31\31e\31\31-\14\14\13\2\2\0\0\0\0\0\0\0\0\0";
	static const unsigned char lamp_off [12 * 12 * 3 + 1] =
	  "\0\0\0\0\0\0\0\0\0\0\0\0\14\2\2\14\2\2\14\2\2\14\2\2\0\0\0\0\0\0\0\0\0\0"
	  "\0\0\0\0\0\0\0\0\25\5\5A\21\21h\32\32c\30\30c\30\30h\32\32A\21\21\25\5\5"
	  "\0\0\0\0\0\0\0\0\0\25\5\5\\\30\30""8\16\16\0\0\0\0\0\0\0\0\0\0\0\0""8\16"
	  "\16\\\30\30\25\5\5\0\0\0\0\0\0A\21\21""8\16\16\0\0\0\0\0\0\0\0\0\0\0\0\0"
	  "\0\0\0\0\0""8\16\16A\21\21\0\0\0\14\2\2h\32\32\0\0\0\0\0\0\0\0\0\0\0\0\0"
	  "\0\0\0\0\0\0\0\0\0\0\0h\32\32\14\2\2\14\2\2c\30\30\0\0\0\0\0\0\0\0\0\0\0"
	  "\0\0\0\0\0\0\0\0\0\0\0\0\0c\30\30\14\2\2\14\2\2c\30\30\0\0\0\0\0\0\0\0\0"
	  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0c\30\30\14\2\2\14\2\2h\32\32\0\0\0\0\0\0\0"
	  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0h\32\32\14\2\2\0\0\0A\21\21""8\16\16\0"
	  "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0""8\16\16A\21\21\0\0\0\0\0\0\25\5\5\\\30"
	  "\30""8\16\16\0\0\0\0\0\0\0\0\0\0\0\0""8\16\16\\\30\30\25\5\5\0\0\0\0\0\0"
	  "\0\0\0\25\5\5A\21\21h\32\32c\30\30c\30\30h\32\32A\21\21\25\5\5\0\0\0\0\0"
	  "\0\0\0\0\0\0\0\0\0\0\0\0\0\14\2\2\14\2\2\14\2\2\14\2\2\0\0\0\0\0\0\0\0\0"
	  "\0\0\0";
	const unsigned char *p;
	unsigned int *screenp, r, g, b, y, x;

	p = on ? lamp_on : lamp_off;
	for (y=0; y<lamp_height; ++y) {
		screenp = ((unsigned*) screen->pixels) +
			(top + y) * WIDTH + left;
		for (x=0; x<lamp_width; ++x) {
			r = *p++;
			g = *p++;
			b = *p++;
			*screenp++ = r<<16 | g<<8 | b;
		}
	}
}

/*
 * Отрисовка лампочек БРЗ.
 */
static void draw_modifiers_periodic (int group, int left, int top)
{
	int x, y, reg, val;

	for (y=0; y<8; ++y) {
		reg = regnum [y + group*8];
		val = M [reg];
		if (val == old_M [reg])
			continue;
		old_M [reg] = val;
		for (x=0; x<15; ++x) {
			draw_lamp (left+76 + x*STEPX, top+28 + y*STEPY, val >> (14-x) & 1);
		}
		SDL_UpdateRect (screen, left+76, top+28 + y*STEPY, 15*STEPX, 12);
	}
}

/*
 * Отрисовка лампочек ГРП и МГРП.
 */
static void draw_grp_periodic (int top)
{
	int x, y;
	t_value val;

	for (y=0; y<2; ++y) {
		val = y ? MGRP : GRP;
		if (val == old_GRP [y])
			continue;
		old_GRP [y] = val;
		for (x=0; x<48; ++x) {
			draw_lamp (100 + x*STEPX, top+28 + y*STEPY, val >> (47-x) & 1);
		}
		SDL_UpdateRect (screen, 100, top+28 + y*STEPY, 48*STEPX, 12);
	}
}

/*
 * Отрисовка лампочек БРЗ.
 */
static void draw_brz_periodic (int top)
{
	int x, y;
	t_value val;

	for (y=0; y<8; ++y) {
		val = BRZ [7-y];
		if (val == old_BRZ [7-y])
			continue;
		old_BRZ [7-y] = val;
		for (x=0; x<48; ++x) {
			draw_lamp (100 + x*STEPX, top+28 + y*STEPY, val >> (47-x) & 1);
		}
		SDL_UpdateRect (screen, 100, top+28 + y*STEPY, 48*STEPX, 12);
	}
}

/*
 * Отрисовка статичной части регистров-модификаторов.
 */
static void draw_modifiers_static (int group, int left, int top)
{
	int x, y, color, reg;
	char message [40];
	SDL_Rect area;

	background = black;
	foreground = cyan;

	/* Оттеняем группы разрядов. */
	color = grey.r << 16 | grey.g << 8 | grey.b;
	for (x=3; x<15; x+=3) {
		area.x = left + 74 + x*STEPX;
		area.y = top + 26;
		area.w = 2;
		area.h = 8*STEPY + 2;
		SDL_FillRect (screen, &area, color);
	}
	/* Названия регистров. */
	for (y=0; y<8; ++y) {
		reg = regnum [y + group*8];
		sprintf (message, "М%2o", reg);
		render_utf8 (font_big, left, top + 24 + y*STEPY, 1, message);
		old_M [reg] = ~0;
	}
	/* Номера битов. */
	for (x=0; x<15; ++x) {
		sprintf (message, "%d", 15-x);
		render_utf8 (font_small, left+82 + x*STEPX,
			(x & 1) ? top+4 : top+10, 0, message);
	}
}

/*
 * Отрисовка статичной части регистров ГРП и МГРП.
 */
static void draw_grp_static (int top)
{
	int x, y, color;
	char message [40];
	SDL_Rect area;

	background = black;
	foreground = cyan;

	/* Оттеняем группы разрядов. */
	color = grey.r << 16 | grey.g << 8 | grey.b;
	for (x=3; x<48; x+=3) {
		area.x = 98 + x*STEPX;
		area.y = top + 26;
		area.w = 2;
		area.h = 2*STEPY + 2;
		SDL_FillRect (screen, &area, color);
	}
	/* Названия регистров. */
	for (y=0; y<2; ++y) {
		render_utf8 (font_big, 24, top + 24 + y*STEPY, 1, y ? "МГРП" : "ГРП");
		old_GRP[y] = ~0;
	}
	/* Номера битов. */
	for (x=0; x<48; ++x) {
		sprintf (message, "%d", 48-x);
		render_utf8 (font_small, 106 + x*STEPX,
			(x & 1) ? top+10 : top+4, 0, message);
	}
}

/*
 * Отрисовка статичной части регистров БРЗ.
 */
static void draw_brz_static (int top)
{
	int x, y, color;
	char message [40];
	SDL_Rect area;

	background = black;
	foreground = cyan;

	/* Оттеняем группы разрядов. */
	color = grey.r << 16 | grey.g << 8 | grey.b;
	for (x=3; x<48; x+=3) {
		area.x = 98 + x*STEPX;
		area.y = top + 26;
		area.w = 2;
		area.h = 8*STEPY + 2;
		SDL_FillRect (screen, &area, color);
	}
	/* Названия регистров. */
	for (y=7; y>=0; --y) {
		sprintf (message, "БРЗ %d", 7-y);
		render_utf8 (font_big, 24, top + 24 + y*STEPY, 1, message);
		old_BRZ[y] = ~0;
	}
}

/*
 * Поиск файла шрифта по имени.
 */
static int probe_font (const char *path, const struct stat *st, int flag)
{
	const char *p;

	if (flag != FTW_F)
		return 0;
	p = path + strlen (path) - strlen (FONTNAME);
	if (p < path || strcmp (p, FONTNAME) != 0)
		return 0;
	font_path = strdup (path);
	return 1;
}

/*
 * Закрываем графическое окно.
 */
void besm6_close_panel ()
{
	if (! screen)
		return;
	TTF_Quit();
	SDL_Quit();
	screen = 0;
}

/*
 * Начальная инициализация графического окна и шрифтов.
 */
static void init_panel ()
{
	/* Initialize SDL subsystems - in this case, only video. */
	if (SDL_Init (SDL_INIT_VIDEO) < 0) {
		fprintf (stderr, "SDL: unable to init: %s\n",
			SDL_GetError ());
		exit (1);
	}
	screen = SDL_SetVideoMode (WIDTH, HEIGHT, DEPTH, SDL_SWSURFACE);
	if (! screen) {
		fprintf (stderr, "SDL: unable to set %dx%dx%d mode: %s\n",
			WIDTH, HEIGHT, DEPTH, SDL_GetError ());
		exit (1);
	}

	/* Initialize the TTF library */
	if (TTF_Init() < 0) {
		fprintf (stderr, "SDL: couldn't initialize TTF: %s\n",
			SDL_GetError());
		SDL_Quit();
		exit (1);
	}

	/* Find font file */
	if (ftw (FONTPATH1, probe_font, 255) <= 0 &&
	    ftw (FONTPATH2, probe_font, 255) <= 0 &&
	    ftw (FONTPATH3, probe_font, 255) <= 0) {
		fprintf(stderr, "SDL: couldn't find font %s in directory %s\n",
			FONTNAME, FONTPATH1);
		besm6_close_panel();
		exit (1);
	}

	/* Open the font file with the requested point size */
	font_big = TTF_OpenFont (font_path, 16);
	font_small = TTF_OpenFont (font_path, 9);
	if (! font_big || ! font_small) {
		fprintf(stderr, "SDL: couldn't load font %s: %s\n",
			FONTNAME, SDL_GetError());
		besm6_close_panel();
		exit (1);
	}
	atexit (besm6_close_panel);

	/* Отрисовка статичной части панели БЭСМ-6. */
	draw_modifiers_static (0, 24, 10);
	draw_modifiers_static (1, 400, 10);
	draw_grp_static (180);
	draw_brz_static (230);

	/* Tell SDL to update the whole screen */
	SDL_UpdateRect (screen, 0, 0, WIDTH, HEIGHT);
}

/*
 * Обновляем графическое окно.
 */
void besm6_draw_panel ()
{
	if (! screen)
		init_panel ();

	/* Lock surface if needed */
	if (SDL_MUSTLOCK (screen) && SDL_LockSurface (screen) < 0)
		return;

	/* Периодическая отрисовка: мигание лампочек. */
	draw_modifiers_periodic (0, 24, 10);
	draw_modifiers_periodic (1, 400, 10);
	draw_grp_periodic (180);
	draw_brz_periodic (230);

	/* Unlock if needed */
	if (SDL_MUSTLOCK (screen))
		SDL_UnlockSurface (screen);

	/* Exit SIMH when window closed.*/
	SDL_Event event;
	if (SDL_PollEvent (&event) && event.type == SDL_QUIT)
		longjmp (cpu_halt, SCPE_EXIT);
}

#if !defined(__WIN32__) && \
    !(defined(__MWERKS__) && !defined(__BEOS__)) && \
    !defined(__MACOS__) && !defined(__MACOSX__) && \
    !defined(__SYMBIAN32__) && !defined(QWS)
#undef main

/*
 * Вот так всё непросто.
 */
int main (int argc, char *argv[])
{
	extern int SDL_main (int, char**);

	return SDL_main (argc, argv);
}
#endif

#else /* HAVE_LIBSDL */
void besm6_draw_panel ()
{
}
#endif /* HAVE_LIBSDL */
