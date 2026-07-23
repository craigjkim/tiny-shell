#
# Makefile for tsh
#
CC     = gcc
CFLAGS = -Wall -Wextra -O -time
LDFLAGS = -lreadline -lhistory

OBJ    = tsh.o parse.o commands.o jobs.o history.o
TARGET = tsh

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJ) $(LDFLAGS)

tsh.o: tsh.c parse.h commands.h jobs.h history.h
	$(CC) $(CFLAGS) -c tsh.c

parse.o: parse.c parse.h
	$(CC) $(CFLAGS) -c parse.c

commands.o: commands.c commands.h jobs.h
	$(CC) $(CFLAGS) -c commands.c

history.o: history.c history.h
	$(CC) $(CFLAGS) -c history.c

jobs.o: jobs.c jobs.h
	$(CC) $(CFLAGS) -c jobs.c

clean:
	rm -f $(OBJ) $(TARGET)

# vim: set ts=4 sw=4 ai noet:
