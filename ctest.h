#ifndef CTEST_H
#define CTEST_H

#if !(defined(__GNUC__) || defined(__clang__))
#error "ctest requires GCC or Clang"
#endif

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#define CT_VERSION "0.1.0"

#define CT_MAX_TAGS 16
#define CT_MAX_FAILURES 8

#define CT_JOIN(a, b) CT_JOIN_(a, b)
#define CT_JOIN_(a, b) a##b
#define CT_NAME(p) CT_JOIN(p, __LINE__)

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
#define CT_CHECK(...) CT_CHECK_N(__VA_ARGS__, 0,)
#define CT_IS_PAREN_PROBE(...) CT_PROBE()
#define CT_IS_PAREN(x) CT_CHECK(CT_IS_PAREN_PROBE x)

#define CT_DE1(...) __VA_ARGS__
#define CT_DE(t) CT_DE1 t

#define CT_CAT(a, ...) CT_CAT_I(a, __VA_ARGS__)
#define CT_CAT_I(a, ...) a##__VA_ARGS__

#define CT_NARG(...) CT_NARG_I(__VA_ARGS__, 8, 7, 6, 5, 4, 3, 2, 1, 0)
#define CT_NARG_I(_1, _2, _3, _4, _5, _6, _7, _8, N, ...) N

#define CT_ARG_1(a) a
#define CT_ARG_2(a, b) b
#define CT_ARG_3(a, b, c) c
#define CT_ARG_4(a, b, c, d) d
#define CT_ARG_5(a, b, c, d, e) e
#define CT_ARG_6(a, b, c, d, e, f) f
#define CT_ARG_7(a, b, c, d, e, f, g) g
#define CT_ARG_8(a, b, c, d, e, f, g, h) h
#define CT_ARG_N(n, ...) CT_ARG_N_I(CT_CAT(CT_ARG_, n), __VA_ARGS__)
#define CT_ARG_N_I(name, ...) name(__VA_ARGS__)

#define CT_DROP_LAST_1(a)
#define CT_DROP_LAST_2(a, b) a
#define CT_DROP_LAST_3(a, b, c) a, b
#define CT_DROP_LAST_4(a, b, c, d) a, b, c
#define CT_DROP_LAST_5(a, b, c, d, e) a, b, c, d
#define CT_DROP_LAST_6(a, b, c, d, e, f) a, b, c, d, e
#define CT_DROP_LAST_7(a, b, c, d, e, f, g) a, b, c, d, e, f
#define CT_DROP_LAST_8(a, b, c, d, e, f, g, h) a, b, c, d, e, f, g
#define CT_DROP_LAST(...) CT_DROP_LAST_I(CT_CAT(CT_DROP_LAST_, CT_NARG(__VA_ARGS__)), __VA_ARGS__)
#define CT_DROP_LAST_I(name, ...) name(__VA_ARGS__)

#define CT_LAST_ARG(...) CT_ARG_N(CT_NARG(__VA_ARGS__), __VA_ARGS__)
#define CT_HAS_CASES(...) CT_IS_PAREN(CT_LAST_ARG(__VA_ARGS__))

#define CT_SPEC_IF(...) CT_SPEC_IF_II(CT_HAS_CASES(__VA_ARGS__), __VA_ARGS__)
#define CT_SPEC_IF_II(c, ...) CT_CAT(CT_SPEC_IF_, c)(__VA_ARGS__)
#define CT_REG_IF(...) CT_REG_IF_II(CT_HAS_CASES(__VA_ARGS__), __VA_ARGS__)
#define CT_REG_IF_II(c, ...) CT_CAT(CT_REG_IF_, c)(__VA_ARGS__)
#define CT_SIG_IF(...) CT_SIG_IF_II(CT_HAS_CASES(__VA_ARGS__), __VA_ARGS__)
#define CT_SIG_IF_II(c, ...) CT_CAT(CT_SIG_IF_, c)(__VA_ARGS__)
#define CT_SIG_IF_0(...) (void)
#define CT_SIG_IF_1(...) (CT_NAME(ct_param) it)

