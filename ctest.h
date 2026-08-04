#ifndef CTEST_H
#define CTEST_H

/*
 * ctest is a single-header test framework for GCC, Clang, and MSVC. It uses
 * one non-standard feature deliberately: `__attribute__((section(...)))` (or
 * the MSVC `__pragma(allocate(...))` equivalent) for open-world test
 * registration, in the same spirit as Criterion and other modern C/C++
 * runners. No ISO C99 fallback is provided; the design target is "the big
 * three", not strict portability.
 *
 * Open-world registration: `it("name", ...) { body }` emits a self-describing
 * `static const struct ctest_test` record into a custom linker section
 * (`ct_tst`). The runner enumerates the section at startup via linker-provided
 * sentinels (`__start_ct_tst` / `__stop_ct_tst` on GNU ELF, the
 * `section$start$` / `section$end$` asm labels on Mach-O). There is no
 * registration call, no per-file suite array, and no ID to invent -- tests
 * live next to business logic and are found automatically.
 *
 * Each `it()` runs in its own child process under the `c-test` CLI launcher
 * (fork/exec per test) so crashes and timeouts are isolated and reported. A
 * standalone in-process `--run-all` mode is also available.
 *
 * Lifecycle hooks (ctest_setup, ctest_teardown, ctest_before_each,
 * ctest_after_each) default to no-ops. Override one by `#define`-mapping it to
 * your own function before including this header.
 *
 * The runner needs POSIX APIs (timers, signals), so the feature test level is
 * set below. Define a compatible `_POSIX_C_SOURCE` (or `_GNU_SOURCE`) of your
 * own before including this header to opt out.
 */
#if !(defined(__GNUC__) || defined(__clang__) || defined(_MSC_VER) || defined(__TINYC__))
#error "ctest requires GCC, Clang, MSVC, or TCC"
#endif

#ifndef _MSC_VER
#  ifndef _POSIX_C_SOURCE
#    define _POSIX_C_SOURCE 200809L
#  elif _POSIX_C_SOURCE < 200809L
#    undef _POSIX_C_SOURCE
#    define _POSIX_C_SOURCE 200809L
#  endif
#endif

#define CT_VERSION "0.1.0"

#define CT_MAX_TAGS      16
#define CT_MAX_PLATFORMS  8
#define CT_MAX_FAILURES   8
#define CT_MAX_ENV        8

#define CT_JOIN(a, b) CT_JOIN_(a, b)
#define CT_JOIN_(a, b) a##b
#define CT_NAME(p) CT_JOIN(p, __LINE__)

#if defined(__GNUC__) || defined(__clang__)
#define CT_UNUSED __attribute__((unused))
#else
#define CT_UNUSED
#endif

/*
 * Preprocessor plumbing used by `it` to detect a trailing `cases(...)` argument
 * and auto-emit `(void)` for plain tests. `cases` expands to a parenthesized
 * expression (it starts with `(`), while every other `it` argument (the name
 * string, `.tags`/`.skip`/`.platforms`/`.std` designators, integers) does not,
 * so "the last argument starts with a parenthesis" uniquely identifies the
 * parameterized form. Up to 8 arguments are supported.
 */
#define CT_PROBE() ~, 1,
#define CT_CHECK_N(x, n, ...) n
#define CT_CHECK(...) CT_EXPAND(CT_CHECK_N(__VA_ARGS__, 0,))
#define CT_IS_PAREN_PROBE(...) CT_PROBE()
#define CT_IS_PAREN(x) CT_CHECK(CT_IS_PAREN_PROBE x)

/* CT_EXPAND(x) x — forces a second prescan pass.
 * MSVC's legacy preprocessor treats __VA_ARGS__ as a single token when
 * forwarded to a nested macro call.  Wrapping the call in CT_EXPAND causes
 * MSVC to rescan the result and properly split __VA_ARGS__ commas into
 * separate arguments.  This is a standard C99 no-op on conformant
 * preprocessors (GCC, Clang) and is NOT a compiler-specific extension. */
#define CT_EXPAND(x) x

#define CT_DE1(...) __VA_ARGS__
#define CT_DE(t) CT_DE1 t

#define CT_CAT(a, ...) CT_CAT_I(a, __VA_ARGS__)
#define CT_CAT_I(a, ...) a##__VA_ARGS__

#define CT_NARG(...)   CT_EXPAND(CT_NARG_I(__VA_ARGS__, 8, 7, 6, 5, 4, 3, 2, 1, 0))
#define CT_NARG_I(_1, _2, _3, _4, _5, _6, _7, _8, N, ...) N

#define CT_ARG_1(a) a
#define CT_ARG_2(a, b) b
#define CT_ARG_3(a, b, c) c
#define CT_ARG_4(a, b, c, d) d
#define CT_ARG_5(a, b, c, d, e) e
#define CT_ARG_6(a, b, c, d, e, f) f
#define CT_ARG_7(a, b, c, d, e, f, g) g
#define CT_ARG_8(a, b, c, d, e, f, g, h) h
#define CT_ARG_N(n, ...) CT_EXPAND(CT_ARG_N_I(CT_CAT(CT_ARG_, n), __VA_ARGS__))
#define CT_ARG_N_I(name, ...) CT_EXPAND(name(__VA_ARGS__))

#define CT_DROP_LAST_1(a)
#define CT_DROP_LAST_2(a, b) a
#define CT_DROP_LAST_3(a, b, c) a, b
#define CT_DROP_LAST_4(a, b, c, d) a, b, c
#define CT_DROP_LAST_5(a, b, c, d, e) a, b, c, d
#define CT_DROP_LAST_6(a, b, c, d, e, f) a, b, c, d, e
#define CT_DROP_LAST_7(a, b, c, d, e, f, g) a, b, c, d, e, f
#define CT_DROP_LAST_8(a, b, c, d, e, f, g, h) a, b, c, d, e, f, g
#define CT_DROP_LAST(...) CT_EXPAND(CT_DROP_LAST_I(CT_CAT(CT_DROP_LAST_, CT_NARG(__VA_ARGS__)), __VA_ARGS__))
#define CT_DROP_LAST_I(name, ...) CT_EXPAND(name(__VA_ARGS__))

#define CT_LAST_ARG(...) CT_ARG_N(CT_NARG(__VA_ARGS__), __VA_ARGS__)
#define CT_HAS_CASES(...) CT_IS_PAREN(CT_LAST_ARG(__VA_ARGS__))

/* CT_HAS_GEN: detect generate() vs cases() without using ## on computed
 * values (which breaks MSVC's legacy preprocessor when the value comes
 * from a MACRO paren rescan).
 *
 * Strategy: generate(type, fn, n) expands to ((type, fn, n)) — a double
 * paren.  CT_HAS_GEN_1 uses CT_IS_GEN_PROBE to extract the FIRST element
 * of the last arg and checks whether IT is itself a paren:
 *   cases(int, arr) → last arg = (int, arr)  → first elem = int  → not paren → 0
 *   generate(int,f,n) → last arg = ((int,f,n)) → first elem = (int,f,n) → paren → 1
 *
 * On MSVC's legacy preprocessor the bare `MACRO paren` rescan trick fails
 * when the paren comes from a macro parameter substitution — MSVC doesn't
 * re-invoke the function-like macro after the substitution.  The workaround
 * is to wrap every `MACRO_I paren` call inside CT_EXPAND(...) so that the
 * argument-evaluation pass forces a second scan:
 *   CT_EXPAND(MACRO_I paren)  →  CT_EXPAND evaluates arg  →  MACRO_I( ) called
 * Similarly, CT_IS_PAREN_PROBE must be forced via CT_IS_PAREN(a) (which wraps
 * in CT_CHECK) rather than the bare `CT_IS_PAREN_PROBE a` space-trick. */
#define CT_IS_GEN_PROBE_I(a, ...) CT_IS_PAREN(a)
#define CT_IS_GEN_PROBE(paren) CT_EXPAND(CT_IS_GEN_PROBE_I paren)
#define CT_HAS_GEN(...) CT_HAS_GEN_I(CT_HAS_CASES(__VA_ARGS__), __VA_ARGS__)
#define CT_HAS_GEN_I(c, ...) CT_CAT(CT_HAS_GEN_, c)(__VA_ARGS__)
#define CT_HAS_GEN_0(...) 0
#define CT_HAS_GEN_1(...) CT_IS_GEN_PROBE(CT_LAST_ARG(__VA_ARGS__))

#define CT_SIG_IF(...) CT_SIG_IF_II(CT_HAS_CASES(__VA_ARGS__), __VA_ARGS__)
#define CT_SIG_IF_II(c, ...) CT_CAT(CT_SIG_IF_, c)(__VA_ARGS__)
#define CT_SIG_IF_0(...) (void)
#define CT_SIG_IF_1(...) (CT_NAME(ct_param) it)

#define CT_MAX_PARAM_CASES 64
#define CT_PARAM_N(array) (sizeof(array) / sizeof((array)[0]))

/*
 * Parameterized tests: `cases(type, array)` is passed as the last `it` argument
 * and expands to a parenthesised pair so the trailing-parenthesis probe above
 * still identifies it. `it` uses the explicit `type` to declare a by-value `it`
 * struct for the body — no `__typeof__` needed (MSVC compatible).
 */
/* cases(type, array) — explicit element type avoids __typeof__ (MSVC compat).
 * Expands to (type, array) so the IS_PAREN probe still fires.
 *
 * Extraction: CT_PARAM_TYPE/ARRAY_WRAP(paren) takes the parenthesised pair as
 * a single argument.  After substitution the replacement list becomes
 * CT_CASES_*_I (type, array), which the rescan then recognises as a
 * function-like call — no __VA_ARGS__ comma-splitting tricks needed, works
 * with MSVC's legacy preprocessor as well as GCC and Clang. */
#define cases(type, array) (type, array)

#define CT_CASES_TYPE_I(type, array)  type
#define CT_CASES_ARRAY_I(type, array) array
#define CT_PARAM_TYPE_WRAP(paren)   CT_CASES_TYPE_I  paren
#define CT_PARAM_ARRAY_WRAP(paren)  CT_CASES_ARRAY_I paren
#define CT_PARAM_TYPE(...)   CT_PARAM_TYPE_WRAP(CT_LAST_ARG(__VA_ARGS__))
#define CT_PARAM_ARRAY(...)  CT_PARAM_ARRAY_WRAP(CT_LAST_ARG(__VA_ARGS__))

/* generate(type, fn, n): like cases() but calls fn(void *out) n times at
 * runtime to produce test values. type and fn must be single tokens; n must
 * be a single integer literal or #define'd constant.
 *
 * Expands to ((type, fn, n)) — a double-paren — so CT_HAS_GEN can
 * distinguish it from cases() = (type, array) by checking whether the first
 * element of the last paren is itself a paren. */
#define generate(type, fn, n) ((type, fn, n))

/* Inner-tuple extractor: ((type, fn, n)) → (type, fn, n).
 * CT_EXPAND is required so MSVC's legacy preprocessor rescans the result
 * of the parameter substitution and actually calls CT_GEN_INNER_I. */
#define CT_GEN_INNER_I(inner) inner
#define CT_GEN_INNER(paren)   CT_EXPAND(CT_GEN_INNER_I paren)

#define CT_GEN_TYPE_I(t, fn, n)  t
#define CT_GEN_FN_I(t, fn, n)    fn
#define CT_GEN_COUNT_I(t, fn, n) n
/* Each extractor wraps its inner call in CT_EXPAND so the _I macro is called
 * on the expanded (type, fn, n) tuple, not left as unexpanded tokens. */
#define CT_GEN_TYPE_WRAP(inner)   CT_EXPAND(CT_GEN_TYPE_I  inner)
#define CT_GEN_FN_WRAP(inner)     CT_EXPAND(CT_GEN_FN_I    inner)
#define CT_GEN_COUNT_WRAP(inner)  CT_EXPAND(CT_GEN_COUNT_I inner)
#define CT_GEN_TYPE(paren)   CT_GEN_TYPE_WRAP(CT_GEN_INNER(paren))
#define CT_GEN_FN(paren)     CT_GEN_FN_WRAP(CT_GEN_INNER(paren))
#define CT_GEN_COUNT(paren)  CT_GEN_COUNT_WRAP(CT_GEN_INNER(paren))

#define CT_PARAM_TYPEDEF_0(...)
/* Two-level dispatch: evaluate CT_HAS_GEN as a normal argument (not inside ##)
 * so MSVC's legacy preprocessor fully expands it before the paste step. */
