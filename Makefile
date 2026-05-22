CC = gcc
CFLAGS = -Wall -Wextra

SRCS = main.c lexer.c parser.c codegen.c
OBJS = $(SRCS:.c=.o)

redix: $(OBJS)
	$(CC) -o redix $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

test: redix
	@bash test.sh

clean:
	rm -f redix $(OBJS) out.s out.o out

.PHONY: test clean