#define CT_MAX_PARAM_CASES 64
#define CT_PARAM_N(array) (sizeof(array) / sizeof((array)[0]))

/*
 * Parameterized tests: `cases(array)` is passed as the last `it` argument and
 * expands to the bare parenthesized array, so the trailing-parenthesis probe
 * above still identifies it. `it` derives the case type from the array element
 * type with __typeof__ and declares a by-value `it` struct for the body.
 */
#define cases(array) (array)

#define CT_PARAM_ARRAY(...) CT_DE(CT_LAST_ARG(__VA_ARGS__))

#define CT_PARAM_TYPEDEF_0(...)
#define CT_PARAM_TYPEDEF_1(...)                                             \
    typedef char CT_NAME(ct_case_ok)                                        \
        [(CT_PARAM_N(CT_PARAM_ARRAY(__VA_ARGS__)) > 0 &&                    \
          CT_PARAM_N(CT_PARAM_ARRAY(__VA_ARGS__)) <= CT_MAX_PARAM_CASES)    \
             ? 1 : -1];                                                     \
    typedef __typeof__(*(CT_PARAM_ARRAY(__VA_ARGS__))) CT_NAME(ct_param);
#define CT_PARAM_TYPEDEF(...) CT_PARAM_TYPEDEF_II(CT_HAS_CASES(__VA_ARGS__), __VA_ARGS__)
#define CT_PARAM_TYPEDEF_II(c, ...) CT_CAT(CT_PARAM_TYPEDEF_, c)(__VA_ARGS__)

/* In non-test builds `it` keeps the array alive via a pointer so clang's
 * -Wunused-const-variable does not flag case tables that are only read here. */
#define CT_PARAM_USE_0(...)
#define CT_PARAM_USE_1(...) static __attribute__((unused)) const void *       \
    CT_NAME(ct_use) = (const void *)CT_PARAM_ARRAY(__VA_ARGS__);
#define CT_PARAM_USE(...) CT_PARAM_USE_II(CT_HAS_CASES(__VA_ARGS__), __VA_ARGS__)
#define CT_PARAM_USE_II(c, ...) CT_CAT(CT_PARAM_USE_, c)(__VA_ARGS__)

#define CT_PARAM_CALL_0(...) CT_NAME(ctest_fn)();
#define CT_PARAM_CALL_1(...) CT_NAME(ctest_fn)(*(CT_NAME(ct_param) *)g_case_ptr);
#define CT_PARAM_CALL(...) CT_PARAM_CALL_II(CT_HAS_CASES(__VA_ARGS__), __VA_ARGS__)
#define CT_PARAM_CALL_II(c, ...) CT_CAT(CT_PARAM_CALL_, c)(__VA_ARGS__)

typedef struct it_t {
    const char *name;
    const char *tags[CT_MAX_TAGS];
    int skip;
    int only;
    const char *platforms;
    const char *std;
    const char *known;
    int timeout;
} it_t;

typedef struct ctest_failure {
    const char *file;
    int line;
    const char *expr;
    const char *msg;
    int case_index;
    const char *case_desc;
} ctest_failure;

typedef enum ctest_status { CT_PASS, CT_FAIL, CT_SKIP, CT_CRASH, CT_TIMEOUT } ctest_status;

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

int ctest_run(int argc, char **argv);

int main(int argc, char **argv) {
    return ctest_run(argc, argv);
}

#define main ctest_app_main

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

#include <regex.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/*
 * Optional lifecycle hooks. Define any (or all) of these in your test file
 * to run code around the suite:
 *
 *     void ctest_setup(void)        once before all tests (per process)
 *     void ctest_teardown(void)     once after all tests (per process)
 *     void ctest_before_each(void)  before each test
 *     void ctest_after_each(void)   after each test
 *
 * Because every test runs in its own child process under the c-test runner,
 * setup/teardown run once per child there; in standalone mode they run once
 * per suite.
 */