#define CT_PARAM_TYPEDEF_1(...)  CT_PARAM_TYPEDEF_1D(CT_HAS_GEN(__VA_ARGS__), __VA_ARGS__)
#define CT_PARAM_TYPEDEF_1D(g, ...) CT_EXPAND(CT_CAT(CT_PARAM_TYPEDEF_1G_, g)(__VA_ARGS__))
/* cases: compile-time bounds check + typedef */
#define CT_PARAM_TYPEDEF_1G_0(...)                                          \
    typedef char CT_NAME(ct_case_ok)                                        \
        [(CT_PARAM_N(CT_PARAM_ARRAY(__VA_ARGS__)) > 0 &&                    \
          CT_PARAM_N(CT_PARAM_ARRAY(__VA_ARGS__)) <= CT_MAX_PARAM_CASES)    \
             ? 1 : -1];                                                     \
    typedef CT_PARAM_TYPE(__VA_ARGS__) CT_NAME(ct_param);
/* generate: just the typedef; count is a runtime value */
#define CT_PARAM_TYPEDEF_1G_1(...) \
    typedef CT_GEN_TYPE(CT_LAST_ARG(__VA_ARGS__)) CT_NAME(ct_param);
#define CT_PARAM_TYPEDEF(...) CT_PARAM_TYPEDEF_II(CT_HAS_CASES(__VA_ARGS__), __VA_ARGS__)
#define CT_PARAM_TYPEDEF_II(c, ...) CT_CAT(CT_PARAM_TYPEDEF_, c)(__VA_ARGS__)

/* In non-test builds `it` keeps the array alive via a pointer so clang's
 * -Wunused-const-variable does not flag case tables that are only read here. */
#define CT_PARAM_USE_0(...)
#define CT_PARAM_USE_1(...)     CT_PARAM_USE_1D(CT_HAS_GEN(__VA_ARGS__), __VA_ARGS__)
#define CT_PARAM_USE_1D(g, ...) CT_EXPAND(CT_CAT(CT_PARAM_USE_1G_, g)(__VA_ARGS__))
#define CT_PARAM_USE_1G_0(...) static CT_UNUSED const void *                   \
    CT_NAME(ct_use) = (const void *)CT_PARAM_ARRAY(__VA_ARGS__);
#define CT_PARAM_USE_1G_1(...)  /* generate: fn is referenced in struct */
#define CT_PARAM_USE(...) CT_PARAM_USE_II(CT_HAS_CASES(__VA_ARGS__), __VA_ARGS__)
#define CT_PARAM_USE_II(c, ...) CT_CAT(CT_PARAM_USE_, c)(__VA_ARGS__)

#define CT_PARAM_CALL_0(...) CT_NAME(ctest_fn)();
#define CT_PARAM_CALL_1(...) CT_NAME(ctest_fn)(*(CT_NAME(ct_param) *)g_case_ptr);
#define CT_PARAM_CALL(...) CT_PARAM_CALL_II(CT_HAS_CASES(__VA_ARGS__), __VA_ARGS__)
#define CT_PARAM_CALL_II(c, ...) CT_CAT(CT_PARAM_CALL_, c)(__VA_ARGS__)

typedef struct ctest_failure {
    const char *file;
    int line;
    const char *expr;
    const char *msg;
    int case_index;
    const char *case_desc;
} ctest_failure;

typedef enum ctest_status { CT_PASS, CT_FAIL, CT_SKIP, CT_CRASH, CT_TIMEOUT } ctest_status;

/* key/value pair for .env; a NULL key terminates the list; NULL value unsets */
typedef struct { const char *key; const char *value; } ct_env;

/* forward declaration so function pointers inside it_t can reference it */
typedef struct it_t it_t;
struct it_t {
    const char *name;
    int skip;
    int leak;    /* 0 = assert no leak (default), 1 = leaks OK */
    int no_alloc; /* 0 = alloc allowed (default), 1 = assert no allocation */
    int abort;   /* 0 = not expected, 1 = expected */
    int signal;  /* 0 = none, N = expect signal number N */
    void *data;  /* arbitrary user context; passed through to all hooks via it->data */
    int buf;     /* 0 = no buffer; N = allocate N-byte canary-guarded buffer, exposed as ct_buf */
    int retry;   /* retry this test up to N times on failure before marking it failed */
    ct_env env[CT_MAX_ENV]; /* env vars to set before the body; restored after */
    void (*setup)(const it_t *it);    /* called before the test body; it->result is CT_PASS (unset) */
    void (*teardown)(const it_t *it); /* called after the test body; it->result is populated */
    const char *platforms[CT_MAX_PLATFORMS];
    const char *std;
    const char *known;
    int timeout;
    const char *tags[CT_MAX_TAGS];
    const char *depends_on; /* if set: skip this test if the named test failed in this run */
    ctest_status result; /* set before teardown/after_each; CT_PASS (0) when called from setup/before_each */
};

typedef struct ctest_result {
    const char *name;
    const char *file;
    int line;
    ctest_status status;
    const char *skip_reason;
    int fail_count;
    const ctest_failure *failures;
    double seconds;
    const char *captured;
    int flaky;
    const char *known_id;
} ctest_result;

typedef struct ctest_slow { const char *name; double seconds; } ctest_slow;

typedef struct ctest_summary {
    int passed;
    int failed;
    int crashed;
    int timedout;
    int skipped;
    int known;
    double seconds;
    int nslow;
    ctest_slow slow[5];
} ctest_summary;

typedef struct ctest_reporter {
    void (*begin)(int total);
    void (*result)(const ctest_result *r);
    void (*end)(const ctest_summary *s);
} ctest_reporter;

#if defined(CTEST)

/*
 * Test binaries are built as `cc mytests.c -DCTEST` with no other ctest
 * source. This header provides the registry, a machine-readable --list /
 * --run protocol for the `c-test` CLI launcher, and a standalone in-process
 * run-all mode for executing the suite directly.
 *
 * The harness main() must be defined before `main` is renamed so the user's
 * own main (if any) becomes ctest_app_main instead of shadowing ours.
 */

struct ctest_test;

int ctest_run(int argc, char **argv);

int main(int argc, char **argv) {
    return ctest_run(argc, argv);
}

#define main ctest_app_main

/*
 * Open-world section: each `it()` drops a `struct ctest_test` into the
 * `ct_tst` linker section. The runner walks `[CT_SEC_START, CT_SEC_STOP)` at
 * startup. GNU ld and ld64 each provide a sentinel for section bounds:
 *
 *   - GNU ELF: `__start_<section>` / `__stop_<section>` are materialized by
 *     the linker for any section whose name is a valid C identifier, as long
 *     as the symbols are referenced. We reference both, so the section is
 *     retained even under `--gc-sections`.
 *   - Mach-O: equivalent `section$start$__DATA$<section>` /
 *     `section$end$__DATA$<section>` asm-derived labels.
 *   - MSVC PE: no sentinel; use the sub-section alpha-sort trick. Records are
 *     stored as *pointers* in "ct_tst$b"; NULL sentinels sit in "ct_tst$a"
 *     and "ct_tst$z". The linker merges them in ASCII order, so $a < $b < $z.
 */
#if defined(__APPLE__)
#define CT_SECTION_NAME "__DATA,ct_tst"
#elif defined(_MSC_VER)
#pragma section("ct_tst$a", read)
#pragma section("ct_tst$b", read)
#pragma section("ct_tst$z", read)
#define CT_SECTION_NAME "ct_tst$b"
#define CT_SECTION_MSVC 1
#elif defined(__ELF__) || (defined(__GNUC__) && !defined(__APPLE__)) || defined(__TINYC__)
#define CT_SECTION_NAME "ct_tst"
#else
#error "ctest requires GCC/Clang (ELF or Mach-O), MSVC, or TCC."
#endif

#define CT_MAX_FILTERS 8
#define CT_TAGQ_MAX 16

#if defined(_WIN32)
#define CT_OS "windows"
#elif defined(__APPLE__)
#define CT_OS "macos"
#elif defined(__linux__)
#define CT_OS "linux"
#elif defined(__unix__)
#define CT_OS "posix"
#else
#define CT_OS "unknown"
#endif

#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#  include <windows.h>
#  include <fcntl.h>   /* O_BINARY */
#  include <io.h>      /* _isatty, _fileno */
#else
#  include <sys/time.h>
#  include <sys/wait.h>
#  include <unistd.h>
#endif

#ifdef _MSC_VER
#  define strtok_r strtok_s
#  define strdup   _strdup
#endif

/*
 * Optional lifecycle hooks. Define any (or all) of these in your test file
 * to run code around the suite:
 *
 *     void ctest_setup(void)                    once before all tests (per process)
 *     void ctest_teardown(void)                 once after all tests (per process)
 *     void ctest_before_each(const it_t *it)    before each test
 *     void ctest_after_each(const it_t *it)     after each test
 *
 * Because every test runs in its own child process under the c-test runner,
 * setup/teardown run once per child there; in standalone mode they run once
 * per suite.
 *
 * Each hook defaults to a no-op. To run your own, map the hook to a function
 * with a `#define` before including this header:
 *
 *     #define ctest_before_each my_before_each
 *     #include "ctest.h"
 *
 *     void my_before_each(const it_t *it) { ... }
 *
 * ctest.h emits the extern declaration automatically — no forward declaration
 * needed. setup/teardown follow the same pattern but take no arguments.
 *
 * Plain C99, no weak symbols: works on GCC, Clang, and TCC.
 */
#ifndef ctest_setup
#define ctest_setup ct_noop_setup
static void CT_UNUSED ct_noop_setup(void) {}
#endif
#ifndef ctest_teardown
#define ctest_teardown ct_noop_teardown
static void CT_UNUSED ct_noop_teardown(void) {}
#endif
#ifndef ctest_before_each
#define ctest_before_each ct_noop_before_each
static void CT_UNUSED ct_noop_before_each(const it_t *it) { (void)it; }
#else
/* emit the extern so the user doesn't need a forward declaration */
extern void ctest_before_each(const it_t *it);
#endif
#ifndef ctest_after_each
#define ctest_after_each ct_noop_after_each
static void CT_UNUSED ct_noop_after_each(const it_t *it) { (void)it; }
#else
extern void ctest_after_each(const it_t *it);
#endif

#if defined(__linux__)
#include <sys/prctl.h>
#endif

/*
 * Registration is open-world: drop `it("name", ...) { body }` anywhere at file
 * scope. There is no per-file suite array, no ID, and no central list to
 * maintain -- the record is placed in the `ct_tst` linker section and found by
 * the runner at startup.
 *
 * Plain tests take no signature; `it` emits `(void)` automatically:
 *
 *     it("does something") { ... }
 *
 * `cases(array)` is passed as the last `it` argument. `it` derives the case
 * type from the array's element type and declares a by-value `it` struct for
 * the body:
 *
 *     static const struct { int a, b; } mult_cases[] = { {2, 3}, {3, 4} };
 *     it("multiply", cases(mult_cases)) {
 *         expect(it.a * it.b == 6, "multiplication");
 *     }
 *
 * `cases` may be combined with the other `it` options and must be the last
 * argument: it("name", .tags = {"math"}, cases(array)).
 *
 * Each `it` expands on a single source line, so two `it()`s on the same line
 * collide. Keep one per line -- the natural style anyway.
 */
/* In C++, aggregate initializers cannot mix positional and designated fields
 * (even in C++20 where designated initializers are standard). GCC/Clang allow
 * the mix as an extension; MSVC does not. To stay portable, in C++ mode we
 * emit the first field (name) as a designated initializer too. */
#ifdef __cplusplus
#  define CT_SPEC_FIELDS_I(ct_n_, ...) .name = ct_n_, __VA_ARGS__
#  define CT_SPEC_FIELDS(...) CT_EXPAND(CT_SPEC_FIELDS_I(__VA_ARGS__))
#  define CT_SPEC_VALUE_0(...) { CT_EXPAND(CT_SPEC_FIELDS_I(__VA_ARGS__)) }
#  define CT_SPEC_VALUE_1(...) { CT_EXPAND(CT_SPEC_FIELDS_I(CT_DROP_LAST(__VA_ARGS__))) }
#else
#  define CT_SPEC_VALUE_0(...) { __VA_ARGS__ }
#  define CT_SPEC_VALUE_1(...) { CT_DROP_LAST(__VA_ARGS__) }
#endif
#define CT_SPEC_VALUE(...) CT_SPEC_VALUE_II(CT_HAS_CASES(__VA_ARGS__), __VA_ARGS__)
#define CT_SPEC_VALUE_II(c, ...) CT_CAT(CT_SPEC_VALUE_, c)(__VA_ARGS__)

#define CT_PARAM_DATA_0(...) NULL
#define CT_PARAM_DATA_1(...)     CT_PARAM_DATA_1D(CT_HAS_GEN(__VA_ARGS__), __VA_ARGS__)
#define CT_PARAM_DATA_1D(g, ...) CT_EXPAND(CT_CAT(CT_PARAM_DATA_1G_, g)(__VA_ARGS__))
#define CT_PARAM_DATA_1G_0(...) (const void *)CT_PARAM_ARRAY(__VA_ARGS__)
#define CT_PARAM_DATA_1G_1(...) NULL
#define CT_PARAM_DATA(...) CT_PARAM_DATA_II(CT_HAS_CASES(__VA_ARGS__), __VA_ARGS__)
#define CT_PARAM_DATA_II(c, ...) CT_CAT(CT_PARAM_DATA_, c)(__VA_ARGS__)

