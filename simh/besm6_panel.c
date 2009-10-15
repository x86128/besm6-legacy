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
#define WIDTH		720
#define HEIGHT		240
#define DEPTH		32
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
static const SDL_Color grey  = { 32,  32,  32,  0 };
static t_value old_BRZ [8];

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
 * Периодическая отрисовка: мигание лампочек.
 */
static void draw_periodic()
{
	int x, y;
	t_value val;

	for (y=0; y<8; ++y) {
		val = BRZ [7-y];
		if (val == old_BRZ [7-y])
			continue;
		for (x=0; x<48; ++x) {
			draw_lamp (100 + x*12, 34 + 24*y, val >> (47-x) & 1);
		}
		old_BRZ [7-y] = val;
		SDL_UpdateRect (screen, 100, 34 + 24*y, 48*12, 58 + 24*y);
	}
}

/*
 * Отрисовка статичной части панели БЭСМ-6.
 */
static void draw_static()
{
	int x, y, color;
	char message [40];
	SDL_Rect area;

	background = black;
	foreground = cyan;

	/* Оттеняем группы разрядов. */
	color = grey.r << 16 | grey.g << 8 | grey.b;
	for (x=3; x<48; x+=3) {
		area.x = 99 + x*12;
		area.y = 26;
		area.w = 2;
		area.h = 24*8 + 2;
		SDL_FillRect (screen, &area, color);
	}
	/* Названия регистров. */
	for (y=7; y>=0; --y) {
		sprintf (message, "брз %d", 7-y);
		render_utf8 (font_big, 24, 24 + 24*y, 1, message);
		old_BRZ[y] = ~0;
	}
	/* Номера битов. */
	for (x=0; x<48; ++x) {
		sprintf (message, "%d", 48-x);
		render_utf8 (font_small, 106 + x*12, 10, 0, message);
	}
	/* Tell SDL to update the whole screen */
	SDL_UpdateRect (screen, 0, 0, WIDTH, HEIGHT);
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
	font_big = TTF_OpenFont (font_path, 24);
	font_small = TTF_OpenFont (font_path, 8);
	if (! font_big || ! font_small) {
		fprintf(stderr, "SDL: couldn't load font %s: %s\n",
			FONTNAME, SDL_GetError());
		besm6_close_panel();
		exit (1);
	}
	atexit (besm6_close_panel);
	draw_static();
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
	draw_periodic();

	/* Unlock if needed */
	if (SDL_MUSTLOCK (screen))
		SDL_UnlockSurface (screen);

	/* Exit SIMH when window closed.*/
	SDL_Event event;
	if (SDL_PollEvent (&event) && event.type == SDL_QUIT)
		longjmp (cpu_halt, SCPE_EXIT);
}
#else /* HAVE_LIBSDL */
void besm6_draw_panel ()
{
}
#endif /* HAVE_LIBSDL */
