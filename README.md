# c-test

Single-header test framework for C99 and C++11. Works on GCC, Clang, TCC, and MSVC. No boilerplate, no test lists — just write tests.

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
make run gcc       # or clang / tcc / msvc
```

Output goes to `dist/`. MSVC currently requires [msvc-wine](https://github.com/mstorsjo/msvc-wine).

## Test spec options

```c
it("name")                          
it("name", .skip = 1)                 // always skip
it("name", .abort = 1)                // assert test calls abort()
it("name", .signal = SIGSEGV)         // assert test raises signal
it("name", .leak = 1)                 // allow memory leaks (default: assert no leak)
it("name", .no_alloc = 1)             // assert no heap allocation
it("name", .timeout = 500)            // ms; kills hung tests
it("name", .platforms = {"linux"})    // skip on other platforms
it("name", .std = "c11")              // skip unless -std=c11 (or gnu11)
it("name", .known = "BUG-42")         // expected failure; reported, not fatal
it("name", .tags = {"math", "fast"})  // run by tag later 
it("name", .depends_on = "setup db")  // skip if named test failed earlier
it("name", .retry = 3)               // retry up to 3 times before marking failed
it("name", .env = {{"KEY","val"},{0}}) // set env vars for this test; NULL value = unset
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

Global hooks (file-wide):

```c
#define ctest_setup        my_setup
#define ctest_before_each  my_before_each
#define ctest_after_each   my_after_each
#define ctest_teardown     my_teardown
#include "ctest.h"

void my_setup(void)                   { db = connect(); }
void my_teardown(void)                { disconnect(db); }
void my_before_each(const it_t *it)   { begin_tx(); }
void my_after_each(const it_t *it)    { rollback(); }
```

`ctest.h` emits `extern` declarations automatically — no forward declaration needed. The `it_t *` lets you inspect the current test's name, tags, timeout, etc. Use `it->data` for arbitrary per-test context shared with hooks.

Per-test hooks (inline on the test):

```c
void open_file(const it_t *it)  { g_fp = fopen((const char *)it->data, "rb"); }
void close_file(const it_t *it) { (void)it; fclose(g_fp); }

it("reads header", .setup = open_file, .teardown = close_file, .data = "data.bin") {
    expect(read_header(g_fp) == 0);
}
```

`.setup` is called with `it->result == CT_PASS` (not yet set). `.teardown` is called after the body with `it->result` populated — use it to log, send metrics, or conditionally clean up:

```c
void on_done(const it_t *it) {
    if (it->result == CT_FAIL)
        log_failure(it->name);
}

it("my test", .teardown = on_done) { ... }
```

`.setup` / `.teardown` run after `ctest_before_each` / before `ctest_after_each`. `after_each` also receives the populated `it_t`.

## Signal / crash assertions

```c
it("aborts on bad input", .abort = 1) {
    process(NULL);
}

it("segfaults on null deref", .signal = SIGSEGV) {
    *(int *)0 = 1;
}
```

On POSIX: the test body runs in a `fork()`ed child; the parent verifies it terminated with the expected signal. On Windows (MSVC): uses SEH (`__try`/`__except`).

## Safety assertions

```c
it("no memory leak") {
    void *p = malloc(64);
    free(p);        // passes — net zero
}

it("intentional leak", .leak = 1) {
    malloc(64);     // leaks OK, check suppressed
}

it("heap-free code path", .no_alloc = 1) {
    int x = 1 + 1;  // assert no heap allocation
}
```

`.leak = 0` (the default) asserts no net memory leak after the test. Supported on Linux, macOS (via `dlsym` interception), and MSVC debug builds.

```c
typedef struct { int a, b, sum; } triple;
static void add_cases(triple *out, size_t *n) {
    for (int i = 0; i < 5; i++) out[(*n)++] = (triple){i, i*2, i*3};
}

it("add", generate(triple, add_cases, 5)) {
    expect(it.a + it.b == it.sum);
}
```

## Safety assertions

```c
it("no memory leak") {
    void *p = malloc(64);
    free(p);
}

it("heap-free code path", .no_alloc = 1) {
    int x = 1 + 1;
    (void)x;
}

it("guarded buffer", .buf = 32) {
    ct_buf[0] = 0xff;  /* ct_buf is a 32-byte canary-guarded region */
}
```

`.buf = N` allocates an N-byte zero-initialized, canary-bracketed buffer before the body and exposes it as `ct_buf`. Overruns and underruns are reported as failures after the body returns.

```c
it("captures stdout") {
    ct_capture_stdout();
    printf("hello capture");
    expect(strcmp(ct_captured(), "hello capture") == 0);
}
```

## CLI

```
c-test [options] <binary> [binary ...]

--tags <query>     e.g. "math", "math,fast" (OR), "math+fast" (AND), "!slow" (NOT)
--filter <text>    run tests whose name contains <text> (prefix ! to exclude)
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

MSVC requires `/std:c++20` (for designated initializers). Field order in `it()` must match declaration order: `skip`, `leak`, `no_alloc`, `abort`, `signal`, `data`, `setup`, `teardown`, `platforms`, `std`, `known`, `timeout`, `tags`, `depends_on`, `retry`, `env`.

