CC = gcc
CFLAGS = -Wall -Wextra -std=c11

SRC = memsim.c
OUT = memsim

all: $(OUT)

$(OUT): $(SRC)
	$(CC) $(CFLAGS) -o $(OUT) $(SRC)

clean:
	rm -f $(OUT)
	rm -f *.bin
	rm -f out*