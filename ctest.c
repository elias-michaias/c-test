#if defined(CTEST)

#include "ctest.h"

#include <dirent.h>
#include <errno.h>
#include <poll.h>
#include <regex.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#undef main

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

#define CT_MAX_FILTERS 8
#define CT_TAGQ_MAX 16

typedef struct ctest_test {
    it_t spec;
    const char *file;
    int line;
    void (*fn)(void);
} ctest_test;

static ctest_test *g_tests;
static size_t g_count;
static size_t g_cap;

void ctest_register(it_t spec, const char *file, int line, void (*fn)(void)) {
    if (g_count == g_cap) {
        g_cap = g_cap ? g_cap * 2 : 64;
        g_tests = realloc(g_tests, g_cap * sizeof *g_tests);
    }
    g_tests[g_count].spec = spec;
    g_tests[g_count].file = file;
    g_tests[g_count].line = line;
    g_tests[g_count].fn = fn;
    g_count++;
}

static ctest_failure g_failures[CT_MAX_FAILURES];
static int g_fail_count;

void ctest_expect(int ok, const char *file, int line, const char *expr) {
    if (ok) return;
    if (g_fail_count < CT_MAX_FAILURES) {
        ctest_failure *f = &g_failures[g_fail_count];
        f->file = file;
        f->line = line;
        f->expr = expr;
    }
    g_fail_count++;
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

typedef struct ct_xfer {
    int fail_count;
    double seconds;
    ctest_failure failures[CT_MAX_FAILURES];
} ct_xfer;

static const ctest_failure g_fork_failure = { "<fork>", 0, "failed to fork test process" };

static char g_detail[96];

static pid_t ct_fork_test(int pfds[2], size_t idx) {
    if (pipe(pfds) < 0) return -1;
    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0) {
        close(pfds[0]);
        close(pfds[1]);
        return -1;
    }
    if (pid == 0) {
        close(pfds[0]);
        struct rlimit rl = { 0, 0 };
        setrlimit(RLIMIT_CORE, &rl);
#if defined(__linux__)
        prctl(PR_SET_DUMPABLE, 0);
#endif
        g_fail_count = 0;
        double t0 = ct_now();
        g_tests[idx].fn();
        double t1 = ct_now();
        ct_xfer xf;
        xf.fail_count = g_fail_count;
        xf.seconds = t1 - t0;
        for (int i = 0; i < xf.fail_count && i < CT_MAX_FAILURES; i++)
            xf.failures[i] = g_failures[i];
        ssize_t w = write(pfds[1], &xf, sizeof xf);
        (void)w;
        fflush(NULL);
        _exit(0);
    }
    close(pfds[1]);
    return pid;
}

static void ct_synth(ct_xfer *xf, const ctest_test *t, double started,
                     const char *detail) {
    memset(xf, 0, sizeof *xf);
    xf->fail_count = 1;
    xf->failures[0].file = t->file;
    xf->failures[0].line = t->line;
    xf->failures[0].expr = detail;
    xf->seconds = ct_now() - started;
}

static int ct_finish(int fd, int st, const ctest_test *t, ct_xfer *xf,
                     double started) {
    ssize_t n = read(fd, xf, sizeof *xf);
    close(fd);
    if (n == (ssize_t)sizeof *xf) {
        if (xf->fail_count > CT_MAX_FAILURES) xf->fail_count = CT_MAX_FAILURES;
        return 0;
    }
    if (WIFSIGNALED(st)) {
        snprintf(g_detail, sizeof g_detail, "crashed: %s",
                 strsignal(WTERMSIG(st)));
    } else if (WIFEXITED(st)) {
        snprintf(g_detail, sizeof g_detail, "exited unexpectedly (status %d)",
                 WEXITSTATUS(st));
    } else {
        snprintf(g_detail, sizeof g_detail, "terminated unexpectedly");
    }
    ct_synth(xf, t, started, g_detail);
    return 1;
}