void ctest_setup(void) __attribute__((weak));
void ctest_teardown(void) __attribute__((weak));
void ctest_before_each(void) __attribute__((weak));
void ctest_after_each(void) __attribute__((weak));

#if defined(__linux__)
#include <sys/prctl.h>

static void __attribute__((constructor)) ctest_nodump(void) {
    prctl(PR_SET_DUMPABLE, 0);
}
#endif

void ctest_register(it_t spec, const char *file, int line, void (*fn)(void),
                    const void *cases_data, size_t cases_elem,
                    size_t cases_count);

/*
 * Parameterized tests: `cases(array)` is passed as the last `it` argument.
 * `it` derives the case type from the array's element type and declares a
 * by-value `it` struct for the body:
 *
 *     static const struct { int a, b; } mult_cases[] = { {2, 3}, {3, 4} };
 *     it("multiply", cases(mult_cases)) {
 *         expect(it.a * it.b == 6, "multiplication");
 *     }
 *
 * Plain tests take no signature; `it` emits `(void)` automatically:
 *
 *     it("does something") { ... }
 *
 * `cases` may be combined with the other `it` options and must be the last
 * argument: it("name", .tags = {"math"}, cases(array)).
 */
#define CT_SPEC_IF_0(...) it_t spec = { __VA_ARGS__ };
#define CT_SPEC_IF_1(...) it_t spec = { CT_DROP_LAST(__VA_ARGS__) };

#define CT_REG_IF_0(...)                                                     \
    ctest_register(spec, __FILE__, __LINE__, CT_NAME(ctest_fn), NULL, 0, 0);
#define CT_REG_IF_1(...) do {                                                \
    const void *ct_data = (const void *)CT_PARAM_ARRAY(__VA_ARGS__);         \
    size_t ct_n = CT_PARAM_N(CT_PARAM_ARRAY(__VA_ARGS__));                   \
    if (ct_n > CT_MAX_PARAM_CASES) ct_n = CT_MAX_PARAM_CASES;                \
    ctest_register(spec, __FILE__, __LINE__, CT_NAME(ct_call), ct_data,      \
                   sizeof((CT_PARAM_ARRAY(__VA_ARGS__))[0]), ct_n);          \
} while (0);

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
#else
#define CT_DIAG_PUSH                                                         \
    _Pragma("GCC diagnostic push")                                           \
    _Pragma("GCC diagnostic ignored \"-Wmissing-field-initializers\"")
#define CT_DIAG_POP _Pragma("GCC diagnostic pop")
#endif

#define it(...) CT_DIAG_PUSH                                                 \
    CT_PARAM_TYPEDEF(__VA_ARGS__)                                            \
    static void CT_NAME(ctest_fn)();                                         \
    static void __attribute__((unused)) CT_NAME(ct_call)(void) {             \
        CT_PARAM_CALL(__VA_ARGS__)                                           \
    }                                                                        \
    static __attribute__((constructor)) void CT_NAME(ctest_reg)(void) {      \
        CT_SPEC_IF(__VA_ARGS__)                                              \
        CT_REG_IF(__VA_ARGS__)                                               \
    }                                                                        \
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
} ctest_test;

static ctest_test *g_tests;
static size_t g_count;
static size_t g_cap;
static const void *g_case_ptr;

void ctest_register(it_t spec, const char *file, int line, void (*fn)(void),
                    const void *cases_data, size_t cases_elem,
                    size_t cases_count) {
    if (g_count == g_cap) {
        g_cap = g_cap ? g_cap * 2 : 64;
        g_tests = realloc(g_tests, g_cap * sizeof *g_tests);
    }
    g_tests[g_count].spec = spec;
    g_tests[g_count].file = file;
    g_tests[g_count].line = line;
    g_tests[g_count].fn = fn;
    g_tests[g_count].cases_data = cases_data;
    g_tests[g_count].cases_elem = cases_elem;
    g_tests[g_count].cases_count = cases_count;
    g_count++;
}

