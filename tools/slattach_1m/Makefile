# Makefile for slattach_1m - High-speed SLIP attach tool
#
# Supports baud rates up to 4 Mbaud for FPGA SLIP interfaces

CC = gcc
CFLAGS = -O2 -Wall -Wextra -D_GNU_SOURCE
TARGET = slattach_1m

all: $(TARGET)

$(TARGET): slattach.c
	$(CC) $(CFLAGS) -o $(TARGET) slattach.c

clean:
	rm -f $(TARGET) *.o

install: $(TARGET)
	install -m 0755 $(TARGET) /usr/local/bin/

uninstall:
	rm -f /usr/local/bin/$(TARGET)

test: $(TARGET)
	@echo "Testing supported baud rates..."
	@strings $(TARGET) | grep -E "^[0-9]{6,}$$" | sort -n
	@echo ""
	@echo "Version:"
	@./$(TARGET) -V

.PHONY: all clean install uninstall test