#define CT_PARAM_ELEM_0(...) 0
#define CT_PARAM_ELEM_1(...)     CT_PARAM_ELEM_1D(CT_HAS_GEN(__VA_ARGS__), __VA_ARGS__)
#define CT_PARAM_ELEM_1D(g, ...) CT_EXPAND(CT_CAT(CT_PARAM_ELEM_1G_, g)(__VA_ARGS__))
#define CT_PARAM_ELEM_1G_0(...) sizeof((CT_PARAM_ARRAY(__VA_ARGS__))[0])
#define CT_PARAM_ELEM_1G_1(...) sizeof(CT_GEN_TYPE(CT_LAST_ARG(__VA_ARGS__)))
#define CT_PARAM_ELEM(...) CT_PARAM_ELEM_II(CT_HAS_CASES(__VA_ARGS__), __VA_ARGS__)
#define CT_PARAM_ELEM_II(c, ...) CT_CAT(CT_PARAM_ELEM_, c)(__VA_ARGS__)

#define CT_PARAM_COUNT_0(...) 0
#define CT_PARAM_COUNT_1(...)     CT_PARAM_COUNT_1D(CT_HAS_GEN(__VA_ARGS__), __VA_ARGS__)
#define CT_PARAM_COUNT_1D(g, ...) CT_EXPAND(CT_CAT(CT_PARAM_COUNT_1G_, g)(__VA_ARGS__))
#define CT_PARAM_COUNT_1G_0(...) CT_PARAM_N(CT_PARAM_ARRAY(__VA_ARGS__))
#define CT_PARAM_COUNT_1G_1(...) 0
#define CT_PARAM_COUNT(...) CT_PARAM_COUNT_II(CT_HAS_CASES(__VA_ARGS__), __VA_ARGS__)
#define CT_PARAM_COUNT_II(c, ...) CT_CAT(CT_PARAM_COUNT_, c)(__VA_ARGS__)

/* Generator function pointer: non-NULL only for generate() tests. */
#define CT_PARAM_GEN_0(...) NULL
#define CT_PARAM_GEN_1(...)     CT_PARAM_GEN_1D(CT_HAS_GEN(__VA_ARGS__), __VA_ARGS__)
#define CT_PARAM_GEN_1D(g, ...) CT_EXPAND(CT_CAT(CT_PARAM_GEN_1G_, g)(__VA_ARGS__))
#define CT_PARAM_GEN_1G_0(...) NULL
#define CT_PARAM_GEN_1G_1(...) \
    (void (*)(void *))CT_GEN_FN(CT_LAST_ARG(__VA_ARGS__))
#define CT_PARAM_GEN(...) CT_PARAM_GEN_II(CT_HAS_CASES(__VA_ARGS__), __VA_ARGS__)
#define CT_PARAM_GEN_II(c, ...) CT_CAT(CT_PARAM_GEN_, c)(__VA_ARGS__)
/* Generate count (only meaningful for generate()): */
#define CT_PARAM_GCOUNT_0(...) 0
#define CT_PARAM_GCOUNT_1(...)     CT_PARAM_GCOUNT_1D(CT_HAS_GEN(__VA_ARGS__), __VA_ARGS__)
#define CT_PARAM_GCOUNT_1D(g, ...) CT_EXPAND(CT_CAT(CT_PARAM_GCOUNT_1G_, g)(__VA_ARGS__))
#define CT_PARAM_GCOUNT_1G_0(...) 0
#define CT_PARAM_GCOUNT_1G_1(...) \
    (size_t)(CT_GEN_COUNT(CT_LAST_ARG(__VA_ARGS__)))
#define CT_PARAM_GCOUNT(...) CT_PARAM_GCOUNT_II(CT_HAS_CASES(__VA_ARGS__), __VA_ARGS__)
#define CT_PARAM_GCOUNT_II(c, ...) CT_CAT(CT_PARAM_GCOUNT_, c)(__VA_ARGS__)

/*
 * The `it` macro relies on a prototype-less forward declaration and on
 * partial struct initializers, which trip clang-only warnings
 * (-Wstrict-prototypes, -Wdeprecated-non-prototype) and the shared
 * -Wmissing-field-initializers at every call site. Suppress them for the
 * duration of the expansion only, so callers keep their own diagnostics.
 */
#if defined(__clang__)
#define CT_DIAG_PUSH                                                         \
    _Pragma("clang diagnostic push")                                         \
    _Pragma("clang diagnostic ignored \"-Wstrict-prototypes\"")              \
    _Pragma("clang diagnostic ignored \"-Wdeprecated-non-prototype\"")       \
    _Pragma("clang diagnostic ignored \"-Wmissing-field-initializers\"")
#define CT_DIAG_POP _Pragma("clang diagnostic pop")
#elif defined(__TINYC__)
/* TCC claims __GNUC__ but does not support _Pragma — suppress nothing. */
#define CT_DIAG_PUSH
#define CT_DIAG_POP
#elif defined(__GNUC__)
#define CT_DIAG_PUSH                                                         \
    _Pragma("GCC diagnostic push")                                           \
    _Pragma("GCC diagnostic ignored \"-Wmissing-field-initializers\"")
#define CT_DIAG_POP _Pragma("GCC diagnostic pop")
#elif defined(_MSC_VER)
/* C4204: non-constant aggregate initializer (designated init of it_t.tags) */
#define CT_DIAG_PUSH __pragma(warning(push)) __pragma(warning(disable:4204))
#define CT_DIAG_POP  __pragma(warning(pop))
#else
#define CT_DIAG_PUSH
#define CT_DIAG_POP
#endif

/*
 * CT_REGISTER places the ctest_test record into the section. On all platforms
 * the struct itself lives in normal static storage and a *pointer* to it is
 * placed in the section. The walker dereferences each pointer.
 *
 * This avoids struct-alignment issues: the section stride is always
 * sizeof(pointer), regardless of how large or oddly-padded ctest_test is.
 *
 * MSVC PE: sub-section alpha-sort trick. NULL sentinels in $a/$z.
 * ELF:     __start_ct_tst / __stop_ct_tst sentinels from GNU ld.
 * Mach-O:  section$start / section$end from ld64.
 */
#ifdef CT_SECTION_MSVC
#define CT_REGISTER(...)                                                     \
    static const struct ctest_test CT_NAME(ct_rec) = {                      \
        CT_SPEC_VALUE(__VA_ARGS__),                                          \
        __FILE__, __LINE__,                                                  \
        CT_NAME(ct_run),                                                     \
        CT_PARAM_DATA(__VA_ARGS__), CT_PARAM_ELEM(__VA_ARGS__),              \
        CT_PARAM_COUNT(__VA_ARGS__),                                         \
        CT_PARAM_GEN(__VA_ARGS__), CT_PARAM_GCOUNT(__VA_ARGS__)              \
    };                                                                       \
    __declspec(allocate("ct_tst$b")) static const struct ctest_test *        \
        CT_NAME(ct_ptr) = &CT_NAME(ct_rec);
#elif defined(__TINYC__)
/* TCC: push via constructor (no __start/__stop section symbols in TCC linker) */
#define CT_REGISTER(...)                                                     \
    static const struct ctest_test CT_NAME(ct_rec) = {                      \
        CT_SPEC_VALUE(__VA_ARGS__),                                          \
        __FILE__, __LINE__,                                                  \
        CT_NAME(ct_run),                                                     \
        CT_PARAM_DATA(__VA_ARGS__), CT_PARAM_ELEM(__VA_ARGS__),              \
        CT_PARAM_COUNT(__VA_ARGS__),                                         \
        CT_PARAM_GEN(__VA_ARGS__), CT_PARAM_GCOUNT(__VA_ARGS__)              \
    };                                                                       \
    __attribute__((constructor))                                             \
    static void CT_NAME(ct_ctor)(void) { ct_tcc_push(&CT_NAME(ct_rec)); }
#else
#define CT_REGISTER(...)                                                     \
    static const struct ctest_test CT_NAME(ct_rec) = {                      \
        CT_SPEC_VALUE(__VA_ARGS__),                                          \
        __FILE__, __LINE__,                                                  \
        CT_NAME(ct_run),                                                     \
        CT_PARAM_DATA(__VA_ARGS__), CT_PARAM_ELEM(__VA_ARGS__),              \
        CT_PARAM_COUNT(__VA_ARGS__),                                         \
        CT_PARAM_GEN(__VA_ARGS__), CT_PARAM_GCOUNT(__VA_ARGS__)              \
    };                                                                       \
    __attribute__((used, section(CT_SECTION_NAME)))                          \
    static const struct ctest_test * const CT_NAME(ct_ptr) =                 \
        &CT_NAME(ct_rec);
#endif

#define it(...) CT_DIAG_PUSH                                                 \
    CT_PARAM_TYPEDEF(__VA_ARGS__)                                            \
    static void CT_NAME(ctest_fn) CT_SIG_IF(__VA_ARGS__);                   \
    static void CT_NAME(ct_run)(void) {                                      \
        CT_PARAM_CALL(__VA_ARGS__)                                          \
    }                                                                        \
    CT_REGISTER(__VA_ARGS__)                                                 \
    CT_DIAG_POP                                                              \
    static void CT_NAME(ctest_fn) CT_SIG_IF(__VA_ARGS__)

typedef struct ctest_test {
    it_t spec;
    const char *file;
    int line;
    void (*fn)(void);
    const void *cases_data;
    size_t cases_elem;
    size_t cases_count;
    void (*cases_gen)(void *out);  /* non-NULL for generate() tests */
    size_t cases_gen_count;        /* number of cases to generate */
} ctest_test;

/*
 * Linker-provided section sentinels. Declared here (after `struct ctest_test`
 * is complete) so the array-of-record externs have a complete element type.
 * `it()` only needs `CT_SECTION_NAME` (a string, defined above); the runner
 * needs these to walk [CT_SEC_START, CT_SEC_STOP).
 */
#if defined(__APPLE__)
extern const struct ctest_test *section$start$__DATA$ct_tst;
extern const struct ctest_test *section$end$__DATA$ct_tst;
#define CT_SEC_PTR_START (&section$start$__DATA$ct_tst)
#define CT_SEC_PTR_STOP  (&section$end$__DATA$ct_tst)
#elif defined(CT_SECTION_MSVC)
/* Sentinel pointers at sub-section boundaries; the walker reads [start+1, stop). */
__declspec(allocate("ct_tst$a")) static const struct ctest_test *ct_msvc_sec_start = NULL;
__declspec(allocate("ct_tst$z")) static const struct ctest_test *ct_msvc_sec_stop  = NULL;
#elif defined(__TINYC__)
/* TCC's built-in linker does not emit __start_X/__stop_X section symbols.
 * Use constructor functions to push each test record into a static list. */
#  define CT_TCC_MAX_TESTS 1024
static const struct ctest_test *ct_tcc_list[CT_TCC_MAX_TESTS];
static size_t ct_tcc_nlist;
static void ct_tcc_push(const struct ctest_test *t) {
    if (ct_tcc_nlist < CT_TCC_MAX_TESTS) ct_tcc_list[ct_tcc_nlist++] = t;
}
#define CT_SEC_PTR_START ((const struct ctest_test * const *)ct_tcc_list)
#define CT_SEC_PTR_STOP  ((const struct ctest_test * const *)(ct_tcc_list + ct_tcc_nlist))
#elif defined(__ELF__) || (defined(__GNUC__) && !defined(__APPLE__))
extern const struct ctest_test * const __start_ct_tst[];
extern const struct ctest_test * const __stop_ct_tst[];
#define CT_SEC_PTR_START (__start_ct_tst)
#define CT_SEC_PTR_STOP  (__stop_ct_tst)
#endif

static const struct ctest_test **g_tests;
static size_t g_count;
static const void *g_case_ptr;
static unsigned g_seed = 12345; /* seed for generate() — overridden by --seed */

static ctest_failure g_failures[CT_MAX_FAILURES];
static int g_fail_count;
static int g_cur_case = -1;
static const char *g_cur_case_desc;

static char **ct_dep_failed;
static int ct_dep_failed_n;

static int ct_dep_has_failed(const char *name) {
    if (!name) return 0;
    for (int i = 0; i < ct_dep_failed_n; i++)
        if (strcmp(ct_dep_failed[i], name) == 0) return 1;
    return 0;
}

static void ct_dep_note_failed(const char *name) {
    char **next;
    if (!name || ct_dep_has_failed(name)) return;
    next = (char **)realloc(ct_dep_failed,
                            (size_t)(ct_dep_failed_n + 1) * sizeof *ct_dep_failed);
    if (!next) return;
    ct_dep_failed = next;
    ct_dep_failed[ct_dep_failed_n++] = strdup(name);
}

static void ct_dep_clear_failed(void) {
    for (int i = 0; i < ct_dep_failed_n; i++) free(ct_dep_failed[i]);
    free(ct_dep_failed);
    ct_dep_failed = NULL;
    ct_dep_failed_n = 0;
}

