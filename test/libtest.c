// clang-format off
#define ctest_setup libtest_setup
#define ctest_before_each libtest_before_each
#define ctest_after_each libtest_after_each
void libtest_setup(void);
void libtest_before_each(void);
void libtest_after_each(void);
#include "ctest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_setup, g_before, g_after;

void libtest_setup(void) { g_setup++; }
void libtest_before_each(void) { g_before++; }
void libtest_after_each(void) { g_after++; }

it("custom failure message", .tags = {"msg"})
{
    int x = 5;
    expect(x == 6, "x should be six");
}

it("expect passes with a message", .tags = {"msg"})
{
    expect(2 + 2 == 4, "basic arithmetic");
}

typedef struct { int a, b, product; } mult_case;

static const mult_case mult_cases[] = {
    { 2, 3, 6 },
    { 3, 4, 12 },
    { -1, 5, -5 },
};

it("multiply", cases(mult_case, mult_cases))
{
    expect(it.a * it.b == it.product, "multiplication");
}

it("expect_abort catches abort", .tags = {"sig"})
{
    expect_abort({ abort(); });
}

it("expect_signal catches segfault", .tags = {"sig"})
{
    expect_signal(SIGSEGV, { *(volatile int *)0 = 1; });
}

it("no signal reported as failure", .tags = {"sig", "broken"})
{
    expect_abort({ });
}

it("hooks run before the body", .tags = {"hooks"})
{
    expect(g_setup == 1, "setup ran once");
    expect(g_before == 1, "before_each ran once");
    expect(g_after == 0, "after_each has not run yet");
}

it("flaky once, then passes", .tags = {"flaky"})
{
    const char *p = "/tmp/ctest_flaky_marker";
    FILE *f = fopen(p, "r");
    if (f) {
        fclose(f);
        remove(p);
        expect(1, "passed on the retry");
    } else {
        f = fopen(p, "w");
        fclose(f);
        expect(0, "failed on the first run");
    }
}

it("known issue reproduces", .known = "JIRA-1234")
{
    expect(1 == 2, "known bug: off-by-one in the counter");
}

it("times out", .timeout = 100)
{
    for (;;) {}
}

/* ---------------------------------------------------------------- */
/* parameterized cases: exhaustive coverage                          */
/* ---------------------------------------------------------------- */
typedef struct { int input; int expected; const char *label; } add_case;
static const add_case add_cases[] = {
    { 1, 2, "one" },
    { 2, 4, "two" },
    { 40, 80, "forty" },
    { -5, -10, "negative" },
};

it("double input", .tags = {"param", "math"}, cases(add_case, add_cases))
{
    expect(it.input * 2 == it.expected, "doubling");
    expect(it.label[0] != '\0', "label present");
}

/* primitive element type: `it` is a plain int */
static const int prime_samples[] = { 2, 3, 5, 7, 11, 13, 17, 19 };

it("prime samples", cases(int, prime_samples))
{
    expect(it > 1 && (it == 2 || it % 2 == 1), "odd, or exactly two");
}

/* a single-element table: one aggregate entry with one case */
static const int one_case[] = { 42 };

it("single case", cases(int, one_case))
{
    expect(it == 42, "the only case");
}

/* designated initializers in the table */
typedef struct { int a; int b; int sum; } di_case;
static const di_case di_cases[] = {
    { .a = 1, .b = 2, .sum = 3 },
    { .a = -7, .b = 7, .sum = 0 },
    { .a = 100, .b = 200, .sum = 300 },
};

it("designated cases", cases(di_case, di_cases))
{
    expect(it.a + it.b == it.sum, "sum of designators");
}

/* non-const table with string + size_t fields */
typedef struct { const char *word; size_t len; } word_case;
static const word_case words[] = {
    { "one", 3 },
    { "four", 4 },
    { "eight", 5 },
};

it("words", cases(word_case, words))
{
    expect(strlen(it.word) == it.len, "word length");
}

/* the body receives a by-value copy; mutating it must not touch the table */
static int mutable_cases[] = { 7, 8, 9 };

it("body copies, array untouched", cases(int, mutable_cases))
{
    it = 0;
    expect(it == 0 && mutable_cases[0] == 7 && mutable_cases[1] == 8 &&
           mutable_cases[2] == 9,
           "body works on a copy; source array intact");
}

/* enum-valued element type */
typedef enum { CT_E_RED = 1, CT_E_GREEN = 2, CT_E_BLUE = 4 } ct_rgb_t;

static const ct_rgb_t color_cases[] = { CT_E_RED, CT_E_GREEN, CT_E_BLUE };

it("enum cases", cases(ct_rgb_t, color_cases))
{
    expect(it == 1 || it == 2 || it == 4, "a valid color");
}

/* floating-point fields (struct larger than an int, covers sizeof stepping) */
typedef struct { double a; double b; double product; } fp_case;
static const fp_case fp_cases[] = {
    { 0.5, 2.0, 1.0 },
    { 1.5, 4.0, 6.0 },
    { -2.25, 4.0, -9.0 },
};

it("float cases", cases(fp_case, fp_cases))
{
    expect(it.a * it.b == it.product, "fp product");
}

/* large struct: nested arrays and padding */
typedef struct {
    int id;
    char buf[16];
    double vals[2];
} big_case;

static const big_case big_cases[] = {
    { 1, "alpha", { 1.5, 2.5 } },
    { 2, "beta", { -1.0, 3.0 } },
};

