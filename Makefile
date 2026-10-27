CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2 -Iinclude -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=600
LDFLAGS ?= -lncursesw -lpthread

SRCDIR = src
INCDIR = include
BINDIR = bin
TARGET = $(BINDIR)/fleetMonitor

SRCS = $(wildcard $(SRCDIR)/*.c)
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS) | $(BINDIR)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

$(SRCDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(BINDIR):
	mkdir -p $(BINDIR)

install: $(TARGET)
	mkdir -p $(HOME)/bin
	install -m 755 $(TARGET) $(HOME)/bin/fleetMonitor
	ln -sf $(HOME)/bin/fleetMonitor $(HOME)/bin/fleet-monitor
	@echo "Installed to $(HOME)/bin/fleetMonitor (symlinked as fleet-monitor)"

clean:
	rm -f $(SRCDIR)/*.o $(TARGET)

.PHONY: all install clean
