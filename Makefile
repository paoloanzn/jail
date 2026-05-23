CC      ?= clang
CFLAGS  ?= -Wall -Wextra -O2
PREFIX  ?= /usr/local
BINDIR  := $(PREFIX)/bin

BIN     := jail
SRC     := jail.c

.PHONY: build install uninstall clean

build: $(BIN)

$(BIN): $(SRC)
	$(CC) $(CFLAGS) -o $@ $<

install: build
	install -d $(DESTDIR)$(BINDIR)
	install -m 0755 $(BIN) $(DESTDIR)$(BINDIR)/$(BIN)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(BIN)

clean:
	rm -f $(BIN)
