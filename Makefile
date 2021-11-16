CC=gcc
CFLAGS=-Wall -Werror -O2 -g
OBJ= oss.o user_proc.o
DEPS= oss.c user_proc.c

all: oss user

%.o: %.c $(DEPS)
	$(CC) $(CFLAGS) -c -o $@ $<
oss: $(OBJ)
	$(CC) $(CFLAGS) -lm -o $@ $@.o

user_proc: $(OBJ)
	$(CC) $(CFLAGS) -lm -o $@ $@.o
clean:
	rm -rf oss user *.log *.o 