static int ct_timeout_kill(pid_t pid, int fd, const ctest_test *t, ct_xfer *xf,
                           double started, double timeout) {
    kill(pid, SIGKILL);
    int st;
    waitpid(pid, &st, 0);
    snprintf(g_detail, sizeof g_detail, "timed out after %d ms",
             (int)(timeout * 1000.0));
    ct_synth(xf, t, started, g_detail);
    char drain[64];
    while (read(fd, drain, sizeof drain) > 0) {}
    close(fd);
    return 2;
}

static int ct_poll_ready(const struct pollfd *fds, int nf, int fd) {
    for (int i = 0; i < nf; i++)
        if (fds[i].fd == fd)
            return (fds[i].revents & (POLLIN | POLLHUP | POLLERR)) != 0;
    return 0;
}

typedef struct ct_slot {
    pid_t pid;
    int fd;
    int used;
    size_t idx;
    double started;
} ct_slot;

#define CT_BOLD "\033[1m"
#define CT_DIM "\033[2m"
#define CT_RED "\033[31m"
#define CT_GREEN "\033[32m"
#define CT_YELLOW "\033[33m"
#define CT_CYAN "\033[36m"
#define CT_RESET "\033[0m"

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

static const char *ct_g_ok(void)   { return ct_color() ? "✓" : "[ok]"; }
static const char *ct_g_fail(void) { return ct_color() ? "✗" : "[FAIL]"; }
static const char *ct_g_crash(void)   { return ct_color() ? "✗" : "[CRASH]"; }
static const char *ct_g_timeout(void) { return ct_color() ? "✗" : "[TIMEOUT]"; }
static const char *ct_g_skip(void) { return ct_color() ? "–" : "[SKIP]"; }

static int ct_is_fail(ctest_status s) {
    return s == CT_FAIL || s == CT_CRASH || s == CT_TIMEOUT;
}

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

static void ct_pretty_begin(int total) {
    g_total = total;
    g_last_file = NULL;
    const char *sep = ct_color() ? "·" : "|";
    printf("\n  ctest %s %s %d test%s\n", CT_VERSION, sep, total,
           total == 1 ? "" : "s");
}

static void ct_pretty_result(const ctest_result *r) {
    const char *dim = ct_ansi(CT_DIM), *bold = ct_ansi(CT_BOLD), *rs = ct_ansi(CT_RESET);
    const char *bar = ct_color() ? "──" : "--";
    if (g_last_file != r->file) {
        g_last_file = r->file;
        printf("\n  %s%s %s %s%s\n", ct_ansi(CT_CYAN), bar, r->file, bar, rs);
    }
    if (r->status == CT_PASS) {
        printf("  %s%s%s %-*s %s%6.1f ms%s\n", ct_ansi(CT_GREEN), ct_g_ok(), rs,
               g_name_width, r->name, dim, r->seconds * 1000.0, rs);
    } else if (r->status == CT_SKIP) {
        printf("  %s%s%s %-*s %s(%s)%s\n", ct_ansi(CT_YELLOW), ct_g_skip(), rs,
               g_name_width, r->name, dim, r->skip_reason, rs);
    } else {
        const char *mark, *mb = "";
        if (r->status == CT_CRASH) { mark = ct_g_crash(); mb = ct_ansi(CT_BOLD); }
        else if (r->status == CT_TIMEOUT) { mark = ct_g_timeout(); mb = ct_ansi(CT_BOLD); }
        else mark = ct_g_fail();
        printf("  %s%s%s%s %-*s %s%6.1f ms%s\n", mb, ct_ansi(CT_RED), mark, rs,
               g_name_width, r->name, dim, r->seconds * 1000.0, rs);
        for (int i = 0; i < r->fail_count; i++) {
            const ctest_failure *f = &r->failures[i];
            printf("    %s%s:%d%s  %s%s%s\n", dim, f->file, f->line, rs,
                   bold, f->expr, rs);
            ct_print_source(f->file, f->line);
        }
    }
}

