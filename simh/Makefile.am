bin_PROGRAMS = besm6
besm6_SOURCES = scp.c sim_console.c sim_fio.c sim_timer.c sim_sock.c \
	sim_tmxr.c sim_ether.c sim_tape.c sim_readline.c sim_serial.c sim_disk.c \
	besm6_cpu.c besm6_sys.c besm6_mmu.c besm6_arith.c \
	besm6_printer.c besm6_tty.c besm6_drum.c besm6_disk.c \
	besm6_punch.c besm6_panel.c
AM_CFLAGS = -Wall -g -O2
AM_CPPFLAGS = -DUSE_INT64 -DTMXR_MAXBUF=1024 -D_GNU_SOURCE

clean-local:
	-rm -rf *~

besm6_LDADD =
if HAVE_READLINE
AM_CPPFLAGS += -DHAVE_READLINE
besm6_LDADD += -ldl
endif

if HAVE_LIBSDL2
AM_CPPFLAGS += -DHAVE_LIBSDL2 `sdl2-config --cflags`
besm6_LDFLAGS = `sdl2-config --libs`
besm6_LDADD += -lSDL2
else
if HAVE_LIBSDL
AM_CPPFLAGS += -DHAVE_LIBSDL -Dmain=SDL_main `sdl-config --cflags`
besm6_LDFLAGS = `sdl-config --libs`
besm6_LDADD += -lSDL
endif
endif
