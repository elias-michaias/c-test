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

it(t_custom_msg, "custom failure message", .tags = {"msg"})
{
    int x = 5;
    expect(x == 6, "x should be six");
}

it(t_expect_msg, "expect passes with a message", .tags = {"msg"})
{
    expect(2 + 2 == 4, "basic arithmetic");
}

typedef struct { int a, b, product; } mult_case;

static const mult_case mult_cases[] = {
    { 2, 3, 6 },
    { 3, 4, 12 },
    { -1, 5, -5 },
};

it(t_multiply, "multiply", cases(mult_cases))
{
    expect(it.a * it.b == it.product, "multiplication");
}

it(t_abort, "ct_expect_abort catches abort", .tags = {"sig"})
{
    ct_expect_abort({ abort(); });
}

it(t_segv, "ct_expect_signal catches segfault", .tags = {"sig"})
{
    ct_expect_signal(SIGSEGV, { *(volatile int *)0 = 1; });
}

it(t_no_signal, "no signal reported as failure", .tags = {"sig", "broken"})
{
    ct_expect_abort({ });
}

it(t_hooks, "hooks run before the body", .tags = {"hooks"})
{
    expect(g_setup == 1, "setup ran once");
    expect(g_before == 1, "before_each ran once");
    expect(g_after == 0, "after_each has not run yet");
}

it(t_flaky, "flaky once, then passes", .tags = {"flaky"})
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

it(t_known, "known issue reproduces", .known = "JIRA-1234")
{
    expect(1 == 2, "known bug: off-by-one in the counter");
}

it(t_timeout, "times out", .timeout = 100)
{
    for (;;) {}
}

/* ---------------------------------------------------------------- */
/* parameterized cases: exhaustive coverage                          */
/* ---------------------------------------------------------------- */
static const struct {
    int input;
    int expected;
    const char *label;
} add_cases[] = {
    { 1, 2, "one" },
    { 2, 4, "two" },
    { 40, 80, "forty" },
    { -5, -10, "negative" },
};

it(t_double, "double input", .tags = {"param", "math"}, cases(add_cases))
{
    expect(it.input * 2 == it.expected, "doubling");
    expect(it.label[0] != '\0', "label present");
}

/* primitive element type: `it` is a plain int */
static const int prime_samples[] = { 2, 3, 5, 7, 11, 13, 17, 19 };

it(t_prime, "prime samples", cases(prime_samples))
{
    expect(it > 1 && (it == 2 || it % 2 == 1), "odd, or exactly two");
}

/* a single-element table: one aggregate entry with one case */
static const int one_case[] = { 42 };

it(t_single, "single case", cases(one_case))
{
    expect(it == 42, "the only case");
}

/* designated initializers in the table */
static const struct { int a; int b; int sum; } di_cases[] = {
    { .a = 1, .b = 2, .sum = 3 },
    { .a = -7, .b = 7, .sum = 0 },
    { .a = 100, .b = 200, .sum = 300 },
};

it(t_designated, "designated cases", cases(di_cases))
{
    expect(it.a + it.b == it.sum, "sum of designators");
}

/* non-const table with string + size_t fields */
static const struct {
    const char *word;
    size_t len;
} words[] = {
    { "one", 3 },
    { "four", 4 },
    { "eight", 5 },
};

it(t_words, "words", cases(words))
{
    expect(strlen(it.word) == it.len, "word length");
}

/* the body receives a by-value copy; mutating it must not touch the table */
static int mutable_cases[] = { 7, 8, 9 };

it(t_copy, "body copies, array untouched", cases(mutable_cases))
{
    it = 0;
    expect(it == 0 && mutable_cases[0] == 7 && mutable_cases[1] == 8 &&
           mutable_cases[2] == 9,
           "body works on a copy; source array intact");
}

/* enum-valued element type */
typedef enum { CT_E_RED = 1, CT_E_GREEN = 2, CT_E_BLUE = 4 } ct_rgb_t;

static const ct_rgb_t color_cases[] = { CT_E_RED, CT_E_GREEN, CT_E_BLUE };

it(t_enum, "enum cases", cases(color_cases))
{
    expect(it == 1 || it == 2 || it == 4, "a valid color");
}

