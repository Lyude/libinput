lib_LTLIBRARIES = libinput.la
noinst_LTLIBRARIES = libinput-util.la

include_HEADERS =			\
	libinput.h

libinput_la_SOURCES =			\
	libinput.c			\
	libinput.h			\
	libinput-private.h		\
	evdev.c				\
	evdev.h				\
	evdev-mt-touchpad.c		\
	evdev-mt-touchpad.h		\
	evdev-mt-touchpad-tap.c		\
	evdev-mt-touchpad-buttons.c	\
	evdev-mt-touchpad-edge-scroll.c	\
	filter.c			\
	filter.h			\
	filter-private.h		\
	path.h				\
	path.c				\
	udev-seat.c			\
	udev-seat.h			\
	timer.c				\
	timer.h				\
	../include/linux/input.h

libinput_la_LIBADD = $(MTDEV_LIBS) \
		     $(LIBUDEV_LIBS) \
		     $(LIBEVDEV_LIBS) \
		     libinput-util.la

libinput_la_CFLAGS = -I$(top_srcdir)/include \
		     $(MTDEV_CFLAGS)	\
		     $(LIBUDEV_CFLAGS)	\
		     $(LIBEVDEV_CFLAGS)	\
		     $(GCC_CFLAGS)
EXTRA_libinput_la_DEPENDENCIES = $(srcdir)/libinput.sym

libinput_util_la_SOURCES = \
	libinput-util.c		\
	libinput-util.h

libinput_util_la_LIBADD =
libinput_util_la_CFLAGS = -I$(top_srcdir)/include \
			  $(LIBUDEV_CFLAGS) \
			  $(GCC_CFLAGS)

libinput_la_LDFLAGS = -version-info $(LIBINPUT_LT_VERSION) -shared \
		      -Wl,--version-script=$(srcdir)/libinput.sym

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libinput.pc

AM_CFLAGS = $(GCC_CFLAGS)

DISTCLEANFILES = libinput-version.h
EXTRA_DIST = libinput-version.h.in libinput.sym