static void ct_progress_bar(const ctest_summary *s) {
    if (g_total <= 0) return;
    int units = 20;
    int pu = (int)(s->passed * units / (double)g_total + 0.5);
    int fu = (int)(s->failed * units / (double)g_total + 0.5);
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
    printf("]\n");
}

static void ct_pretty_end(const ctest_summary *s) {
    const char *g = ct_ansi(CT_GREEN), *r = ct_ansi(CT_RED), *y = ct_ansi(CT_YELLOW);
    const char *d = ct_ansi(CT_DIM), *rs = ct_ansi(CT_RESET);
    const char *sep = ct_color() ? "·" : "|";
    printf("\n  %s%d passed%s %s %s%d failed%s %s %s%d skipped%s %s %.2f s\n",
           g, s->passed, rs, sep, r, s->failed, rs, sep, y, s->skipped, rs,
           sep, s->seconds);
    if (ct_color()) ct_progress_bar(s);
    if (s->nslow > 0) {
        printf("\n  slowest:\n");
        for (int i = 0; i < s->nslow; i++)
            printf("    %s%5.0f ms%s  %s\n", d, s->slow[i].seconds * 1000.0,
                   rs, s->slow[i].name);
    }
}

static void ct_quiet_begin(int total) {
    g_total = total;
}

static void ct_quiet_result(const ctest_result *r) {
    const char *rs = ct_ansi(CT_RESET);
    switch (r->status) {
    case CT_PASS: printf("%s.%s", ct_ansi(CT_GREEN), rs); break;
    case CT_FAIL: printf("%sF%s", ct_ansi(CT_RED), rs); break;
    case CT_CRASH: printf("%sC%s", ct_ansi(CT_RED), rs); break;
    case CT_TIMEOUT: printf("%sT%s", ct_ansi(CT_RED), rs); break;
    case CT_SKIP: printf("%sS%s", ct_ansi(CT_YELLOW), rs); break;
    }
    fflush(stdout);
}

static void ct_quiet_end(const ctest_summary *s) {
    printf("\n  %d passed | %d failed | %d skipped in %.2f s\n",
           s->passed, s->failed, s->skipped, s->seconds);
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
    case CT_CRASH:
    case CT_TIMEOUT:
        printf("not ok %d - %s\n", g_tap_num, r->name);
        for (int i = 0; i < r->fail_count; i++) {
            const ctest_failure *f = &r->failures[i];
            printf("#   %s:%d %s\n", f->file, f->line, f->expr);
        }
        break;
    }
}

static void ct_tap_end(const ctest_summary *s) {
    printf("# %d passed, %d failed, %d skipped in %.2fs\n",
           s->passed, s->failed, s->skipped, s->seconds);
}

const ctest_reporter ct_pretty = { ct_pretty_begin, ct_pretty_result, ct_pretty_end };
const ctest_reporter ct_tap = { ct_tap_begin, ct_tap_result, ct_tap_end };
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
    int jobs;
    int timeout_ms;
    int watch;
    int list;
    int quiet;
    const ctest_reporter *reporter;
} ct_opts;