/* floating-point fields (struct larger than an int, covers sizeof stepping) */
static const struct { double a; double b; double product; } fp_cases[] = {
    { 0.5, 2.0, 1.0 },
    { 1.5, 4.0, 6.0 },
    { -2.25, 4.0, -9.0 },
};

it(t_float, "float cases", cases(fp_cases))
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

it(t_big, "big cases", cases(big_cases))
{
    expect(it.buf[0] == 'a' || it.buf[0] == 'b', "name prefix");
    expect(it.vals[0] + it.vals[1] != 0.0, "vals sum to a non-zero total");
}

/* one aggregate entry: case [1] fails, cases [0] and [2] pass */
typedef struct { int v; } trap_case;

static const trap_case trap_cases[] = { { 1 }, { 2 }, { 3 } };

it(t_trap, "traps on the middle case", cases(trap_cases))
{
    expect(it.v != 2, "case 2 is broken on purpose");
}

/* several failing expects in a single case are all reported under it */
static const struct { int v; } multi_cases[] = { { 0 } };

it(t_multi, "multiple failures in one case", cases(multi_cases))
{
    expect(it.v == 1, "first miss");
    expect(it.v == 2, "second miss");
}

/* .known is test-level: the case-[2] failure makes the aggregate known */
static const struct { int x; } known_cases[] = { { 1 }, { 2 }, { 3 } };

it(t_known_param, "known param bug", .known = "JIRA-99", cases(known_cases))
{
    expect(it.x < 3, "value 3 not implemented");
}

/* .timeout wraps each case: case [2] spins and is aborted, the rest pass */
static const struct { int spin; } slow_cases[] = { { 0 }, { 0 }, { 1 } };

it(t_slow, "per-case timeout", .timeout = 50, cases(slow_cases))
{
    while (it.spin) {}
}

/* .skip gates the whole aggregate */
static const struct { int v; } skip_cases[] = { { 1 }, { 2 } };

it(t_skip_param, "skipped param", .skip = 1, cases(skip_cases))
{
    expect(it.v > 0, "never runs");
}

/* .platforms gates the whole aggregate */
static const struct { int v; } plat_cases[] = { { 1 }, { 2 } };

it(t_linux, "linux-only param", .platforms = "linux", cases(plat_cases))
{
    expect(it.v > 0, "runs on linux");
}

/* .std gates the whole aggregate (skipped: build is c99, not ISO C11) */
static const struct { int v; } std_cases[] = { { 1 }, { 2 } };

it(t_c11, "c11 param", .std = "c11", cases(std_cases))
{
    expect(it.v > 0, "never runs: only runs in an ISO C11 build");
}

/* signal expectations inside a parameterized body */
static const struct { int should_abort; } sig_cases[] = { { 0 }, { 1 } };

it(t_abort_case, "abort per case", cases(sig_cases))
{
    if (it.should_abort)
        ct_expect_abort({ abort(); });
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

it(t_boundary, "boundary 64 cases", cases(boundary_cases))
{
    expect(it.n >= 0 && it.n <= 63, "in bounds");
}

/* long name exercises the registry name allocation */
static const int lc[] = { 1, 2 };

it(t_long_name, "this is an extremely long parameterized test name that overflows the "
   "per-case name buffer used by the registry", cases(lc))
{
    expect(it == 1 || it == 2, "long-name cases");
}

const struct ctest_test *const ctest_suite[] = {
    CT_IT(t_custom_msg),
    CT_IT(t_expect_msg),
    CT_IT(t_multiply),
    CT_IT(t_abort),
    CT_IT(t_segv),
    CT_IT(t_no_signal),
    CT_IT(t_hooks),
    CT_IT(t_flaky),
    CT_IT(t_known),
    CT_IT(t_timeout),
    CT_IT(t_double),
    CT_IT(t_prime),
    CT_IT(t_single),
    CT_IT(t_designated),
    CT_IT(t_words),
    CT_IT(t_copy),
    CT_IT(t_enum),
    CT_IT(t_float),
    CT_IT(t_big),
    CT_IT(t_trap),
    CT_IT(t_multi),
    CT_IT(t_known_param),
    CT_IT(t_slow),
    CT_IT(t_skip_param),
    CT_IT(t_linux),
    CT_IT(t_c11),
    CT_IT(t_abort_case),
    CT_IT(t_boundary),
    CT_IT(t_long_name),
    0
};
