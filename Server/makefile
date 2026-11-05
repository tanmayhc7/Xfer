CC     = gcc
CFLAGS = -g -Wall -Wextra
TARGET = server
SRCS   = command_handler.c server_ftp_udp.c

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRCS) command_handler.h
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS)

clean:
	rm -rf $(TARGET) *.o *.dSYM