static void ct_usage(FILE *out) {
    fprintf(out,
        "usage: ctest [options]\n"
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
        "  execution\n"
        "    --fail-fast       stop at the first failing test\n"
        "    --jobs <n>        run up to <n> tests in parallel processes\n"
        "    -j <n>            alias for --jobs\n"
        "    --timeout <ms>    kill any test that runs longer than <ms>\n"
        "    --shuffle         randomize test order\n"
        "    --seed <n>        seed for --shuffle (deterministic)\n"
        "\n"
        "  output\n"
        "    --list            list selected tests and exit\n"
        "    --watch           re-run the suite when a source file changes\n"
        "    --quiet           minimal dot output\n"
        "    --no-color        disable colors\n"
        "    --pretty          pretty reporter (default)\n"
        "    --tap             TAP reporter\n"
        "    --help            show this help\n"
        "\n"
        "  examples\n"
        "    ctest --tags math              run the math suite\n"
        "    ctest --tags math,!slow        math, minus the slow ones\n"
        "    ctest --list --tags math       what the math suite contains\n"
        "    ctest --match 'prime|power'    regex on test names\n"
        "    ctest --jobs 4 --timeout 500  parallel, 500 ms per-test cap\n"
        "    ctest --watch                  re-run on source changes\n");
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
    size_t w = 0;
    for (size_t i = 0; i < nsel; i++) {
        size_t l = strlen(g_tests[sel[i]].spec.name);
        if (l > w) w = l;
    }
    const char *dim = ct_ansi(CT_DIM), *cy = ct_ansi(CT_CYAN), *rs = ct_ansi(CT_RESET);
    const char *sep = ct_color() ? "·" : "|";
    const char *bar = ct_color() ? "──" : "--";
    printf("ctest %s %s %zu test%s\n\n", CT_VERSION, sep, nsel, nsel == 1 ? "" : "s");
    const char *last = NULL;
    for (size_t i = 0; i < nsel; i++) {
        const ctest_test *t = &g_tests[sel[i]];
        if (last != t->file) {
            last = t->file;
            printf("  %s%s %s %s%s\n", cy, bar, t->file, bar, rs);
        }
        printf("  %-*s  %s%s:%d%s", (int)w, t->spec.name, dim, t->file, t->line, rs);
        if (t->spec.tags[0]) {
            printf("  [");
            for (int j = 0; j < CT_MAX_TAGS && t->spec.tags[j]; j++) {
                if (j) printf(", ");
                printf("%s", t->spec.tags[j]);
            }
            printf("]");
        }
        if (t->spec.only) printf("  (only)");
        if (t->spec.skip) printf("  (skipped)");
        if (t->spec.platforms) printf("  (platform: %s)", t->spec.platforms);
        if (t->spec.std) printf("  (std: %s)", t->spec.std);
        printf("\n");
    }
    printf("\n");
    free(sel);
    return 0;
}

typedef struct ct_wfile { char *path; time_t mtime; } ct_wfile;

static ct_wfile *g_wf;
static int g_nwf;
static int g_capwf;
static time_t g_bin_mtime;

static void ct_wscan(ct_wfile **out, int *n, int *cap, const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char path[4096];
        snprintf(path, sizeof path, "%s%s%s", dir,
                 strcmp(dir, ".") == 0 ? "/" : "", e->d_name);
        struct stat st;
        if (stat(path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            ct_wscan(out, n, cap, path);
        } else {
            const char *dot = strrchr(e->d_name, '.');
            if (dot && (strcmp(dot, ".c") == 0 || strcmp(dot, ".h") == 0)) {
                if (*n == *cap) {
                    *cap = *cap ? *cap * 2 : 64;
                    *out = realloc(*out, (size_t)*cap * sizeof **out);
                }
                (*out)[*n].path = strdup(path);
                (*out)[*n].mtime = st.st_mtime;
                (*n)++;
            }
        }
    }
    closedir(d);
}

static int ct_watch_poll(const char *self) {
    ct_wfile *nw = NULL;
    int nn = 0, cap = 0;
    ct_wscan(&nw, &nn, &cap, ".");
    struct stat st;
    time_t bin = stat(self, &st) == 0 ? st.st_mtime : 0;
    int changed = (nn != g_nwf) || (bin != g_bin_mtime);
    if (!changed) {
        for (int i = 0; i < nn && !changed; i++) {
            int found = 0;
            for (int j = 0; j < g_nwf; j++)
                if (strcmp(nw[i].path, g_wf[j].path) == 0) {
                    found = 1;
                    if (nw[i].mtime != g_wf[j].mtime) changed = 1;
                    break;
                }
            if (!found) changed = 1;
        }
    }
    for (int i = 0; i < g_nwf; i++) free(g_wf[i].path);
    free(g_wf);
    g_wf = nw;
    g_nwf = nn;
    g_capwf = cap;
    g_bin_mtime = bin;
    return changed;
}

