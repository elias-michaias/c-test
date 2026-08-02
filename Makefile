CC      ?= cc
CFLAGS  ?= -std=gnu11 -Wall -Wextra -pedantic -g
CFLAGS  += -Wno-missing-field-initializers

.PHONY: all run tap list filter sizes clean

all: example app app-notest

example: example.c ctest.c ctest.h
	$(CC) $(CFLAGS) -DCTEST -o $@ example.c ctest.c

app: app.c ctest.c ctest.h
	$(CC) $(CFLAGS) -DCTEST -o $@ app.c ctest.c

crash: crash.c ctest.c ctest.h
	$(CC) $(CFLAGS) -DCTEST -o $@ crash.c ctest.c

app-notest: app.c ctest.h
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections -Wl,--gc-sections -o $@ app.c

run: example
	./example

tap: example
	./example --tap

list: example
	./example --list

filter: example
	./example --filter primes

sizes: example app app-notest
	@size example app app-notest

clean:
	rm -f example app-notest