it("big cases", cases(big_case, big_cases))
{
    expect(it.buf[0] == 'a' || it.buf[0] == 'b', "name prefix");
    expect(it.vals[0] + it.vals[1] != 0.0, "vals sum to a non-zero total");
}

/* one aggregate entry: case [1] fails, cases [0] and [2] pass */
typedef struct { int v; } trap_case;

static const trap_case trap_cases[] = { { 1 }, { 2 }, { 3 } };

it("traps on the middle case", cases(trap_case, trap_cases))
{
    expect(it.v != 2, "case 2 is broken on purpose");
}

/* several failing expects in a single case are all reported under it */
typedef struct { int v; } multi_case;
static const multi_case multi_cases[] = { { 0 } };

it("multiple failures in one case", cases(multi_case, multi_cases))
{
    expect(it.v == 1, "first miss");
    expect(it.v == 2, "second miss");
}

/* .known is test-level: the case-[2] failure makes the aggregate known */
typedef struct { int x; } known_case;
static const known_case known_cases[] = { { 1 }, { 2 }, { 3 } };

it("known param bug", .known = "JIRA-99", cases(known_case, known_cases))
{
    expect(it.x < 3, "value 3 not implemented");
}

/* .timeout wraps each case: case [2] spins and is aborted, the rest pass */
typedef struct { int spin; } slow_case;
static const slow_case slow_cases[] = { { 0 }, { 0 }, { 1 } };

it("per-case timeout", .timeout = 50, cases(slow_case, slow_cases))
{
    while (it.spin) {}
}

/* .skip gates the whole aggregate */
typedef struct { int v; } skip_case;
static const skip_case skip_cases[] = { { 1 }, { 2 } };

it("skipped param", .skip = 1, cases(skip_case, skip_cases))
{
    expect(it.v > 0, "never runs");
}

/* .platforms gates the whole aggregate */
typedef struct { int v; } plat_case;
static const plat_case plat_cases[] = { { 1 }, { 2 } };

it("linux-only param", .platforms = {"linux"}, cases(plat_case, plat_cases))
{
    expect(it.v > 0, "runs on linux");
}

/* .std gates the whole aggregate (skipped: build is gnu99, not ISO C11) */
typedef struct { int v; } std_case;
static const std_case std_cases[] = { { 1 }, { 2 } };

it("c11 param", .std = "c11", cases(std_case, std_cases))
{
    expect(it.v > 0, "never runs: only runs in an ISO C11 build");
}

/* signal expectations inside a parameterized body */
typedef struct { int should_abort; } sig_case;
static const sig_case sig_cases[] = { { 0 }, { 1 } };

it("abort per case", cases(sig_case, sig_cases))
{
    if (it.should_abort)
        expect_abort({ abort(); });
    else
        expect(1, "no abort expected");
}

/* exactly CT_MAX_PARAM_CASES cases: the hard compile-time limit */
typedef struct { int n; } b_case;

static const b_case boundary_cases[] = {
    { 0 }, { 1 }, { 2 }, { 3 }, { 4 }, { 5 }, { 6 }, { 7 },
    { 8 }, { 9 }, { 10 }, { 11 }, { 12 }, { 13 }, { 14 }, { 15 },
    { 16 }, { 17 }, { 18 }, { 19 }, { 20 }, { 21 }, { 22 }, { 23 },
    { 24 }, { 25 }, { 26 }, { 27 }, { 28 }, { 29 }, { 30 }, { 31 },
    { 32 }, { 33 }, { 34 }, { 35 }, { 36 }, { 37 }, { 38 }, { 39 },
    { 40 }, { 41 }, { 42 }, { 43 }, { 44 }, { 45 }, { 46 }, { 47 },
    { 48 }, { 49 }, { 50 }, { 51 }, { 52 }, { 53 }, { 54 }, { 55 },
    { 56 }, { 57 }, { 58 }, { 59 }, { 60 }, { 61 }, { 62 }, { 63 },
};

it("boundary 64 cases", cases(b_case, boundary_cases))
{
    expect(it.n >= 0 && it.n <= 63, "in bounds");
}

/* long name exercises the registry name allocation */
static const int lc[] = { 1, 2 };

it("this is an extremely long parameterized test name that overflows the "
   "per-case name buffer used by the registry", cases(int, lc))
{
    expect(it == 1 || it == 2, "long-name cases");
}

/* ── generate() tests ─────────────────────────────────────────────────── */

typedef struct { int a; int b; } gen_add_case_t;

/* Generator: produces random pairs of small ints */
static void gen_add_case(void *out) {
    gen_add_case_t *c = (gen_add_case_t *)out;
    c->a = rand() % 100;
    c->b = rand() % 100;
}

it("generate addition", generate(gen_add_case_t, gen_add_case, 20))
{
    expect(it.a + it.b >= 0, "sum is non-negative");
}

/* Generator that always produces a known value (determinism check) */
static void gen_const(void *out) {
    int *p = (int *)out;
    *p = 42;
}

it("generate constant", generate(int, gen_const, 5))
{
    expect(it == 42, "generated value is always 42");
}

/* ── expect_no_leak tests ─────────────────────────────────────────────── */

it("no leak: alloc and free")
{
    expect_no_leak({
        char *p = (char *)malloc(64);
        free(p);
    });
}

it("no leak: no alloc")
{
    expect_no_leak({
        int x = 1 + 2;
        (void)x;
    });
}
