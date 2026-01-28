# Makefile for NVIDIA GPU Monitor

CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -ldl
TARGET = gpu_monitor
SOURCE = gpu_monitor.c

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCE) $(LDFLAGS)

clean:
	rm -f $(TARGET)

install:
	install -d /usr/local/bin
	install -m 755 $(TARGET) /usr/local/bin/$(TARGET)

uninstall:
	rm -f /usr/local/bin/$(TARGET)

.PHONY: all clean install uninstall
