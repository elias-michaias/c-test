#ifndef CTEST_H
#define CTEST_H

#if !(defined(__GNUC__) || defined(__clang__))
#error "ctest requires GCC or Clang"
#endif

#define CT_VERSION "0.1.0"

#define CT_MAX_TAGS 16
#define CT_MAX_FAILURES 8

#define CT_JOIN(a, b) CT_JOIN_(a, b)
#define CT_JOIN_(a, b) a##b
#define CT_NAME(p) CT_JOIN(p, __LINE__)

#if defined(CTEST)

#define main ctest_app_main

typedef struct it_t {
    const char *name;
    const char *tags[CT_MAX_TAGS];
    int skip;
    int only;
    const char *platforms;
    const char *std;
} it_t;

void ctest_register(it_t spec, const char *file, int line, void (*fn)(void));

#define it(...)                                                           \
    static void CT_NAME(ctest_fn)(void);                                  \
    static __attribute__((constructor)) void CT_NAME(ctest_reg)(void) {   \
        ctest_register((it_t){ __VA_ARGS__ }, __FILE__, __LINE__,         \
                       CT_NAME(ctest_fn));                                \
    }                                                                     \
    static void CT_NAME(ctest_fn)(void)

typedef struct ctest_failure {
    const char *file;
    int line;
    const char *expr;
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
} ctest_result;

typedef struct ctest_slow { const char *name; double seconds; } ctest_slow;
typedef struct ctest_summary {
    int passed;
    int failed;
    int skipped;
    double seconds;
    int nslow;
    ctest_slow slow[5];
} ctest_summary;

typedef struct ctest_reporter {
    void (*begin)(int total);
    void (*result)(const ctest_result *r);
    void (*end)(const ctest_summary *s);
} ctest_reporter;

extern const ctest_reporter ct_pretty;
extern const ctest_reporter ct_tap;

int ctest_run(int argc, char **argv);

void ctest_expect(int ok, const char *file, int line, const char *expr);

#define expect(expr) ctest_expect((expr) != 0, __FILE__, __LINE__, #expr)

#else

#define it(...) static void __attribute__((unused)) CT_NAME(ctest_fn)(void)
#define expect(expr) ((void)0)

#endif

#endif
