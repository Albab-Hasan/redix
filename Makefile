CC = gcc
CFLAGS = -Wall -Wextra

SRCS = main.c lexer.c parser.c codegen.c
OBJS = $(SRCS:.c=.o)
HDRS = lexer.h parser.h codegen.h

redix: $(OBJS)
	$(CC) -o redix $(OBJS)

# a struct changing shape in a header has to rebuild every object or the halves disagree on its size
$(OBJS): $(HDRS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

test: redix
	@bash test.sh

clean:
	rm -f redix $(OBJS) out.s out.o out

.PHONY: test clean
