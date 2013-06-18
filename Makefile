
FILES=soundrec_ui.cpp soundrec.cpp soundrec_dbus.cpp soundrec_dconf.cpp

CC=g++

OPTS=`pkg-config --cflags --libs gtk+-3.0 libpulse libpulse-mainloop-glib sndfile`

DCONF_OPTS=-I/usr/include/dconf -ldconf

recorder: $(FILES)
	$(CC) -Wall --std=c++11 -g -o soundrec $(FILES) $(OPTS) $(DCONF_OPTS)
