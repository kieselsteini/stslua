CC=cc -std=c99 -Wall -Wextra
LIB=-llua
OBJ=sts_base64.o sts_msgpack.o test.o
BIN=sts_test

default: $(OBJ)
	$(CC) -o $(BIN) $(OBJ) $(LIB)

clean:
	rm -f $(BIN) $(OBJ)
