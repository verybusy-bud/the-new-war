#
# SPDX-FileCopyrightText: Copyright (C) 1987, 1988 Chuck Simmons
# SPDX-License-Identifier: GPL-2.0+
#
# See the file COPYING, distributed with empire, for restriction
# and warranty information.

VERS=$(shell sed -n <NEWS '/^[0-9]/s/:.*//p' | head -1)

# Use -g to compile the program for debugging.
#DEBUG = -g -DDEBUG
DEBUG = -O2

# Use -p to profile the program.
#PROFILE = -p -DPROFILE
PROFILE =

LIBS = -lncurses
SDL_LIBS = -lSDL2

# You shouldn't have to modify anything below this line.

# There's a dynamic format in the object-display routines; suppress the warning
CFLAGS = $(DEBUG) $(PROFILE) -Wall -Wno-format-security
SDL_CFLAGS = $(DEBUG) $(PROFILE) -Wall -Wno-format-security -I/usr/include/SDL2

FILES = \
	attack.c \
	compmove.c \
	data.c \
	display.c \
	edit.c \
	empire.c \
	game.c \
	main.c \
	map.c \
	math.c \
	object.c \
	term.c \
	usermove.c \
	util.c

SDL_FILES = \
	attack.c \
	compmove.c \
	data.c \
	display_sdl.c \
	edit.c \
	empire.c \
	game.c \
	main.c \
	map.c \
	math.c \
	object.c \
	term.c \
	usermove.c \
	util.c

HEADERS = empire.h extern.h

OFILES = \
	attack.o \
	compmove.o \
	data.o \
	display.o \
	edit.o \
	empire.o \
	game.o \
	main.o \
	map.o \
	math.o \
	object.o \
	sdl_stubs.o \
	term.o \
	usermove.o \
	util.o

SDL_OFILES = \
	attack_sdl.o \
	compmove_sdl.o \
	data_sdl.o \
	edit_sdl.o \
	empire_sdl.o \
	game_sdl.o \
	main_sdl.o \
	map_sdl.o \
	math_sdl.o \
	object_sdl.o \
	term_sdl.o \
	usermove_sdl.o \
	util_sdl.o

all: tnw tnw-gui tnw.6 tnw.html

tnw: $(OFILES)
	$(CC) $(PROFILE) -o tnw $(OFILES) $(LIBS)

tnw-gui: $(SDL_OFILES)
	$(CC) $(PROFILE) -o tnw-gui $(SDL_OFILES) $(SDL_LIBS)

attack.o:: extern.h empire.h
compmove.o:: extern.h empire.h
data.o:: empire.h
display.o:: extern.h empire.h
edit.o:: extern.h empire.h
empire.o:: extern.h empire.h
game.o:: extern.h empire.h
main.o:: extern.h empire.h
map.o:: extern.h empire.h
math.o:: extern.h empire.h
object.o:: extern.h empire.h
term.o:: extern.h empire.h
usermove.o:: extern.h empire.h
util.o:: extern.h empire.h

# SDL GUI object files
attack_sdl.o: attack.c extern.h empire.h
	$(CC) $(SDL_CFLAGS) -DUSE_SDL -c -o $@ attack.c

compmove_sdl.o: compmove.c extern.h empire.h
	$(CC) $(SDL_CFLAGS) -DUSE_SDL -c -o $@ compmove.c

data_sdl.o: data.c empire.h
	$(CC) $(SDL_CFLAGS) -DUSE_SDL -c -o $@ data.c

display_sdl.o: display_sdl.c extern.h empire.h
	$(CC) $(SDL_CFLAGS) -c -o $@ display_sdl.c

edit_sdl.o: edit.c extern.h empire.h
	$(CC) $(SDL_CFLAGS) -DUSE_SDL -c -o $@ edit.c

empire_sdl.o: empire.c extern.h empire.h
	$(CC) $(SDL_CFLAGS) -DUSE_SDL -c -o $@ empire.c

game_sdl.o: game.c extern.h empire.h
	$(CC) $(SDL_CFLAGS) -DUSE_SDL -c -o $@ game.c

main_sdl.o: main.c extern.h empire.h
	$(CC) $(SDL_CFLAGS) -DUSE_SDL -c -o $@ main.c

