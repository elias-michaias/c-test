# ISO C99 by default. Any C99-capable compiler works; override the standard
# with `make STD=gnu99` if you want GNU extensions.
CC      ?= cc
STD     ?= c99
CFLAGS  ?= -std=$(STD) -Wall -Wextra -pedantic -g
CFLAGS  += -Wno-missing-field-initializers

.PHONY: all strict run tap list filter sizes clean

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

# Rebuild everything with warnings as errors under ISO C99.
strict:
	$(MAKE) clean all CFLAGS="-std=c99 -Wall -Wextra -pedantic -Werror -Wno-missing-field-initializers"