void ctest_expect(int ok, const char *file, int line, const char *expr,
                  const char *msg) {
    if (ok) return;
    if (g_fail_count < CT_MAX_FAILURES) {
        ctest_failure *f = &g_failures[g_fail_count];
        f->file = file;
        f->line = line;
        f->expr = expr;
        f->msg = msg;
        f->case_index = g_cur_case;
        f->case_desc = g_cur_case_desc;
    }
    g_fail_count++;
}

#define CT_EXPECT_SELECT(_1, _2, NAME, ...) NAME
#define CT_EXPECT_1(expr) ctest_expect((expr) != 0, __FILE__, __LINE__, #expr, NULL)
#define CT_EXPECT_2(expr, msg) ctest_expect((expr) != 0, __FILE__, __LINE__, #expr, msg)
#define expect(...) CT_EXPECT_SELECT(__VA_ARGS__, CT_EXPECT_2, CT_EXPECT_1, 0)(__VA_ARGS__)

#ifndef _WIN32
static const char *ct_signal_explain(int sig, int st) CT_UNUSED;
static const char *ct_signal_explain(int sig, int st) {
    static char buf[128];
    if (WIFSIGNALED(st)) {
        snprintf(buf, sizeof buf, "expected %s, got %s",
                 strsignal(sig), strsignal(WTERMSIG(st)));
    } else if (WIFEXITED(st)) {
        snprintf(buf, sizeof buf, "expected %s, got exit status %d",
                 strsignal(sig), WEXITSTATUS(st));
    } else {
        snprintf(buf, sizeof buf, "expected %s, process did not terminate",
                 strsignal(sig));
    }
    return buf;
}

#define CT_HAS_CRASH_DETECT 1

#elif defined(_MSC_VER)
/*
 * Windows / MSVC: hardware faults map to Win32 exceptions; abort() is
 * bridged via a SIGABRT → RaiseException shim.
 */
#define CT_ABORT_EXCEPTION 0xE0435400UL

static void ct_abort_to_seh(int s) {
    (void)s;
    RaiseException(CT_ABORT_EXCEPTION, 0, 0, NULL);
}

static int ct_seh_filter(unsigned int code, int sig) CT_UNUSED;
static int ct_seh_filter(unsigned int code, int sig) {
    unsigned int want;
    switch (sig) {
        case SIGABRT: want = CT_ABORT_EXCEPTION;           break;
        case SIGSEGV: want = EXCEPTION_ACCESS_VIOLATION;   break;
        case SIGFPE:  want = EXCEPTION_INT_DIVIDE_BY_ZERO; break;
        case SIGILL:  want = EXCEPTION_ILLEGAL_INSTRUCTION;break;
        default:      return EXCEPTION_CONTINUE_SEARCH;
    }
    return (code == want) ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}

#define CT_HAS_CRASH_DETECT 1
#endif /* crash detection helpers */

/* ── expect_no_leak ────────────────────────────────────────────────────────
 * Asserts that no heap memory is net-allocated inside the block.
 *
 * Tracks a per-call allocation balance using malloc/free interception.
 * Platform coverage:
 *   Linux/macOS (CTEST builds) — intercepted via dlsym(RTLD_NEXT)
 *   MSVC debug                 — _CrtMemDifference
 *   Everything else            — no-op (block still executes)
 *
 * Usage:
 *   expect_no_leak({ char *p = malloc(8); free(p); })   // passes
 *   expect_no_leak({ char *p = malloc(8); (void)p; })   // fails
 */

#if (defined(__linux__) || defined(__APPLE__)) && !defined(_WIN32) && defined(CTEST)
#  include <dlfcn.h>
#  include <string.h>  /* memset */
#  ifdef __APPLE__
#    include <malloc/malloc.h>   /* malloc_size on macOS */
#    define ct_usable_size(p) malloc_size(p)
#  else
#    include <malloc.h>          /* malloc_usable_size on Linux */
#    define ct_usable_size(p) malloc_usable_size(p)
#  endif

/* Live allocation balance (bytes) and call count; only DELTAs matter. */
static long ct_alloc_live;
static long ct_alloc_calls;

/* Pointers to the real allocator functions. */
static void *(*ct_r_malloc)(size_t)            = NULL;
static void (*ct_r_free)(void *)               = NULL;
static void *(*ct_r_calloc)(size_t, size_t)    = NULL;
static void *(*ct_r_realloc)(void *, size_t)   = NULL;

/* Bootstrap buffer used before dlsym initialises ct_r_calloc.
 * dlsym itself may call calloc internally on first use. */
static char   ct_boot_buf[65536];
static size_t ct_boot_off;

static void __attribute__((constructor)) ct_alloc_init_real(void) {
    ct_r_malloc  = (void *(*)(size_t))          dlsym(RTLD_NEXT, "malloc");
    ct_r_free    = (void (*)(void *))           dlsym(RTLD_NEXT, "free");
    ct_r_calloc  = (void *(*)(size_t, size_t))  dlsym(RTLD_NEXT, "calloc");
    ct_r_realloc = (void *(*)(void *, size_t))  dlsym(RTLD_NEXT, "realloc");
}

static int ct_is_boot(const void *p) {
    const char *c = (const char *)p;
    return c >= ct_boot_buf && c < ct_boot_buf + sizeof ct_boot_buf;
}

void *malloc(size_t n) {
    if (!ct_r_malloc) {
        if (ct_boot_off + n + 16 <= sizeof ct_boot_buf) {
            void *p = ct_boot_buf + ct_boot_off;
            ct_boot_off = (ct_boot_off + n + 15u) & ~15u;
            return p;
        }
        return NULL;
    }
    void *p = ct_r_malloc(n);
    if (p) { ct_alloc_live += (long)ct_usable_size(p); ct_alloc_calls++; }
    return p;
}

void free(void *p) {
    if (!p || ct_is_boot(p)) return;
    if (ct_r_free) ct_alloc_live -= (long)ct_usable_size(p);
    if (ct_r_free) ct_r_free(p);
}

void *calloc(size_t n, size_t s) {
    if (!ct_r_calloc) {
        size_t total = n * s;
        void *p = malloc(total);
        if (p) memset(p, 0, total);
        return p;
    }
    void *p = ct_r_calloc(n, s);
    if (p) { ct_alloc_live += (long)ct_usable_size(p); ct_alloc_calls++; }
    return p;
}

void *realloc(void *old, size_t n) {
    if (!old) return malloc(n);
    if (ct_is_boot(old)) {
        void *p = malloc(n);
        if (p) memcpy(p, old, n); /* conservative: old size unknown */
        return p;
    }
    if (!ct_r_realloc) return NULL;
    long old_sz = (long)ct_usable_size(old);
    void *p = ct_r_realloc(old, n);
    if (p) { ct_alloc_live -= old_sz; ct_alloc_live += (long)ct_usable_size(p); }
    return p;
}

#  define CT_HAS_HEAP_TRACK 1

#elif defined(_MSC_VER) && defined(_DEBUG) && defined(CTEST)
#  define CT_HAS_HEAP_TRACK 1

#endif /* heap tracking */

#define CT_CANARY_N  16
#define CT_CANARY_B  ((unsigned char)0xAB)

/*
 * ct_buf — framework-managed canary-guarded buffer for .buf = N tests.
 * Set to a valid N-byte region before the test body, NULL otherwise.
 * Canary bytes bracket it; overruns and underruns are detected after fn().
 */
static unsigned char *ct_buf = NULL;
static unsigned char *ct_buf_guard = NULL;  /* full allocation including canaries */
static size_t         ct_buf_size  = 0;

static void ct_buf_setup(int n) {
    if (n <= 0) return;
    unsigned char *g = (unsigned char *)malloc(CT_CANARY_N + (size_t)n + CT_CANARY_N);
    if (!g) return;
    memset(g,                          CT_CANARY_B, CT_CANARY_N);
    memset(g + CT_CANARY_N,            0,           (size_t)n);
    memset(g + CT_CANARY_N + (size_t)n, CT_CANARY_B, CT_CANARY_N);
    ct_buf_guard = g;
    ct_buf       = g + CT_CANARY_N;
    ct_buf_size  = (size_t)n;
}

static void ct_buf_check(const ctest_test *t) {
    if (!ct_buf_guard) return;
    int under = 0, over = 0;
    for (int i = 0; i < CT_CANARY_N; i++)
        if (ct_buf_guard[i] != CT_CANARY_B) { under = 1; break; }
    for (int i = 0; i < CT_CANARY_N; i++)
        if (ct_buf_guard[CT_CANARY_N + ct_buf_size + (size_t)i] != CT_CANARY_B)
            { over = 1; break; }
    if (under)
        ctest_expect(0, t->file, t->line, ".buf",
                     "buffer underrun: write before start of .buf");
    if (over)
        ctest_expect(0, t->file, t->line, ".buf",
                     "buffer overrun: write past end of .buf");
    free(ct_buf_guard);
    ct_buf_guard = NULL;
    ct_buf       = NULL;
    ct_buf_size  = 0;
}

static void ct_buf_free(void) {
    free(ct_buf_guard);
    ct_buf_guard = NULL;
    ct_buf       = NULL;
    ct_buf_size  = 0;
}

/* ── .env: save/restore environment variables around a test body ──────── */
static char *ct_env_saved[CT_MAX_ENV];
static int   ct_env_n_saved;

static void ct_env_push(const ct_env *env) {
    ct_env_n_saved = 0;
    for (int i = 0; i < CT_MAX_ENV && env[i].key; i++) {
        const char *old = getenv(env[i].key);
        ct_env_saved[i] = old ? strdup(old) : NULL;
        ct_env_n_saved++;
#ifndef _WIN32
        if (env[i].value) setenv(env[i].key, env[i].value, 1);
        else              unsetenv(env[i].key);
#else
        _putenv_s(env[i].key, env[i].value ? env[i].value : "");
#endif
    }
}

static void ct_env_pop(const ct_env *env) {
    for (int i = ct_env_n_saved - 1; i >= 0; i--) {
#ifndef _WIN32
        if (ct_env_saved[i]) setenv(env[i].key, ct_env_saved[i], 1);
        else                  unsetenv(env[i].key);
#else
        _putenv_s(env[i].key, ct_env_saved[i] ? ct_env_saved[i] : "");
#endif
        free(ct_env_saved[i]);
        ct_env_saved[i] = NULL;
    }
    ct_env_n_saved = 0;
}

#ifdef _WIN32
static int ct_cap_saved_fd = -1;
static int ct_cap_pipe_fds[2] = { -1, -1 };
static char ct_cap_buf[65536];

static void ct_capture_stdout(void) CT_UNUSED;
static void ct_capture_stdout(void) {
    if (ct_cap_saved_fd >= 0) return;
    if (_pipe(ct_cap_pipe_fds, (unsigned)sizeof ct_cap_buf, O_BINARY) != 0) return;
    ct_cap_saved_fd = _dup(_fileno(stdout));
    if (ct_cap_saved_fd < 0) {
        _close(ct_cap_pipe_fds[0]);
        _close(ct_cap_pipe_fds[1]);
        ct_cap_pipe_fds[0] = ct_cap_pipe_fds[1] = -1;
        return;
    }
    fflush(stdout);
    setvbuf(stdout, NULL, _IONBF, 0);
    if (_dup2(ct_cap_pipe_fds[1], _fileno(stdout)) != 0) {
        _close(ct_cap_saved_fd);
        ct_cap_saved_fd = -1;
        _close(ct_cap_pipe_fds[0]);
        _close(ct_cap_pipe_fds[1]);
        ct_cap_pipe_fds[0] = ct_cap_pipe_fds[1] = -1;
        return;
    }
    _close(ct_cap_pipe_fds[1]);
    ct_cap_pipe_fds[1] = -1;
}

static const char *ct_captured(void) CT_UNUSED;
static const char *ct_captured(void) {
    int n;
    if (ct_cap_saved_fd < 0) return "";
    fflush(stdout);
    _dup2(ct_cap_saved_fd, _fileno(stdout));
    _close(ct_cap_saved_fd);
    ct_cap_saved_fd = -1;
    n = _read(ct_cap_pipe_fds[0], ct_cap_buf, (unsigned)sizeof(ct_cap_buf) - 1);
    if (n < 0) n = 0;
    ct_cap_buf[n] = '\0';
    _close(ct_cap_pipe_fds[0]);
    ct_cap_pipe_fds[0] = -1;
    return ct_cap_buf;
}
#else
static int ct_cap_saved_fd = -1;
static int ct_cap_pipe_fds[2] = { -1, -1 };
static char ct_cap_buf[65536];

