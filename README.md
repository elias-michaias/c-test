# c-test

Single-header test framework for C99 and C++11. Works on GCC, Clang, and MSVC. No boilerplate, no test lists — just write tests.

```c
#include "ctest.h"

it("adds numbers") {
    expect(1 + 2 == 3);
}

it("with a message") {
    expect(1 + 2 == 4, "expected 4; got 3");
}
```

```sh
cc mytest.c -I. -DCTEST -o mytest
c-test ./mytest
```

## Build c-test

```sh
make run gcc       # or clang / msvc
```

Output goes to `dist/`. MSVC currently requires [msvc-wine](https://github.com/mstorsjo/msvc-wine).

## Test spec options

```c
it("name")                          
it("name", .skip = 1)                 // always skip
it("name", .only = 1)                 // run with --only
it("name", .timeout = 500)            // ms; kills hung tests
it("name", .platforms = {"linux"})    // skip on other platforms
it("name", .std = "c11")              // skip unless -std=c11 (or gnu11)
it("name", .known = "BUG-42")         // expected failure; reported, not fatal
it("name", .tags = {"math", "fast"})  // run by tag later 
```

## Parameterized tests

```c
typedef struct { int a, b, sum; } add_case;
static const add_case cases[] = { {1,2,3}, {4,5,9} };

it("addition", cases(add_case, cases)) {
    expect(it.a + it.b == it.sum);
}
```

`it` is the current case value. Works with any type, including scalars:

```c
static const int primes[] = {2, 3, 5, 7, 11};
it("is prime", cases(int, primes)) {
    expect(it > 1);
}
```

## Lifecycle hooks

```c
void my_setup(void)        { db = connect(); }
void my_teardown(void)     { disconnect(db); }
void my_before_each(void)  { begin_tx(); }
void my_after_each(void)   { rollback(); }

#define ctest_setup        my_setup
#define ctest_teardown     my_teardown
#define ctest_before_each  my_before_each
#define ctest_after_each   my_after_each

#include "ctest.h"
```

## Signal / crash assertions

```c
it("aborts on bad input") {
    expect_abort({ process(NULL); });
}

it("segfaults on null deref") {
    expect_signal(SIGSEGV, { *(int *)0 = 1; });
}
```

On POSIX: uses `fork()` for full isolation. On Windows (MSVC): uses SEH (`__try`/`__except`) — `abort()` is bridged via a SIGABRT handler that raises a catchable Win32 exception. MinGW has no implementation (no-op).

## CLI

```
c-test [options] <binary> [binary ...]

--tags <query>     e.g. "math", "math,fast" (OR), "math+fast" (AND), "!slow" (NOT)
--filter <text>    run tests whose name contains <text> (prefix ! to exclude)
--only             run only tests marked .only = 1
--shuffle          randomize order; --seed <n> for reproducibility
--retries <n>      re-run failing tests; flaky tests are reported but not fatal
--timeout <ms>     global timeout per test (default: none)
--tap              TAP output
--quiet            minimal output
--list             print test names and exit
```

## C++ support

```cpp
#include "ctest.h"
#include <string>

it("std::string") {
    std::string s = "hello";
    expect(s.size() == 5);
}
```

```sh
g++ -std=c++11 mytest.cpp -I. -DCTEST -o mytest
```

MSVC requires `/std:c++20` (for designated initializers). Field order in `it()` must match declaration order: `skip`, `only`, `platforms`, `std`, `known`, `timeout`, `tags`.