static int ct_execute(const ct_opts *o, const size_t *sel, size_t nsel,
                      int only_mode) {
    const ctest_reporter *R = o->quiet ? &ct_quiet : o->reporter;
    int jobs = o->jobs > 0 ? o->jobs : 1;
    double timeout = o->timeout_ms > 0 ? (double)o->timeout_ms / 1000.0 : 0.0;

    g_name_width = 0;
    for (size_t i = 0; i < nsel; i++) {
        size_t l = strlen(g_tests[sel[i]].spec.name);
        if (l > (size_t)g_name_width) g_name_width = (int)l;
    }
    g_total = (int)nsel;
    R->begin((int)nsel);

    int passed = 0, failed = 0, skipped = 0;
    ctest_slow slow[5];
    int nslow = 0;
    double t_start = ct_now();

    ct_slot *slots = calloc((size_t)jobs, sizeof *slots);
    struct pollfd *fds = malloc((size_t)jobs * sizeof *fds);
    size_t next = 0;
    int stop = 0;

    while (next < nsel || !stop) {
        for (int i = 0; i < jobs; i++) {
            if (slots[i].used) continue;
            if (next >= nsel || stop) continue;
            size_t idx = sel[next];
            const ctest_test *t = &g_tests[idx];
            const char *reason = ct_skip_reason(t, only_mode);
            if (reason) {
                ctest_result res = {0};
                res.name = t->spec.name;
                res.file = t->file;
                res.line = t->line;
                res.status = CT_SKIP;
                res.skip_reason = reason;
                skipped++;
                R->result(&res);
                next++;
                continue;
            }
            int pfds[2];
            double t0 = ct_now();
            pid_t pid = ct_fork_test(pfds, idx);
            if (pid < 0) {
                ctest_result res = {0};
                res.name = t->spec.name;
                res.file = t->file;
                res.line = t->line;
                res.status = CT_FAIL;
                res.fail_count = 1;
                res.failures = &g_fork_failure;
                failed++;
                R->result(&res);
                if (o->fail_fast) stop = 1;
                next++;
                continue;
            }
            slots[i].used = 1;
            slots[i].pid = pid;
            slots[i].fd = pfds[0];
            slots[i].idx = idx;
            slots[i].started = t0;
            next++;
        }
        if (stop) break;
        if (next >= nsel) {
            int active = 0;
            for (int i = 0; i < jobs; i++)
                if (slots[i].used) { active = 1; break; }
            if (!active) break;
        }

        int nf = 0;
        for (int i = 0; i < jobs; i++)
            if (slots[i].used) {
                fds[nf].fd = slots[i].fd;
                fds[nf].events = POLLIN | POLLHUP;
                fds[nf].revents = 0;
                nf++;
            }
        int to = -1;
        if (timeout > 0) {
            double earliest = 0.0;
            for (int i = 0; i < jobs; i++)
                if (slots[i].used) {
                    double rem = timeout - (ct_now() - slots[i].started);
                    if (rem < 0) rem = 0;
                    if (earliest == 0.0 || rem < earliest) earliest = rem;
                }
            to = (int)(earliest * 1000.0);
        }
        if (nf > 0) poll(fds, (nfds_t)nf, to);

        for (int i = 0; i < jobs; i++) {
            if (!slots[i].used) continue;
            const ctest_test *t = &g_tests[slots[i].idx];
            ct_xfer xf;
            int rc;
            if (timeout > 0 && ct_now() - slots[i].started >= timeout) {
                rc = ct_timeout_kill(slots[i].pid, slots[i].fd, t, &xf,
                                     slots[i].started, timeout);
                slots[i].used = 0;
            } else {
                if (!ct_poll_ready(fds, nf, slots[i].fd)) continue;
                int st = 0;
                if (waitpid(slots[i].pid, &st, WNOHANG) != slots[i].pid) continue;
                rc = ct_finish(slots[i].fd, st, t, &xf, slots[i].started);
                slots[i].used = 0;
            }
            ctest_result res = {0};
            res.name = t->spec.name;
            res.file = t->file;
            res.line = t->line;
            res.seconds = xf.seconds;
            res.fail_count = xf.fail_count;
            res.failures = xf.failures;
            if (rc == 0)
                res.status = xf.fail_count > 0 ? CT_FAIL : CT_PASS;
            else if (rc == 2)
                res.status = CT_TIMEOUT;
            else
                res.status = CT_CRASH;
            R->result(&res);
            if (ct_is_fail(res.status)) {
                failed++;
                if (o->fail_fast) stop = 1;
            } else {
                passed++;
            }
            ct_slow_insert(slow, &nslow, t->spec.name, xf.seconds);
        }
    }

    for (int i = 0; i < jobs; i++) {
        if (!slots[i].used) continue;
        kill(slots[i].pid, SIGKILL);
        int st;
        waitpid(slots[i].pid, &st, 0);
        char drain[64];
        while (read(slots[i].fd, drain, sizeof drain) > 0) {}
        close(slots[i].fd);
    }
    free(fds);
    free(slots);

    double t_end = ct_now();
    ctest_summary sum;
    memset(&sum, 0, sizeof sum);
    sum.passed = passed;
    sum.failed = failed;
    sum.skipped = skipped;
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
        else if (strcmp(a, "--tap") == 0) o.reporter = &ct_tap;
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
        else if (strcmp(a, "--watch") == 0) o.watch = 1;
        else if ((r = ct_take(a, "--jobs", &v, &i, argc, argv)) != 0) {
            if (r < 0) bad = 1; else o.jobs = atoi(v);
        }
        else if (strcmp(a, "-j") == 0 && i + 1 < argc) o.jobs = atoi(argv[++i]);
        else if (a[0] == '-' && a[1] == 'j' && a[2] != '\0') o.jobs = atoi(a + 2);
        else if ((r = ct_take(a, "--timeout", &v, &i, argc, argv)) != 0) {
            if (r < 0) bad = 1; else o.timeout_ms = atoi(v);
        }
        else if ((r = ct_take(a, "--filter", &v, &i, argc, argv)) != 0) {
            if (r < 0) bad = 1;
            else if (o.nfilters < CT_MAX_FILTERS) o.filters[o.nfilters++] = v;
            else bad = 1;
        }
        else {
            fprintf(stderr, "ctest: unknown option %s\n", a);
            bad = 1;
        }
    }
    if (bad) { ct_usage(stderr); return 2; }

    if (o.tagq_str && ct_tagq_parse(&o.tags, o.tagq_str) < 0) {
        fprintf(stderr, "ctest: invalid tag query: %s\n", o.tagq_str);
        return 2;
    }
    if (o.match) {
        if (regcomp(&o.match_re, o.match, REG_EXTENDED | REG_ICASE) != 0) {
            fprintf(stderr, "ctest: invalid regex: %s\n", o.match);
            return 2;
        }
    }

    if (o.list) return ct_list(&o);

    int only_mode = o.only;
    if (!only_mode) {
        for (size_t i = 0; i < g_count; i++)
            if (g_tests[i].spec.only) { only_mode = 1; break; }
    }

    size_t nsel = 0;
    size_t *sel = NULL;
    ct_build_sel(&o, &sel, &nsel);

    if (o.shuffle) {
        ct_seed(o.seed);
        ct_shuffle(sel, nsel);
    }

    if (o.watch) {
        ct_watch_poll(argv[0]);
        for (;;) {
            int rc = ct_execute(&o, sel, nsel, only_mode);
            (void)rc;
            printf("\n  %sWatching for changes (Ctrl-C to stop)%s\n",
                   ct_ansi(CT_DIM), ct_ansi(CT_RESET));
            fflush(stdout);
            while (!ct_watch_poll(argv[0])) usleep(200000);
            if (ct_color()) printf("\033[2J\033[H");
            fflush(NULL);
            execv(argv[0], argv);
            fprintf(stderr, "ctest: cannot re-exec %s\n", argv[0]);
            return 1;
        }
    }

    int rc = ct_execute(&o, sel, nsel, only_mode);
    free(sel);
    return rc;
}

int main(int argc, char **argv) {
    return ctest_run(argc, argv);
}

#endif