static void ct_capture_stdout(void) CT_UNUSED;
static void ct_capture_stdout(void) {
    if (ct_cap_saved_fd >= 0) return;
    if (pipe(ct_cap_pipe_fds) != 0) return;
    ct_cap_saved_fd = dup(STDOUT_FILENO);
    if (ct_cap_saved_fd < 0) {
        close(ct_cap_pipe_fds[0]);
        close(ct_cap_pipe_fds[1]);
        ct_cap_pipe_fds[0] = ct_cap_pipe_fds[1] = -1;
        return;
    }
    fflush(stdout);
    setvbuf(stdout, NULL, _IONBF, 0);
    if (dup2(ct_cap_pipe_fds[1], STDOUT_FILENO) < 0) {
        close(ct_cap_saved_fd);
        ct_cap_saved_fd = -1;
        close(ct_cap_pipe_fds[0]);
        close(ct_cap_pipe_fds[1]);
        ct_cap_pipe_fds[0] = ct_cap_pipe_fds[1] = -1;
        return;
    }
    close(ct_cap_pipe_fds[1]);
    ct_cap_pipe_fds[1] = -1;
}

static const char *ct_captured(void) CT_UNUSED;
static const char *ct_captured(void) {
    ssize_t n;
    if (ct_cap_saved_fd < 0) return "";
    fflush(stdout);
    dup2(ct_cap_saved_fd, STDOUT_FILENO);
    close(ct_cap_saved_fd);
    ct_cap_saved_fd = -1;
    n = read(ct_cap_pipe_fds[0], ct_cap_buf, sizeof(ct_cap_buf) - 1);
    if (n < 0) n = 0;
    ct_cap_buf[n] = '\0';
    close(ct_cap_pipe_fds[0]);
    ct_cap_pipe_fds[0] = -1;
    return ct_cap_buf;
}
#endif

static void ct_restore_stdout_if_capturing(void) {
#ifdef _WIN32
    if (ct_cap_saved_fd >= 0) {
        _dup2(ct_cap_saved_fd, _fileno(stdout));
        _close(ct_cap_saved_fd);
        ct_cap_saved_fd = -1;
        if (ct_cap_pipe_fds[0] >= 0) {
            _close(ct_cap_pipe_fds[0]);
            ct_cap_pipe_fds[0] = -1;
        }
    }
#else
    if (ct_cap_saved_fd >= 0) {
        dup2(ct_cap_saved_fd, STDOUT_FILENO);
        close(ct_cap_saved_fd);
        ct_cap_saved_fd = -1;
        if (ct_cap_pipe_fds[0] >= 0) {
            close(ct_cap_pipe_fds[0]);
            ct_cap_pipe_fds[0] = -1;
        }
    }
#endif
}

static int ct_stristr(const char *hay, const char *needle) {
    size_t nh = strlen(hay), nn = strlen(needle);
    if (nn == 0) return 1;
    if (nn > nh) return 0;
    for (size_t i = 0; i + nn <= nh; i++) {
        int m = 1;
        for (size_t j = 0; j < nn; j++) {
            char a = hay[i + j], b = needle[j];
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) { m = 0; break; }
        }
        if (m) return 1;
    }
    return 0;
}

static int ct_has_tag(const it_t *spec, const char *tag) {
    for (int i = 0; i < CT_MAX_TAGS && spec->tags[i]; i++)
        if (strcmp(spec->tags[i], tag) == 0) return 1;
    return 0;
}

typedef struct ct_tagq_term {
    const char *tag;
    int neg;
} ct_tagq_term;

typedef struct ct_tagq_group {
    ct_tagq_term terms[CT_TAGQ_MAX];
    int n;
} ct_tagq_group;

typedef struct ct_tagq {
    ct_tagq_group groups[CT_TAGQ_MAX];
    int n;
} ct_tagq;

static int ct_tagq_parse(ct_tagq *q, const char *s) {
    memset(q, 0, sizeof *q);
    const char *p = s;
    for (;;) {
        while (*p == ' ') p++;
        if (!*p) break;
        if (q->n >= CT_TAGQ_MAX) return -1;
        ct_tagq_group *g = &q->groups[q->n];
        g->n = 0;
        while (*p) {
            while (*p == ' ') p++;
            if (!*p || *p == ',') break;
            if (g->n >= CT_TAGQ_MAX) return -1;
            ct_tagq_term *t = &g->terms[g->n];
            t->neg = 0;
            while (*p == '!' || *p == '-') { t->neg = !t->neg; p++; }
            const char *st = p;
            while (*p && *p != '+' && *p != ',' && *p != ' ') p++;
            if (p == st) return -1;
            size_t len = p - st;
            char *buf = (char *)malloc(len + 1);
            memcpy(buf, st, len);
            buf[len] = '\0';
            t->tag = buf;
            g->n++;
            while (*p == ' ') p++;
            if (*p == '+') { p++; continue; }
        }
        q->n++;
        while (*p == ' ' || *p == ',') p++;
    }
    return 0;
}

static int ct_tagq_match(const ct_tagq *q, const it_t *spec) {
    if (q->n == 0) return 1;
    for (int g = 0; g < q->n; g++) {
        int ok = 1;
        const ct_tagq_group *grp = &q->groups[g];
        for (int i = 0; i < grp->n; i++) {
            int has = ct_has_tag(spec, grp->terms[i].tag);
            if (grp->terms[i].neg) has = !has;
            if (!has) { ok = 0; break; }
        }
        if (ok) return 1;
    }
    return 0;
}

static int ct_platform_ok(const char * const *plats) {
    if (!plats[0]) return 1;
    for (int i = 0; i < CT_MAX_PLATFORMS && plats[i]; i++)
        if (strstr(CT_OS, plats[i]) || strstr(plats[i], CT_OS))
            return 1;
    return 0;
}

static long ct_have_std(void) {
#if defined(__STDC_VERSION__)
    return __STDC_VERSION__;
#else
    return 0;
#endif
}

static int ct_have_gnu(void) {
#if defined(__STRICT_ANSI__)
    return 0;
#else
    return 1;
#endif
}

static int ct_std_ok(const char *req) {
    static const struct { const char *name; long ver; } tab[] = {
        { "c89", 0 }, { "c99", 199901L }, { "c11", 201112L },
        { "c17", 201710L }, { "c23", 202311L },
    };
    if (!req) return 1;
    int gnu = req[0] == 'g' && req[1] == 'n' && req[2] == 'u';
    const char *base = gnu ? req + 3 : req;
    char key[8];
    if (gnu) {
        snprintf(key, sizeof key, "c%s", base);
        base = key;
    }
    long need = -1;
    for (size_t i = 0; i < sizeof tab / sizeof tab[0]; i++)
        if (strcmp(base, tab[i].name) == 0) { need = tab[i].ver; break; }
    if (need < 0) return 1;
    return ct_have_std() == need && ct_have_gnu() == gnu;
}

static char g_reason[128];

static const char *ct_skip_reason(const ctest_test *t) {
    if (t->spec.skip) return "skipped";
#ifndef CT_HAS_CRASH_DETECT
    if (t->spec.abort || t->spec.signal)
        return "crash detection not supported on this platform";
#endif
    if (!ct_platform_ok(t->spec.platforms)) {
        int n = 0;
        char *p = g_reason;
        int rem = (int)sizeof g_reason;
        int w = snprintf(p, (size_t)rem, "requires ");
        p += w; rem -= w;
        for (int i = 0; i < CT_MAX_PLATFORMS && t->spec.platforms[i]; i++) {
            if (n++) { w = snprintf(p, (size_t)rem, " or "); p += w; rem -= w; }
            w = snprintf(p, (size_t)rem, "%s", t->spec.platforms[i]);
            p += w; rem -= w;
        }
        return g_reason;
    }
    if (!ct_std_ok(t->spec.std)) {
        snprintf(g_reason, sizeof g_reason, "requires %s", t->spec.std);
        return g_reason;
    }
    if (t->spec.depends_on && ct_dep_has_failed(t->spec.depends_on)) {
        snprintf(g_reason, sizeof g_reason, "dependency '%s' failed", t->spec.depends_on);
        return g_reason;
    }
    return NULL;
}

static double ct_now(void) {
#ifdef _WIN32
    LARGE_INTEGER freq, count;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    return (double)count.QuadPart / (double)freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
#endif
}

/*
 * Per-test timeouts (`.timeout = <ms>`). On POSIX, the test body runs with a
 * real-time interval timer armed; if it fires we longjmp back. On Windows,
 * per-test timeouts are not supported (the c-test launcher enforces its own
 * global --timeout via TerminateProcess instead).
 */
#ifndef _WIN32
static sigjmp_buf ct_alarm_jb;

static void ct_alarm_h(int sig) {
    (void)sig;
    siglongjmp(ct_alarm_jb, 1);
}

static void ct_timeout_start(int ms) {
    if (ms <= 0) return;
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = ct_alarm_h;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGALRM, &sa, NULL);
    struct itimerval iv;
    memset(&iv, 0, sizeof iv);
    iv.it_value.tv_sec = ms / 1000;
    iv.it_value.tv_usec = (ms % 1000) * 1000;
    setitimer(ITIMER_REAL, &iv, NULL);
}

static void ct_timeout_stop(void) {
    struct itimerval iv;
    memset(&iv, 0, sizeof iv);
    setitimer(ITIMER_REAL, &iv, NULL);
    signal(SIGALRM, SIG_DFL);
}
#else  /* _WIN32 */
static void ct_timeout_start(int ms) { (void)ms; }
static void ct_timeout_stop(void)    {}
#endif /* _WIN32 */

static unsigned g_rng = 0x9e3779b9u;

static void ct_seed(unsigned s) {
    g_rng = s ? s : 0x9e3779b9u;
}