map_sdl.o: map.c extern.h empire.h
	$(CC) $(SDL_CFLAGS) -DUSE_SDL -c -o $@ map.c

math_sdl.o: math.c empire.h
	$(CC) $(SDL_CFLAGS) -DUSE_SDL -c -o $@ math.c

object_sdl.o: object.c extern.h empire.h
	$(CC) $(SDL_CFLAGS) -DUSE_SDL -c -o $@ object.c

term_sdl.o: term_sdl.c extern.h empire.h
	$(CC) $(SDL_CFLAGS) -c -o $@ term_sdl.c

usermove_sdl.o: usermove.c extern.h empire.h
	$(CC) $(SDL_CFLAGS) -DUSE_SDL -c -o $@ usermove.c

util_sdl.o: util.c extern.h empire.h
	$(CC) $(SDL_CFLAGS) -DUSE_SDL -c -o $@ util.c

sdl_stubs.o: sdl_stubs.c
	$(CC) $(SDL_CFLAGS) -c -o $@ sdl_stubs.c

empire.6: vms-empire.xml
	xmlto man vms-empire.xml

tnw.6: tnw.xml
	xmlto man tnw.xml

vms-empire.html: vms-empire.xml
	xmlto html-nochunks vms-empire.xml

tnw.html: tnw.xml
	xmlto html-nochunks tnw.xml

TAGS: $(HEADERS) $(FILES)
	etags $(HEADERS) $(FILES)

lint: $(FILES)
	lint -u -D$(SYS) $(FILES) -lcurses

spellcheck:
	@spellcheck vms-empire.xml

# This should run clean
cppcheck:
	@cppcheck --quiet --inline-suppr --suppress=missingIncludeSystem --suppress=unusedFunction --template=gcc --enable=all --force *.[ch]

install: empire.6 uninstall
	install -m 0755 -d $(DESTDIR)/usr/bin
	install -m 0755 -d $(DESTDIR)/usr/share/man/man6
	install -m 0755 -d $(DESTDIR)/usr/share/applications/
	install -m 0755 -d $(DESTDIR)/usr/share/icons/hicolor/48x48/apps/
	install -m 0755 -d $(DESTDIR)/usr/share/appdata
	install -m 0755 vms-empire $(DESTDIR)/usr/bin/
	install -m 0644 empire.6 $(DESTDIR)/usr/share/man/man6/vms-empire.6
	install -m 0644 vms-empire.desktop $(DESTDIR)/usr/share/applications/
	install -m 0644 vms-empire.png $(DESTDIR)/usr/share/icons/hicolor/48x48/apps/
	install -m 0644 vms-empire.xml $(DESTDIR)/usr/share/appdata/

uninstall:
	rm -f /usr/bin/vms-empire /usr/share/man/man6/vms-empire.6
	rm -f /usr/share/applications/vms-empire.desktop
	rm -f /usr/share/icons/hicolor/48x48/apps/vms-empire.png
	rm -f /usr/share/appdata/vms-empire.xml

clean:
	rm -f *.o TAGS tnw tnw-gui
	rm -f *.6 *.html
	rm -f *.sav

reflow:
	@clang-format --style="{IndentWidth: 8, UseTab: ForIndentation}" -i $$(find . -name "*.[ch]")

clobber: clean
	rm -f vms-empire vms-empire-*.tar*

SOURCES = README.adoc HACKING NEWS control empire.6 vms-empire.xml COPYING Makefile BUGS AUTHORS $(FILES) $(HEADERS) vms-empire.png vms-empire.desktop

vms-empire-$(VERS).tar.gz: $(SOURCES)
	@ls $(SOURCES) | sed s:^:vms-empire-$(VERS)/: >MANIFEST
	@(cd ..; ln -s vms-empire vms-empire-$(VERS))
	(cd ..; tar -czf vms-empire/vms-empire-$(VERS).tar.gz `cat vms-empire/MANIFEST`)
	@(cd ..; rm vms-empire-$(VERS))

dist: vms-empire-$(VERS).tar.gz

release: vms-empire-$(VERS).tar.gz vms-empire.html
	shipper version=$(VERS) | sh -e -x

refresh: vms-empire.html
	shipper -N -w version=$(VERS) | sh -e -x
