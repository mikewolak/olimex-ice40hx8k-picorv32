#===============================================================================
# SLIP Performance Test Client - Build System
#===============================================================================

CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c99 -DDEBUG_MODE
LDFLAGS =

TARGET = slip_perf_client
SRC = slip_perf_client.c
OBJ = $(SRC:.c=.o)

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(TARGET) $(OBJ)

test: $(TARGET)
	@echo "Usage: ./$(TARGET) <server_ip> [options]"
	@echo ""
	@echo "Options:"
	@echo "  -d <seconds>   Test duration (default: 2)"
	@echo "  -t <seconds>   Socket timeout (default: 1800)"
	@echo "  -b             Bidirectional mode"
	@echo ""
	@echo "Example:"
	@echo "  ./$(TARGET) 192.168.100.2 -d 2 -t 30"
