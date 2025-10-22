# Makefile for Linux Performance Test Server

CC = gcc
CFLAGS = -Wall -Wextra -O2 -g
TARGET = slip_perf_server_linux

all: $(TARGET)

$(TARGET): slip_perf_server_linux.c
	$(CC) $(CFLAGS) -o $(TARGET) slip_perf_server_linux.c

clean:
	rm -f $(TARGET)

test: $(TARGET)
	@echo "Starting server on port 8888..."
	@echo "Run this in another terminal:"
	@echo "  ../slip_perf_client/slip_perf_client 127.0.0.1 -d 2"
	@echo ""
	./$(TARGET)

.PHONY: all clean test
