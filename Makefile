CC      ?= cc
CFLAGS  ?= -std=gnu99 -Wall -Wextra -pedantic -g
CFLAGS  += -Wno-missing-field-initializers

.PHONY: all run tap list filter sizes clean

all: c-test example app app-notest libtest

c-test: ctest.c ctest.h
	$(CC) $(CFLAGS) -o $@ ctest.c

example: example.c ctest.h
	$(CC) $(CFLAGS) -DCTEST -o $@ example.c

app: app.c ctest.h
	$(CC) $(CFLAGS) -DCTEST -o $@ app.c

crash: crash.c ctest.h
	$(CC) $(CFLAGS) -DCTEST -o $@ crash.c

libtest: libtest.c ctest.h
	$(CC) $(CFLAGS) -DCTEST -o $@ libtest.c

app-notest: app.c ctest.h
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections -Wl,--gc-sections -o $@ app.c

run: c-test example
	./c-test ./example

tap: c-test example
	./c-test ./example --tap

list: c-test example
	./c-test ./example --list

filter: c-test example
	./c-test ./example --filter primes

sizes: example app app-notest
	@size example app app-notest

clean:
	rm -f c-test example app app-notest libtest ctest_preload.so
