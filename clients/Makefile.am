noinst_PROGRAMS =				\
	gears					\
	flower					\
	screenshot				\
	terminal				\
	image					\
	$(poppler_programs)			\
	dnd					\
	smoke					\
	resizor					\
	simple-client				\
	simple-shm				\
	eventdemo

noinst_LIBRARIES = libtoytoolkit.a

AM_CFLAGS = $(GCC_CFLAGS)
AM_CPPFLAGS =					\
	-DDATADIR='"$(datadir)"'		\
	$(CLIENT_CFLAGS)

libtoytoolkit_a_SOURCES =			\
	window.c				\
	window.h				\
	wayland-glib.c				\
	wayland-glib.h				\
	cairo-util.c				\
	cairo-util.h

toolkit_libs =					\
	libtoytoolkit.a				\
	$(CLIENT_LIBS) -lrt -lm

gears_SOURCES = gears.c
gears_LDADD = $(toolkit_libs)

flower_SOURCES = flower.c
flower_LDADD = $(toolkit_libs)

screenshot_SOURCES = screenshot.c screenshooter-protocol.c
screenshot_LDADD = $(toolkit_libs)

terminal_SOURCES = terminal.c
terminal_LDADD = $(toolkit_libs) -lutil

image_SOURCES = image.c
image_LDADD = $(toolkit_libs)

dnd_SOURCES = dnd.c
dnd_LDADD = $(toolkit_libs)

smoke_SOURCES = smoke.c
smoke_LDADD = $(toolkit_libs)

resizor_SOURCES = resizor.c
resizor_LDADD = $(toolkit_libs)

simple_client_SOURCES = simple-client.c
simple_client_LDADD = $(SIMPLE_CLIENT_LIBS) -lm

simple_shm_SOURCES = simple-shm.c
simple_shm_LDADD = $(CLIENT_LIBS) -lrt -lm

eventdemo_SOURCES = eventdemo.c
eventdemo_LDADD = $(toolkit_libs)

BUILT_SOURCES =					\
	screenshooter-client-protocol.h		\
	screenshooter-protocol.c

CLEANFILES = $(BUILT_SOURCES)

@wayland_scanner_rules@

if HAVE_POPPLER
poppler_programs = view
view_SOURCES = view.c
view_LDADD = $(toolkit_libs) $(POPPLER_LIBS)
view_CPPFLAGS = $(AM_CPPFLAGS) $(POPPLER_CFLAGS)
endif
