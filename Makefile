# NetChecker GUI - build with: make
# On Windows, run this from an MSYS2 MinGW64 shell.

CC = gcc
PKG_CONFIG = pkg-config
CFLAGS = -Wall -O2 `$(PKG_CONFIG) --cflags gtk+-3.0`
LIBS = `$(PKG_CONFIG) --libs gtk+-3.0` -lpthread

# Detect a MinGW/Windows target from gcc's own target triple rather than the
# $(OS) environment variable -- $(OS) isn't reliably inherited into every
# MSYS2 shell session, which silently drops the -liphlpapi/-lws2_32 flags
# the ICMP ping implementation needs and causes "undefined reference to
# IcmpCreateFile/IcmpSendEcho/..." at link time.
GCC_TARGET := $(shell $(CC) -dumpmachine)

ifneq (,$(findstring mingw,$(GCC_TARGET)))
    TARGET = netchecker_gui.exe
    LDFLAGS_EXTRA = -mwindows
    LIBS += -liphlpapi -lws2_32
else
    TARGET = netchecker_gui
    LDFLAGS_EXTRA =
endif

all: $(TARGET)

$(TARGET): netchecker_gui.c
	$(CC) netchecker_gui.c -o $(TARGET) $(CFLAGS) $(LIBS) $(LDFLAGS_EXTRA)

clean:
	rm -f netchecker_gui netchecker_gui.exe

