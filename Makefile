# GNU C99 (with extensions) by default: open-world registration uses
# `__attribute__((section))` for self-registering tests. gcc and clang work
# on Linux/macOS; MSVC needs the PE section walker (pending). Override the
# standard with `make STD=c11` etc, but stay on a GNU dialect.
CC      ?= cc
STD     ?= gnu99
CFLAGS  ?= -std=$(STD) -Wall -Wextra -g
CFLAGS  += -Wno-missing-field-initializers

.PHONY: all strict run tap list filter sizes clean

all: c-test example app app-notest libtest crash

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
	rm -f c-test example app app-notest libtest crash ctest_preload.so /tmp/ctest_flaky_marker

# Rebuild everything with warnings as errors.
strict:
	$(MAKE) clean all CFLAGS="-std=gnu99 -Wall -Wextra -Werror -Wno-missing-field-initializers"
