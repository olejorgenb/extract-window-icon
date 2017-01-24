

main: main.c
# Note make highjacks the normal $() synax (!) (use `cmd` or $(shell ))
	gcc main.c -o main `pkg-config --cflags --libs glib-2.0 xcb xcb-atom xcb-icccm xcb-ewmh xcb-util cairo cairo-png cairo-xcb` 