static unsigned ct_rand(void) {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

static void ct_shuffle(size_t *arr, size_t n) {
    for (size_t i = n - 1; i > 0; i--) {
        size_t j = ct_rand() % (i + 1);
        size_t t = arr[i];
        arr[i] = arr[j];
        arr[j] = t;
    }
}

static void ct_slow_insert(ctest_slow *arr, int *n, const char *name, double secs) {
    int i = *n;
    while (i > 0 && arr[i - 1].seconds < secs) i--;
    if (i >= 5) return;
    if (*n < 5) (*n)++;
    for (int j = *n - 1; j > i; j--) arr[j] = arr[j - 1];
    arr[i].name = name;
    arr[i].seconds = secs;
}

#define CT_BOLD "\033[1m"
#define CT_DIM "\033[2m"
#define CT_RED "\033[31m"
#define CT_GREEN "\033[32m"
#define CT_YELLOW "\033[33m"
#define CT_CYAN "\033[36m"
#define CT_RESET "\033[0m"

#define CT_GLYPH_OK        "\xEF\x81\x98"
#define CT_GLYPH_FAIL      "\xEF\x81\x97"
#define CT_GLYPH_CRASH     "\xEF\x81\xAA"
#define CT_GLYPH_TIMEOUT   "\xEF\x89\x90"
#define CT_GLYPH_SKIP      "\xEF\x82\x8B"
#define CT_GLYPH_KNOWN     "\xEF\x81\x9A"
#define CT_GLYPH_CHECK     "\xEF\x80\x8C"
#define CT_GLYPH_TIMES     "\xEF\x80\x8D"
#define CT_GLYPH_FLASK     "\xEF\x83\x83"
#define CT_GLYPH_FOLDER    "\xEF\x81\xBB"
#define CT_GLYPH_CLOCK     "\xEF\x80\x97"
#define CT_GLYPH_TACHOMETER "\xEF\x83\xA4"

static int g_force_no_color;

static int ct_color(void) {
    static int v = -1;
#ifdef _WIN32
    if (v < 0) v = _isatty(_fileno(stdout));
#else
    if (v < 0) v = isatty(fileno(stdout));
#endif
    if (g_force_no_color) v = 0;
    return v;
}

static const char *ct_ansi(const char *code) {
    return ct_color() ? code : "";
}

static const char *ct_glyph(const char *g) {
    return ct_color() ? g : "";
}

static const char *ct_g_ok(void)   { return ct_color() ? CT_GLYPH_OK : "[ok]"; }
static const char *ct_g_fail(void) { return ct_color() ? CT_GLYPH_FAIL : "[FAIL]"; }
static const char *ct_g_crash(void)   { return ct_color() ? CT_GLYPH_CRASH : "[CRASH]"; }
static const char *ct_g_timeout(void) { return ct_color() ? CT_GLYPH_TIMEOUT : "[TIMEOUT]"; }
static const char *ct_g_skip(void) { return ct_color() ? CT_GLYPH_SKIP : "[SKIP]"; }
static const char *ct_g_known(void) { return ct_color() ? CT_GLYPH_KNOWN : "[KNOWN]"; }

static int g_name_width;
static int g_total;
static const char *g_last_file;

static void ct_print_source(const char *file, int line) {
    FILE *f = fopen(file, "r");
    if (!f) return;
    int l = 0;
    char buf[1024];
    while (l < line && fgets(buf, sizeof buf, f)) l++;
    fclose(f);
    if (l != line) return;
    size_t n = strlen(buf);
    while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r' || buf[n - 1] == ' '))
        buf[--n] = '\0';
    printf("      %s%4d |%s %s\n", ct_ansi(CT_DIM), line, ct_ansi(CT_RESET), buf);
}

static void ct_rule(void) {
    const char *r = ct_color()
        ? "────────────────────────────────────────"
        : "----------------------------------------";
    printf("  %s%s%s\n", ct_ansi(CT_DIM), r, ct_ansi(CT_RESET));
}

static void ct_pretty_begin(int total) {
    g_total = total;
    g_last_file = NULL;
    const char *dim = ct_ansi(CT_DIM), *rs = ct_ansi(CT_RESET);
    printf("\n  %s%s%s %sctest v%s%s %s%d test%s%s\n",
           ct_ansi(CT_CYAN), ct_glyph(CT_GLYPH_FLASK), rs,
           ct_ansi(CT_BOLD), CT_VERSION, rs,
           dim, total, total == 1 ? "" : "s", rs);
    ct_rule();
}

static void ct_pretty_result(const ctest_result *r) {
    const char *dim = ct_ansi(CT_DIM), *bold = ct_ansi(CT_BOLD), *rs = ct_ansi(CT_RESET);
    if (g_last_file != r->file) {
        g_last_file = r->file;
        printf("\n  %s%s%s %s%s%s\n", ct_ansi(CT_CYAN), ct_glyph(CT_GLYPH_FOLDER), rs,
               ct_ansi(CT_CYAN), r->file, rs);
    }
    if (r->status == CT_PASS) {
        printf("  %s%s%s %-*s %s%s %s%5.1f ms%s\n", ct_ansi(CT_GREEN), ct_g_ok(), rs,
               g_name_width, r->name, dim, ct_glyph(CT_GLYPH_CLOCK), rs,
               r->seconds * 1000.0, rs);
    } else if (r->status == CT_SKIP) {
        printf("  %s%s%s %-*s %s(%s)%s\n", ct_ansi(CT_YELLOW), ct_g_skip(), rs,
               g_name_width, r->name, dim, r->skip_reason, rs);
    } else {
        const char *mark, *mb = "";
        const char *lab = "";
        int known = r->known_id != NULL;
        if (r->status == CT_CRASH) { mark = ct_g_crash(); mb = ct_ansi(CT_BOLD); lab = "[CRASH]"; }
        else if (r->status == CT_TIMEOUT) { mark = ct_g_timeout(); mb = ct_ansi(CT_BOLD); lab = "[TIMEOUT]"; }
        else if (known) { mark = ct_g_known(); mb = ct_ansi(CT_BOLD); lab = "[KNOWN]"; }
        else mark = ct_g_fail();
        const char *mc = known ? ct_ansi(CT_YELLOW) : ct_ansi(CT_RED);
        printf("  %s%s%s%s %-*s %s%s %s%5.1f ms%s", mb, mc, mark, rs,
               g_name_width, r->name, dim, ct_glyph(CT_GLYPH_CLOCK), rs,
               r->seconds * 1000.0, rs);
        if (known) {
            printf("  %s(%s%s)%s\n", dim, mb, r->known_id, rs);
        } else if (*lab) {
            printf("  %s%s%s%s\n", mb, mc, lab, rs);
        } else {
            printf("\n");
        }
        for (int i = 0; i < r->fail_count; i++) {
            const ctest_failure *f = &r->failures[i];
            printf("    %s%s:%d%s  %s%s%s", dim, f->file, f->line, rs,
                   bold, f->expr, rs);
            if (f->case_index >= 0)
                printf("  %scase [%d]: %s%s", dim, f->case_index,
                       f->case_desc ? f->case_desc : "", rs);
            if (f->msg) printf("  %s%s%s", dim, f->msg, rs);
            printf("\n");
            ct_print_source(f->file, f->line);
        }
    }
}

static void ct_progress_bar(const ctest_summary *s) {
    if (g_total <= 0) return;
    int units = 20;
    int done = s->passed + s->failed + s->crashed + s->timedout + s->skipped;
    int pu = (int)(s->passed * units / (double)g_total + 0.5);
    int fu = (int)((s->failed + s->crashed + s->timedout) * units / (double)g_total + 0.5);
    int su = (int)(s->skipped * units / (double)g_total + 0.5);
    if (pu + fu + su > units) {
        su -= pu + fu + su - units;
        if (su < 0) { fu += su; su = 0; }
        if (fu < 0) { pu += fu; fu = 0; }
    }
    printf("  [");
    for (int i = 0; i < pu; i++)
        printf("%s█%s", ct_ansi(CT_GREEN), ct_ansi(CT_RESET));
    for (int i = 0; i < fu; i++)
        printf("%s█%s", ct_ansi(CT_RED), ct_ansi(CT_RESET));
    for (int i = 0; i < su; i++)
        printf("%s█%s", ct_ansi(CT_YELLOW), ct_ansi(CT_RESET));
    for (int i = pu + fu + su; i < units; i++)
        printf("%s░%s", ct_ansi(CT_DIM), ct_ansi(CT_RESET));
    printf("] %s%3d%%%s\n", ct_ansi(CT_DIM),
           (int)(done * 100.0 / g_total + 0.5), ct_ansi(CT_RESET));
}

static void ct_pretty_end(const ctest_summary *s) {
    const char *g = ct_ansi(CT_GREEN), *r = ct_ansi(CT_RED), *y = ct_ansi(CT_YELLOW);
    const char *d = ct_ansi(CT_DIM), *rs = ct_ansi(CT_RESET);
    const char *sep = ct_color() ? "·" : "|";
    ct_rule();
    printf("  %s%s%s %d passed%s %s %s%s%s %d failed%s",
           g, ct_glyph(CT_GLYPH_CHECK), rs, s->passed, rs, sep,
           r, ct_glyph(CT_GLYPH_TIMES), rs, s->failed, rs);
    if (s->crashed) printf(" %s %s%s%s %d crashed%s", sep, ct_ansi(CT_BOLD),
                           r, ct_glyph(CT_GLYPH_CRASH), s->crashed, rs);
    if (s->timedout) printf(" %s %s%s%s %d timed out%s", sep, ct_ansi(CT_BOLD),
                            r, ct_glyph(CT_GLYPH_TIMEOUT), s->timedout, rs);
    if (s->known) printf(" %s %s%s%s %d known%s", sep, ct_ansi(CT_BOLD),
                         y, ct_glyph(CT_GLYPH_KNOWN), s->known, rs);
    printf(" %s %s%s%s %d skipped%s %s %.2f s\n", sep, y,
           ct_glyph(CT_GLYPH_SKIP), rs, s->skipped, rs, sep, s->seconds);
    if (ct_color()) ct_progress_bar(s);
    if (s->nslow > 0) {
        printf("\n  %s%s%s slowest:\n", ct_ansi(CT_DIM), ct_glyph(CT_GLYPH_TACHOMETER), rs);
        for (int i = 0; i < s->nslow; i++)
            printf("    %s%s %s%5.0f ms  %s\n", d, ct_glyph(CT_GLYPH_CLOCK), rs,
                   s->slow[i].seconds * 1000.0, s->slow[i].name);
    }
}

static void ct_quiet_begin(int total) {
    g_total = total;
}

static void ct_quiet_result(const ctest_result *r) {
    const char *rs = ct_ansi(CT_RESET);
    switch (r->status) {
    case CT_PASS: printf("%s.%s", ct_ansi(CT_GREEN), rs); break;
    case CT_FAIL:
        if (r->known_id) { printf("%sK%s", ct_ansi(CT_YELLOW), rs); break; }
        printf("%sF%s", ct_ansi(CT_RED), rs); break;
    case CT_CRASH: printf("%sC%s", ct_ansi(CT_RED), rs); break;
    case CT_TIMEOUT: printf("%sT%s", ct_ansi(CT_RED), rs); break;
    case CT_SKIP: printf("%sS%s", ct_ansi(CT_YELLOW), rs); break;
    }
    fflush(stdout);
}

static void ct_quiet_end(const ctest_summary *s) {
    printf("\n  %d passed | %d failed", s->passed, s->failed);
    if (s->crashed) printf(" | %d crashed", s->crashed);
    if (s->timedout) printf(" | %d timed out", s->timedout);
    if (s->known) printf(" | %d known", s->known);
    printf(" | %d skipped in %.2f s\n", s->skipped, s->seconds);
}

static int g_tap_num;

static void ct_tap_begin(int total) {
    printf("1..%d\n", total);
    g_tap_num = 0;
}

static void ct_tap_result(const ctest_result *r) {
    g_tap_num++;
    switch (r->status) {
    case CT_PASS:
        printf("ok %d - %s\n", g_tap_num, r->name);
        break;
    case CT_SKIP:
        printf("ok %d - %s # SKIP %s\n", g_tap_num, r->name, r->skip_reason);
        break;
    case CT_FAIL:
        if (r->known_id) {
            printf("ok %d - %s # TODO %s\n", g_tap_num, r->name, r->known_id);
            break;
        }
        printf("not ok %d - %s\n", g_tap_num, r->name);
        for (int i = 0; i < r->fail_count; i++) {
            const ctest_failure *f = &r->failures[i];
            printf("#   %s:%d %s", f->file, f->line, f->expr);
            if (f->msg) printf(" %s", f->msg);
            printf("\n");
            if (f->case_index >= 0)
                printf("#   case [%d]: %s\n", f->case_index,
                       f->case_desc ? f->case_desc : "");
        }
        break;
    case CT_CRASH:
    case CT_TIMEOUT:
        printf("not ok %d - %s\n", g_tap_num, r->name);
        for (int i = 0; i < r->fail_count; i++) {
            const ctest_failure *f = &r->failures[i];
            printf("#   %s:%d %s", f->file, f->line, f->expr);
            if (f->msg) printf(" %s", f->msg);
            printf("\n");
            if (f->case_index >= 0)
                printf("#   case [%d]: %s\n", f->case_index,
                       f->case_desc ? f->case_desc : "");
        }
        break;
    }
}

static void ct_tap_end(const ctest_summary *s) {
    printf("# %d passed, %d failed", s->passed, s->failed);
    if (s->crashed) printf(", %d crashed", s->crashed);
    if (s->timedout) printf(", %d timed out", s->timedout);
    if (s->known) printf(", %d known", s->known);
    printf(", %d skipped in %.2fs\n", s->skipped, s->seconds);
}

static const ctest_reporter ct_pretty = { ct_pretty_begin, ct_pretty_result, ct_pretty_end };
static const ctest_reporter ct_tap = { ct_tap_begin, ct_tap_result, ct_tap_end };
static const ctest_reporter ct_quiet = { ct_quiet_begin, ct_quiet_result, ct_quiet_end };

typedef struct ct_opts {
    const char *filters[CT_MAX_FILTERS];
    int nfilters;
    const char *match;
    const char *exclude;
    ct_tagq tags;
    const char *tagq_str;
    const char *skip_tags;
    int fail_fast;
    int shuffle;
    unsigned seed;
    int list;
    int quiet;
    int tap;
    const char *run;
    const ctest_reporter *reporter;
} ct_opts;

static void ct_usage(FILE *out) {
    fprintf(out,
        "usage: c-test [options]\n"
        "       c-test --list [options]      list matching tests (machine format)\n"
        "       c-test --run <name> [options] run a single test (machine format;\n"
        "                                    append ' [j]' to run case j only)\n"
        "\n"
        "  selection\n"
        "    --tags <query>    select tests by tag. suites are just tags:\n"
        "                      \"math\"            run the math suite\n"
        "                      \"math,fast\"       math OR fast\n"
        "                      \"math+fast\"       math AND fast\n"
        "                      \"!slow\"           everything except slow\n"
        "    --tag <tag>       alias for --tags\n"
        "    --skip-tags <t>   skip tests carrying any of these tags (comma list)\n"
        "    --filter <text>   run tests whose name contains <text> (repeatable,\n"
        "                      prefix with ! to exclude)\n"
        "    --match <text>    run tests whose name contains <text> (folded)\n"
        "    --exclude <text>  exclude tests whose name contains <text>\n"
        "\n"
        "  per-test traits (in it(...), Swift-testing style)\n"
        "    .tags = { \"math\", \"fast\" }  tag the test\n"
        "    .skip = 1              always skip\n"
        "    .abort = 1             assert the test calls abort()\n"
        "    .signal = SIGSEGV      assert the test raises that signal\n"
        "    .leak = 1              leaks are OK (default: assert no leak)\n"
        "    .no_alloc = 1          assert no heap allocation\n"
        "    .buf = <n>             allocate n-byte canary-guarded buffer; access as ct_buf\n"
        "    .depends_on = \"name\"   skip if the named test failed\n"
        "    .retry = N             retry up to N times on failure before marking failed\n"
        "    .env = {{\"K\",\"V\"},{0}}  set env vars for the test body; NULL value unsets\n"
        "    .setup = fn            called before the body (receives it_t *)\n"
        "    .teardown = fn         called after the body (it->result is populated)\n"
        "    .data = ptr            arbitrary context passed to all hooks via it->data\n"
        "    .platforms = {\"linux\"}              only run on listed platforms\n"
        "    .platforms = {\"linux\",\"windows\"}   run on any of the listed platforms\n"
        "    .known = \"JIRA-123\"    expected failure; counts as known, not failed\n"
        "    .timeout = <ms>        abort the test after <ms> milliseconds\n"
        "\n"
        "  execution (standalone mode)\n"
        "    --fail-fast       stop at the first failing test\n"
        "    --shuffle         randomize test order\n"
        "    --seed <n>        seed for --shuffle (deterministic)\n"
        "\n"
        "  output\n"
        "    --quiet           minimal dot output\n"
        "    --no-color        disable colors\n"
        "    --pretty          pretty reporter (default)\n"
        "    --tap             TAP reporter\n"
        "    --help            show this help\n"
        "\n"
        "  examples\n"
        "    c-test                        run every test in this binary\n"
        "    c-test --tags math            run the math suite\n"
        "    c-test --run 'detect primes'  run a single test\n"
        "    c-test --match 'prime'     substring on test names\n"
        "\n"
        "  the c-test CLI launcher drives this binary with --list and --run;\n"
        "  both emit tab-separated machine output.\n");
}

static int ct_take(const char *arg, const char *name, const char **out,
                   int *i, int argc, char **argv) {
    size_t n = strlen(name);
    if (strncmp(arg, name, n) != 0) return 0;
    if (arg[n] == '=') { *out = arg + n + 1; return 1; }
    if (arg[n] == '\0' && *i + 1 < argc) { *out = argv[++*i]; return 1; }
    return -1;
}

static int ct_matches(const ctest_test *t, const ct_opts *o) {
    const char *name = t->spec.name;
    if (o->nfilters) {
        for (int i = 0; i < o->nfilters; i++) {
            const char *f = o->filters[i];
            if (f[0] == '!') {
                if (ct_stristr(name, f + 1)) return 0;
            } else if (!ct_stristr(name, f)) {
                return 0;
            }
        }
    }
    if (o->exclude && ct_stristr(name, o->exclude)) return 0;
    if (o->match && !ct_stristr(name, o->match)) return 0;
    if (!ct_tagq_match(&o->tags, &t->spec)) return 0;
    if (o->skip_tags) {
        const char *p = o->skip_tags;
        while (*p) {
            const char *st = p;
            while (*p && *p != ',') p++;
            size_t n = p - st;
            if (n > 0 && n < 64) {
                char buf[64];
                memcpy(buf, st, n);
                buf[n] = '\0';
                if (ct_has_tag(&t->spec, buf)) return 0;
            }
            if (*p == ',') p++;
        }
    }
    return 1;
}

static void ct_build_sel(const ct_opts *o, size_t **out, size_t *nout) {
    size_t cap = g_count ? g_count : 1;
    size_t *sel = (size_t *)malloc(cap * sizeof *sel);
    size_t n = 0;
    for (size_t i = 0; i < g_count; i++)
        if (ct_matches(g_tests[i], o)) sel[n++] = i;
    *out = sel;
    *nout = n;
}

static int ct_list(const ct_opts *o) {
    size_t nsel = 0;
    size_t *sel = NULL;
    ct_build_sel(o, &sel, &nsel);
    for (size_t i = 0; i < nsel; i++) {
        const ctest_test *t = g_tests[sel[i]];
        printf("%s\t%s\t%d\t%d\t%s\t%d\n", t->spec.name, t->file, t->line, t->spec.timeout,
               t->spec.depends_on ? t->spec.depends_on : "",
               t->spec.retry);
    }
    free(sel);
    return 0;
}

/*
 * Hex dump of a case's bytes, the generic way to identify what a failing
 * parameterized case contained. C has no reflection, so raw bytes are the
 * only universal rendering.
 */
static char *ct_case_desc(const char *bytes, size_t n) {
    size_t shown = n < 16 ? n : 16;
    char *buf = (char *)malloc(shown * 3 + 4);
    size_t k = 0;
    for (size_t i = 0; i < shown; i++) {
        if (i) buf[k++] = ' ';
        k += (size_t)snprintf(buf + k, 4, "%02x", (unsigned char)bytes[i]);
    }
    if (n > shown) {
        buf[k++] = ' ';
        buf[k++] = '.';
        buf[k++] = '.';
    }
    buf[k] = '\0';
    return buf;
}

/* Per-run case descriptions, kept alive so failures can reference them. */
static char **g_case_descs;
static size_t g_case_desc_n;

static void ct_case_descs_clear(void) {
    for (size_t i = 0; i < g_case_desc_n; i++) free(g_case_descs[i]);
    free(g_case_descs);
    g_case_descs = NULL;
    g_case_desc_n = 0;
}

/*
 * Run a single test once. Plain tests run `fn` once; parameterized tests run
 * `fn` once per case in [from, to) with g_case_ptr and the case attribution
 * set, so every recorded failure is stamped with which case it came from.
 * A per-case `.timeout` aborts only that iteration; the loop continues.
 * Returns nonzero if any case timed out. Callers must free case descriptions
 * with ct_case_descs_clear() after reporting.
 */
/*
 * ct_run_once — invoke t->fn() once (case context already set in globals).
 * Enforces .abort/.signal crash expectations and .leak/.no_alloc heap checks.
 * Returns 1 if the invocation timed out, 0 otherwise.
 */
static int ct_run_once(const ctest_test *t) {
    int crash_expected = t->spec.abort || t->spec.signal;

#if !defined(_WIN32)  /* POSIX: fork + waitpid */
    if (crash_expected) {
        int expected = t->spec.abort ? SIGABRT : t->spec.signal;
        pid_t pid = fork();
        if (pid < 0) {
            ctest_expect(0, t->file, t->line,
                t->spec.abort ? ".abort" : ".signal", "fork() failed");
            return 0;
        }
        if (pid == 0) {
            signal(SIGALRM, SIG_DFL);
            close(STDOUT_FILENO); close(STDERR_FILENO);
            if (t->spec.timeout > 0)
                alarm((unsigned)((t->spec.timeout + 999) / 1000));
            ct_buf_setup(t->spec.buf);
            t->fn();
            ct_restore_stdout_if_capturing();
            ct_buf_check(t);
            _exit(0);
        }
        int st = 0;
        waitpid(pid, &st, 0);
        if (!(WIFSIGNALED(st) && WTERMSIG(st) == expected))
            ctest_expect(0, t->file, t->line,
                t->spec.abort ? ".abort" : ".signal",
                ct_signal_explain(expected, st));
        return 0;
    }
#elif defined(_MSC_VER)  /* MSVC: SEH */
    if (crash_expected) {
        int expected = t->spec.abort ? SIGABRT : t->spec.signal;
        void (*prev)(int) = NULL;
        volatile int caught = 0;
        volatile int completed = 0;
        if (expected == SIGABRT) prev = signal(SIGABRT, ct_abort_to_seh);
        ct_buf_setup(t->spec.buf);
        __try {
            t->fn();
            completed = 1;
        }
        __except (ct_seh_filter((unsigned int)GetExceptionCode(), expected)) {
            caught = 1;
        }
        if (prev) signal(SIGABRT, prev);
        if (completed) {
            ct_restore_stdout_if_capturing();
            ct_buf_check(t);
        } else {
            ct_restore_stdout_if_capturing();
            ct_buf_free();
        }
        if (!caught)
            ctest_expect(0, t->file, t->line,
                t->spec.abort ? ".abort" : ".signal",
                "expected abort/signal did not occur");
        return 0;
    }
#endif

    /* Normal path: optional heap checks + timeout. */
    int timed_out = 0;

#if defined(CT_HAS_HEAP_TRACK) && (defined(__linux__) || defined(__APPLE__))
    long pre_live  = !t->spec.leak  ? ct_alloc_live  : 0;
    long pre_calls =  t->spec.no_alloc ? ct_alloc_calls : 0;
#elif defined(CT_HAS_HEAP_TRACK) && defined(_MSC_VER)
    _CrtMemState _ct_s1;
    if (!t->spec.leak) _CrtMemCheckpoint(&_ct_s1);
#endif

    ct_buf_setup(t->spec.buf);
#ifdef _WIN32
    ct_timeout_start(t->spec.timeout);
    t->fn();
    ct_timeout_stop();
    ct_restore_stdout_if_capturing();
    ct_buf_check(t);
#else
    if (sigsetjmp(ct_alarm_jb, 1) == 0) {
        ct_timeout_start(t->spec.timeout);
        t->fn();
        ct_timeout_stop();
        ct_restore_stdout_if_capturing();
        ct_buf_check(t);
    } else {
        ct_timeout_stop();
        timed_out = 1;
        ct_restore_stdout_if_capturing();
        ct_buf_free();
        if (g_fail_count < CT_MAX_FAILURES) {
            ctest_failure *f = &g_failures[g_fail_count];
            f->file = t->file;
            f->line = t->line;
            f->expr = "timed out (per-test .timeout)";
            f->msg  = NULL;
            f->case_index = g_cur_case;
            f->case_desc  = g_cur_case_desc;
        }
        g_fail_count++;
    }
#endif

    if (!timed_out) {
#if defined(CT_HAS_HEAP_TRACK) && (defined(__linux__) || defined(__APPLE__))
        if (!t->spec.leak && ct_alloc_live > pre_live)
            ctest_expect(0, t->file, t->line, ".leak", "memory leaked");
        if (t->spec.no_alloc && ct_alloc_calls > pre_calls)
            ctest_expect(0, t->file, t->line, ".no_alloc",
                         "unexpected heap allocation");
#elif defined(CT_HAS_HEAP_TRACK) && defined(_MSC_VER)
        if (!t->spec.leak) {
            _CrtMemState _ct_s2, _ct_diff;
            _CrtMemCheckpoint(&_ct_s2);
            if (_CrtMemDifference(&_ct_diff, &_ct_s1, &_ct_s2))
                ctest_expect(0, t->file, t->line, ".leak", "memory leaked");
        }
#endif
    }
    return timed_out;
}

static int ct_run_body(const ctest_test *t, size_t from, size_t to,
                       void (*emit_case)(size_t, const char *)) {
    ct_case_descs_clear();
    int timed_out = 0;

    int is_gen = (t->cases_gen != NULL);
    size_t count = is_gen ? t->cases_gen_count : t->cases_count;

    if (count == 0 && !is_gen) {
        g_cur_case = -1;
        g_cur_case_desc = NULL;
        g_case_ptr = NULL;
        return ct_run_once(t);
    }
    if (to > count) to = count;
    if (from > to) from = to;

    void *gen_buf = NULL;
    if (is_gen && t->cases_elem > 0) {
        gen_buf = malloc(t->cases_elem);
        if (!gen_buf) { fprintf(stderr, "ctest: out of memory\n"); return 0; }
    }

    g_case_desc_n = to - from;
    g_case_descs = g_case_desc_n ? (char **)malloc(g_case_desc_n * sizeof *g_case_descs) : NULL;

    for (size_t j = from; j < to; j++) {
        const char *elem_ptr;
        if (is_gen) {
            srand(g_seed ^ (unsigned)(j * 2654435761u));
            t->cases_gen(gen_buf);
            elem_ptr = (const char *)gen_buf;
        } else {
            elem_ptr = (const char *)t->cases_data + j * t->cases_elem;
        }
        g_case_descs[j - from] = ct_case_desc(elem_ptr, t->cases_elem);
        if (emit_case) emit_case(j, g_case_descs[j - from]);
        g_cur_case = (int)j;
        g_cur_case_desc = g_case_descs[j - from];
        g_case_ptr = elem_ptr;
        if (ct_run_once(t)) {
            timed_out = 1;
            /* timeout failure already recorded by ct_run_once */
        }
    }
    if (gen_buf) free(gen_buf);
    return timed_out;
}

static void ct_emit_running(size_t j, const char *desc) {
    printf("running\t%zu\t%s\n", j, desc);
    fflush(stdout);
}

static void ct_print_failures(const ctest_failure *fs, int n) {
    if (n > CT_MAX_FAILURES) n = CT_MAX_FAILURES;
    for (int j = 0; j < n; j++) {
        if (fs[j].case_index >= 0 && fs[j].case_desc)
            printf("case\t%d\t%s\n", fs[j].case_index, fs[j].case_desc);
        printf("fail\t%s\t%d\t%s", fs[j].file, fs[j].line, fs[j].expr);
        if (fs[j].msg) printf("\t%s", fs[j].msg);
        printf("\n");
    }
}

static int ct_run_one(const char *name) {
    int only = -1;
    char *base = strdup(name);
    size_t blen = strlen(name);
    if (blen >= 4 && name[blen - 1] == ']') {
        const char *lb = strrchr(name, '[');
        if (lb && lb > name && lb[-1] == ' ' && lb[1] >= '0' && lb[1] <= '9') {
            char *end;
            long v = strtol(lb + 1, &end, 10);
            if (*end == ']') {
                only = (int)v;
                blen = (size_t)(lb - name) - 1;
                char *nb = (char *)malloc(blen + 1);
                memcpy(nb, name, blen);
                nb[blen] = '\0';
                free(base);
                base = nb;
            }
        }
    }
    int rc = 2;
    for (size_t i = 0; i < g_count; i++) {
        const ctest_test *t = g_tests[i];
        if (strcmp(t->spec.name, base) != 0) continue;
        if (only >= 0 && ((t->cases_count == 0 && t->cases_gen == NULL) ||
                          (t->cases_gen ? (size_t)only >= t->cases_gen_count
                                        : (size_t)only >= t->cases_count))) {
            size_t have = t->cases_gen ? t->cases_gen_count : t->cases_count;
            fprintf(stderr, "c-test: %s: no case %d (has %zu)\n",
                    t->spec.name, only, have);
            rc = 2;
            goto done;
        }
        double t0 = ct_now();
        const char *reason = ct_skip_reason(t);
        if (reason) {
            printf("result\tS\t%.3f\t%s\n", ct_now() - t0, t->spec.name);
            printf("skip\t%s\n", reason);
            rc = 0;
            goto done;
        }
        g_fail_count = 0;
        ctest_setup();
        ctest_before_each(&t->spec);
        if (t->spec.setup) t->spec.setup(&t->spec);
        ct_env_push(t->spec.env);
        int timed_out = ct_run_body(t, only >= 0 ? (size_t)only : 0,
                                    only >= 0 ? (size_t)only + 1
                                              : (t->cases_gen ? t->cases_gen_count
                                                              : t->cases_count),
                                    ct_emit_running);
        ct_env_pop(t->spec.env);
        {
            it_t after_spec = t->spec;
            after_spec.result = timed_out ? CT_TIMEOUT : (g_fail_count > 0 ? CT_FAIL : CT_PASS);
            if (t->spec.teardown) t->spec.teardown(&after_spec);
            ctest_after_each(&after_spec);
        }
        ctest_teardown();
        double secs = ct_now() - t0;
        if (timed_out) {
            printf("result\tT\t%.3f\t%s\n", secs, t->spec.name);
            ct_print_failures(g_failures, g_fail_count);
            ct_case_descs_clear();
            rc = 0;
            goto done;
        }
        if (g_fail_count > 0) {
            printf("result\tF\t%.3f\t%s\n", secs, t->spec.name);
            ct_print_failures(g_failures, g_fail_count);
            if (t->spec.known) printf("known\t%s\n", t->spec.known);
            ct_case_descs_clear();
            rc = t->spec.known ? 0 : 1;
            goto done;
        }
        ct_case_descs_clear();
        printf("result\tP\t%.3f\t%s\n", secs, t->spec.name);
        rc = 0;
        goto done;
    }
    fprintf(stderr, "c-test: no test named \"%s\"\n", base);
done:
    free(base);
    return rc;
}

static int ct_execute(const ct_opts *o, const size_t *sel, size_t nsel) {
    const ctest_reporter *R = o->quiet ? &ct_quiet : o->tap ? &ct_tap : o->reporter;
    ct_dep_clear_failed();
    g_name_width = 0;
    for (size_t i = 0; i < nsel; i++) {
        size_t l = strlen(g_tests[sel[i]]->spec.name);
        if (l > (size_t)g_name_width) g_name_width = (int)l;
    }
    g_total = (int)nsel;
    R->begin((int)nsel);

    int passed = 0, failed = 0, skipped = 0, timedout = 0, known = 0;
    ctest_slow slow[5];
    int nslow = 0;
    double t_start = ct_now();

    ctest_setup();

    for (size_t k = 0; k < nsel; k++) {
        const ctest_test *t = g_tests[sel[k]];
        const char *reason = ct_skip_reason(t);
        ctest_result res = {0};
        res.name = t->spec.name;
        res.file = t->file;
        res.line = t->line;
        if (reason) {
            res.status = CT_SKIP;
            res.skip_reason = reason;
            skipped++;
            R->result(&res);
            continue;
        }
        g_fail_count = 0;
        double t0 = ct_now();
        ctest_before_each(&t->spec);
        if (t->spec.setup) t->spec.setup(&t->spec);
        ct_env_push(t->spec.env);
        int timed_out = ct_run_body(t, 0, t->cases_gen ? t->cases_gen_count
                                                        : t->cases_count, NULL);
        int retries_left = t->spec.retry;
        int flaky = 0;
        while (retries_left > 0 && (timed_out || g_fail_count > 0)) {
            retries_left--;
            flaky = 1;
            g_fail_count = 0;
            timed_out = ct_run_body(t, 0, t->cases_gen ? t->cases_gen_count
                                                       : t->cases_count, NULL);
        }
        ct_env_pop(t->spec.env);
        {
            it_t after_spec = t->spec;
            after_spec.result = timed_out ? CT_TIMEOUT : (g_fail_count > 0 ? CT_FAIL : CT_PASS);
            if (t->spec.teardown) t->spec.teardown(&after_spec);
            ctest_after_each(&after_spec);
        }        res.seconds = ct_now() - t0;
        if (timed_out) {
            res.status = CT_TIMEOUT;
        } else if (g_fail_count > 0) {
            res.status = CT_FAIL;
            if (t->spec.known) res.known_id = t->spec.known;
        } else {
            res.status = CT_PASS;
        }
        res.fail_count = g_fail_count > CT_MAX_FAILURES ? CT_MAX_FAILURES : g_fail_count;
        res.failures = g_failures;
        res.flaky = flaky;
        R->result(&res);
        ct_case_descs_clear();
        if (timed_out) {
            timedout++;
            ct_dep_note_failed(t->spec.name);
            if (o->fail_fast) break;
        } else if (g_fail_count > 0) {
            if (t->spec.known) {
                known++;
            } else {
                failed++;
                ct_dep_note_failed(t->spec.name);
                if (o->fail_fast) break;
            }
        } else {
            passed++;
        }
        ct_slow_insert(slow, &nslow, t->spec.name, res.seconds);
    }

    ctest_teardown();
    ct_dep_clear_failed();

    double t_end = ct_now();
    ctest_summary sum;
    memset(&sum, 0, sizeof sum);
    sum.passed = passed;
    sum.failed = failed;
    sum.skipped = skipped;
    sum.known = known;
    sum.timedout = timedout;
    sum.seconds = t_end - t_start;
    sum.nslow = nslow;
    for (int i = 0; i < nslow; i++) sum.slow[i] = slow[i];
    R->end(&sum);

    return failed > 0 ? 1 : 0;
}

int ctest_run(int argc, char **argv) {
#if defined(__linux__)
    prctl(PR_SET_DUMPABLE, 0);
#endif
    g_tests = NULL;
    g_count = 0;
#ifdef CT_SECTION_MSVC
    {
        /* Walk pointer array in [ct_msvc_sec_start+1, ct_msvc_sec_stop). */
        const struct ctest_test * const *pp = &ct_msvc_sec_start + 1;
        const struct ctest_test * const *pe = &ct_msvc_sec_stop;
        size_t cap = 64;
        g_tests = (const struct ctest_test **)malloc(cap * sizeof *g_tests);
        if (!g_tests) { fprintf(stderr, "ctest: out of memory\n"); return 2; }
        for (; pp < pe; pp++) {
            if (!*pp) continue;
            if (g_count >= cap) {
                cap *= 2;
                g_tests = (const struct ctest_test **)realloc(
                    g_tests, cap * sizeof *g_tests);
                if (!g_tests) { fprintf(stderr, "ctest: out of memory\n"); return 2; }
            }
            g_tests[g_count++] = *pp;
        }
    }
#else
    {
        const struct ctest_test * const *pp = CT_SEC_PTR_START;
        const struct ctest_test * const *pe = CT_SEC_PTR_STOP;
        size_t cap = (size_t)(pe - pp) + 1;
        g_tests = (const struct ctest_test **)malloc(cap * sizeof *g_tests);
        if (!g_tests) { fprintf(stderr, "ctest: out of memory\n"); return 2; }
        for (; pp < pe; pp++) {
            if (!*pp) continue;
            if (g_count >= cap) {
                cap *= 2;
                g_tests = (const struct ctest_test **)realloc(
                    g_tests, cap * sizeof *g_tests);
                if (!g_tests) { fprintf(stderr, "ctest: out of memory\n"); return 2; }
            }
            g_tests[g_count++] = *pp;
        }
    }
#endif
    ct_opts o;
    memset(&o, 0, sizeof o);
    o.reporter = &ct_pretty;
    o.seed = (unsigned)time(NULL);
    int bad = 0;
    for (int i = 1; i < argc && !bad; i++) {
        const char *a = argv[i];
        const char *v;
        int r;
        if (strcmp(a, "--help") == 0) { ct_usage(stdout); return 0; }
        else if (strcmp(a, "--list") == 0) o.list = 1;

        else if (strcmp(a, "--fail-fast") == 0) o.fail_fast = 1;
        else if (strcmp(a, "--shuffle") == 0) o.shuffle = 1;
        else if (strcmp(a, "--quiet") == 0 || strcmp(a, "-q") == 0) o.quiet = 1;
        else if (strcmp(a, "--no-color") == 0) g_force_no_color = 1;
        else if (strcmp(a, "--pretty") == 0) o.reporter = &ct_pretty;
        else if (strcmp(a, "--tap") == 0) { o.reporter = &ct_tap; o.tap = 1; }
        else if (strcmp(a, "--run") == 0) {
            if (i + 1 < argc) o.run = argv[++i];
            else bad = 1;
        }
        else if ((r = ct_take(a, "--tags", &v, &i, argc, argv)) != 0 ||
                 (r = ct_take(a, "--tag", &v, &i, argc, argv)) != 0) {
            if (r < 0) bad = 1; else o.tagq_str = v;
        }
        else if ((r = ct_take(a, "--skip-tags", &v, &i, argc, argv)) != 0) {
            if (r < 0) bad = 1; else o.skip_tags = v;
        }
        else if ((r = ct_take(a, "--match", &v, &i, argc, argv)) != 0) {
            if (r < 0) bad = 1; else o.match = v;
        }
        else if ((r = ct_take(a, "--exclude", &v, &i, argc, argv)) != 0) {
            if (r < 0) bad = 1; else o.exclude = v;
        }
        else if ((r = ct_take(a, "--seed", &v, &i, argc, argv)) != 0) {
            if (r < 0) bad = 1; else { o.seed = (unsigned)strtoul(v, NULL, 10); g_seed = o.seed; }
        }
        else if ((r = ct_take(a, "--filter", &v, &i, argc, argv)) != 0) {
            if (r < 0) bad = 1;
            else if (o.nfilters < CT_MAX_FILTERS) o.filters[o.nfilters++] = v;
            else bad = 1;
        }
        else {
            fprintf(stderr, "c-test: unknown option %s\n", a);
            bad = 1;
        }
    }
    if (bad) { ct_usage(stderr); return 2; }

    if (o.tagq_str && ct_tagq_parse(&o.tags, o.tagq_str) < 0) {
        fprintf(stderr, "c-test: invalid tag query: %s\n", o.tagq_str);
        return 2;
    }

    if (o.list) return ct_list(&o);
    if (o.run) return ct_run_one(o.run);

    size_t nsel = 0;
    size_t *sel = NULL;
    ct_build_sel(&o, &sel, &nsel);

    if (o.shuffle) {
        ct_seed(o.seed);
        ct_shuffle(sel, nsel);
    }

    int rc = ct_execute(&o, sel, nsel);
    free(sel);
    return rc;
}

#else

#include <signal.h>

/* Minimal non-test mode for app binaries built without -DCTEST (e.g. the
 * same source compiled as a plain application). Every test macro becomes a
 * no-op: `it("name") { body }` expands to an unreferenced static function
 * (stripped by `-ffunction-sections -Wl,--gc-sections`), and assertions
 * devolve to `(void)((0 && (expr)))` so the bodies still type-check without
 * referencing the framework.
 */
#define it(...) CT_PARAM_TYPEDEF(__VA_ARGS__)                                \
    CT_PARAM_USE(__VA_ARGS__)                                                \
    static void CT_UNUSED CT_NAME(ctest_fn) CT_SIG_IF(__VA_ARGS__)
#define CT_NCT_SELECT(_1, _2, NAME, ...) NAME
#define CT_NOP_1(expr) ((void)(0 && (expr)))
#define CT_NOP_2(expr, msg) ((void)(0 && (expr)))
#define expect(...) CT_NCT_SELECT(__VA_ARGS__, CT_NOP_2, CT_NOP_1, 0)(__VA_ARGS__)

/* Stubs so test bodies still type-check when analysed without -DCTEST
 * (e.g. by clangd / IDEs that index without compile_commands.json). */
static unsigned char *ct_buf = (unsigned char *)0;
static CT_UNUSED void ct_capture_stdout(void) { (void)0; }
static CT_UNUSED const char *ct_captured(void) { return ""; }

#endif

#endif
