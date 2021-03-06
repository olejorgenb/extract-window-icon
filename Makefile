.DEFAULT_GOAL := extract-window-icon

PREFIX=/usr/local

extract-window-icon: extract-window-icon.c
	gcc extract-window-icon.c -o extract-window-icon \
	`pkg-config --cflags --libs glib-2.0 xcb xcb-atom xcb-icccm xcb-ewmh xcb-util cairo cairo-png cairo-xcb` 

install:
	mkdir -p $(PREFIX)/bin
	install extract-window-icon $(PREFIX)/bin/