static ctest_failure g_failures[CT_MAX_FAILURES];
static int g_fail_count;
static int g_cur_case = -1;
static const char *g_cur_case_desc;

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

static const char *ct_signal_explain(int sig, int st)
    __attribute__((unused));
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

#define ct_expect_signal(sig, ...) do {                                    \
    pid_t ct_pid = fork();                                                 \
    if (ct_pid < 0) {                                                      \
        expect(0, "fork() failed");                                        \
        break;                                                             \
    }                                                                      \
    if (ct_pid == 0) {                                                     \
        ct_timeout_stop();                                                 \
        __VA_ARGS__                                                        \
        _exit(0);                                                          \
    }                                                                      \
    int ct_st = 0;                                                         \
    waitpid(ct_pid, &ct_st, 0);                                            \
    expect(WIFSIGNALED(ct_st) && WTERMSIG(ct_st) == (sig),                 \
           ct_signal_explain((sig), ct_st));                               \
} while (0)

#define ct_expect_abort(...) ct_expect_signal(SIGABRT, __VA_ARGS__)

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
            char *buf = malloc(len + 1);
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

static int ct_platform_ok(const char *req) {
    if (!req) return 1;
    return strstr(req, CT_OS) != NULL;
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

static char g_reason[64];

static const char *ct_skip_reason(const ctest_test *t, int only_mode) {
    if (t->spec.skip) return "skipped";
    if (only_mode && !t->spec.only) return "only-mode";
    if (!ct_platform_ok(t->spec.platforms)) {
        snprintf(g_reason, sizeof g_reason, "requires %s", t->spec.platforms);
        return g_reason;
    }
    if (!ct_std_ok(t->spec.std)) {
        snprintf(g_reason, sizeof g_reason, "requires %s", t->spec.std);
        return g_reason;
    }
    return NULL;
}

static double ct_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/*
 * Per-test timeouts (`.timeout = <ms>`). The test body runs with a real-time
 * interval timer armed; if it fires we longjmp back to the runner and report
 * the test as timed out. Timer helpers are no-ops when no timeout is set.
 */
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
    if (v < 0) v = isatty(fileno(stdout));
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
    regex_t match_re;
    const char *exclude;
    ct_tagq tags;
    const char *tagq_str;
    const char *skip_tags;
    int only;
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
        "    --match <regex>   run tests whose name matches <regex>\n"
        "    --exclude <text>  exclude tests whose name contains <text>\n"
        "    --only            run only tests marked .only\n"
        "\n"
        "  per-test traits (in it(...), Swift-testing style)\n"
        "    .tags = { \"math\", \"fast\" }  tag the test\n"
        "    .only = 1              run only this test (with --only)\n"
        "    .skip = 1              always skip\n"
        "    .platforms = \"linux\"    only run on matching platforms\n"
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
        "    c-test --match 'prime|power'  regex on test names\n"
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
    if (o->match && regexec(&o->match_re, name, 0, NULL, 0) != 0) return 0;
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
    size_t *sel = malloc(cap * sizeof *sel);
    size_t n = 0;
    for (size_t i = 0; i < g_count; i++)
        if (ct_matches(&g_tests[i], o)) sel[n++] = i;
    *out = sel;
    *nout = n;
}

static int ct_list(const ct_opts *o) {
    size_t nsel = 0;
    size_t *sel = NULL;
    ct_build_sel(o, &sel, &nsel);
    for (size_t i = 0; i < nsel; i++) {
        const ctest_test *t = &g_tests[sel[i]];
        printf("%s\t%s\t%d\n", t->spec.name, t->file, t->line);
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
    char *buf = malloc(shown * 3 + 4);
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
static int ct_run_body(const ctest_test *t, size_t from, size_t to,
                       void (*emit_case)(size_t, const char *)) {
    ct_case_descs_clear();
    int timed_out = 0;
    if (t->cases_count == 0) {
        g_cur_case = -1;
        g_cur_case_desc = NULL;
        g_case_ptr = NULL;
        if (sigsetjmp(ct_alarm_jb, 1) == 0) {
            ct_timeout_start(t->spec.timeout);
            t->fn();
            ct_timeout_stop();
        } else {
            ct_timeout_stop();
            timed_out = 1;
            if (g_fail_count < CT_MAX_FAILURES) {
                ctest_failure *f = &g_failures[g_fail_count];
                f->file = t->file;
                f->line = t->line;
                f->expr = "timed out (per-test .timeout)";
                f->msg = NULL;
                f->case_index = -1;
                f->case_desc = NULL;
            }
            g_fail_count++;
        }
        return timed_out;
    }
    if (to > t->cases_count) to = t->cases_count;
    if (from > to) from = to;
    g_case_desc_n = to - from;
    g_case_descs = g_case_desc_n ? malloc(g_case_desc_n * sizeof *g_case_descs) : NULL;
    for (size_t j = from; j < to; j++) {
        g_case_descs[j - from] = ct_case_desc(
            (const char *)t->cases_data + j * t->cases_elem, t->cases_elem);
        if (emit_case) emit_case(j, g_case_descs[j - from]);
        g_cur_case = (int)j;
        g_cur_case_desc = g_case_descs[j - from];
        g_case_ptr = (const char *)t->cases_data + j * t->cases_elem;
        int to2 = 0;
        if (sigsetjmp(ct_alarm_jb, 1) == 0) {
            ct_timeout_start(t->spec.timeout);
            t->fn();
            ct_timeout_stop();
        } else {
            ct_timeout_stop();
            to2 = 1;
        }
        if (to2) {
            timed_out = 1;
            if (g_fail_count < CT_MAX_FAILURES) {
                ctest_failure *f = &g_failures[g_fail_count];
                f->file = t->file;
                f->line = t->line;
                f->expr = "timed out (per-test .timeout)";
                f->msg = NULL;
                f->case_index = (int)j;
                f->case_desc = g_case_descs[j - from];
            }
            g_fail_count++;
        }
    }
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

static int ct_run_one(const char *name, int only_mode) {
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
                char *nb = malloc(blen + 1);
                memcpy(nb, name, blen);
                nb[blen] = '\0';
                free(base);
                base = nb;
            }
        }
    }
    int rc = 2;
    for (size_t i = 0; i < g_count; i++) {
        const ctest_test *t = &g_tests[i];
        if (strcmp(t->spec.name, base) != 0) continue;
        if (only >= 0 && (t->cases_count == 0 ||
                          (size_t)only >= t->cases_count)) {
            fprintf(stderr, "c-test: %s: no case %d (has %zu)\n",
                    t->spec.name, only, t->cases_count);
            rc = 2;
            goto done;
        }
        double t0 = ct_now();
        const char *reason = ct_skip_reason(t, only_mode);
        if (reason) {
            printf("result\tS\t%.3f\t%s\n", ct_now() - t0, t->spec.name);
            printf("skip\t%s\n", reason);
            rc = 0;
            goto done;
        }
        g_fail_count = 0;
        if (ctest_setup) ctest_setup();
        if (ctest_before_each) ctest_before_each();
        int timed_out = ct_run_body(t, only >= 0 ? (size_t)only : 0,
                                    only >= 0 ? (size_t)only + 1
                                              : t->cases_count,
                                    ct_emit_running);
        if (ctest_after_each) ctest_after_each();
        if (ctest_teardown) ctest_teardown();
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

static int ct_execute(const ct_opts *o, const size_t *sel, size_t nsel,
                      int only_mode) {
    const ctest_reporter *R = o->quiet ? &ct_quiet : o->tap ? &ct_tap : o->reporter;
    g_name_width = 0;
    for (size_t i = 0; i < nsel; i++) {
        size_t l = strlen(g_tests[sel[i]].spec.name);
        if (l > (size_t)g_name_width) g_name_width = (int)l;
    }
    g_total = (int)nsel;
    R->begin((int)nsel);

    int passed = 0, failed = 0, skipped = 0, timedout = 0, known = 0;
    ctest_slow slow[5];
    int nslow = 0;
    double t_start = ct_now();

    if (ctest_setup) ctest_setup();

    for (size_t k = 0; k < nsel; k++) {
        const ctest_test *t = &g_tests[sel[k]];
        const char *reason = ct_skip_reason(t, only_mode);
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
        if (ctest_before_each) ctest_before_each();
        int timed_out = ct_run_body(t, 0, t->cases_count, NULL);
        if (ctest_after_each) ctest_after_each();
        res.seconds = ct_now() - t0;
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
        R->result(&res);
        ct_case_descs_clear();
        if (timed_out) {
            timedout++;
            if (o->fail_fast) break;
        } else if (g_fail_count > 0) {
            if (t->spec.known) {
                known++;
            } else {
                failed++;
                if (o->fail_fast) break;
            }
        } else {
            passed++;
        }
        ct_slow_insert(slow, &nslow, t->spec.name, res.seconds);
    }

    if (ctest_teardown) ctest_teardown();

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
        else if (strcmp(a, "--only") == 0) o.only = 1;
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
            if (r < 0) bad = 1; else o.seed = (unsigned)strtoul(v, NULL, 10);
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
    if (o.match) {
        if (regcomp(&o.match_re, o.match, REG_EXTENDED | REG_ICASE) != 0) {
            fprintf(stderr, "c-test: invalid regex: %s\n", o.match);
            return 2;
        }
    }

    int only_mode = o.only;
    if (!only_mode) {
        for (size_t i = 0; i < g_count; i++)
            if (g_tests[i].spec.only) { only_mode = 1; break; }
    }

    if (o.list) return ct_list(&o);
    if (o.run) return ct_run_one(o.run, only_mode);

    size_t nsel = 0;
    size_t *sel = NULL;
    ct_build_sel(&o, &sel, &nsel);

    if (o.shuffle) {
        ct_seed(o.seed);
        ct_shuffle(sel, nsel);
    }

    int rc = ct_execute(&o, sel, nsel, only_mode);
    free(sel);
    return rc;
}

#else

#include <signal.h>

/*
 * Minimal non-test mode for app binaries built without -DCTEST (e.g. the
 * same source compiled as a plain application). Every test macro becomes a
 * no-op, but each still *references* its arguments so parameterized test
 * signatures do not produce -Wunused-parameter warnings when the header is
 * parsed by an editor or IDE without the -DCTEST define.
 */
#define it(...) CT_PARAM_TYPEDEF(__VA_ARGS__)                                \
    CT_PARAM_USE(__VA_ARGS__)                                                \
    static void __attribute__((unused)) CT_NAME(ctest_fn) CT_SIG_IF(__VA_ARGS__)
#define CT_NCT_SELECT(_1, _2, NAME, ...) NAME
#define CT_NOP_1(expr) ((void)(0 && (expr)))
#define CT_NOP_2(expr, msg) ((void)(0 && (expr)))
#define expect(...) CT_NCT_SELECT(__VA_ARGS__, CT_NOP_2, CT_NOP_1, 0)(__VA_ARGS__)
#define ct_expect_signal(sig, ...) ((void)(0 && (sig)))
#define ct_expect_abort(...) ct_expect_signal(SIGABRT, __VA_ARGS__)

#endif

#endif
