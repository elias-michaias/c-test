/*
 * ctest - a standalone test runner for ctest-based test binaries.
 *
 * Test executables are built as `cc mytests.c -DCTEST` (see ctest.h). This
 * program discovers which tests each binary contains via `--list`, then runs
 * each test as its own child process (`--run`) so crashes and timeouts are
 * isolated per test. Exit status is nonzero if anything failed, crashed, or
 * timed out.
 */

#include "ctest.h"

#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
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

#define CT_MAX_BINS 32
#define CT_MAX_FILTERS 8
#define CT_ARGS_MAX 64

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
#define CT_GLYPH_CUBE      "\xEF\x86\xB2"
#define CT_GLYPH_CLOCK     "\xEF\x80\x97"
#define CT_GLYPH_TACHOMETER "\xEF\x83\xA4"

static int g_force_no_color;

static void ct_setup_child(void) {
    struct rlimit rl = { 0, 0 };
    setrlimit(RLIMIT_CORE, &rl);
#if defined(__linux__)
    prctl(PR_SET_DUMPABLE, 0);
#endif
}

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

static void ct_slow_insert(ctest_slow *arr, int *n, const char *name, double secs) {
    int i = *n;
    while (i > 0 && arr[i - 1].seconds < secs) i--;
    if (i >= 5) return;
    if (*n < 5) (*n)++;
    for (int j = *n - 1; j > i; j--) arr[j] = arr[j - 1];
    arr[i].name = name;
    arr[i].seconds = secs;
}

typedef struct ct_opts {
    const char *bins[CT_MAX_BINS];
    int nbins;
    const char *filters[CT_MAX_FILTERS];
    int nfilters;
    const char *match;
    const char *exclude;
    const char *tags;
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
    int tap;
    int json;
    int rerun_failed;
    int retries;
    int slow_ms;
    int backtrace;
    const char *junit;
    const char *build_cmd;
} ct_opts;

typedef struct ct_btest {
    char *name;
    char *file;
    int line;
} ct_btest;

typedef struct ct_bin {
    const char *path;
    ct_btest *tests;
    int n;
    int cap;
} ct_bin;

typedef struct ct_run {
    const char *bin;
    const char *name;
    const char *file;
    int line;
} ct_run;

static void ct_usage(FILE *out) {
    fprintf(out,
        "usage: c-test [options] <test-binary>...\n"
        "\n"
        "runs each ctest test binary (built with -DCTEST, see ctest.h) as a\n"
        "separate process, one test per child, so crashes and timeouts are\n"
        "isolated per test.\n"
        "\n"
        "  binaries\n"
        "    <path>            test executable(s) to run\n"
        "\n"
        "  selection (forwarded to each binary)\n"
        "    --tags <query>    select tests by tag. suites are just tags:\n"
        "                      \"math\"            run the math suite\n"
        "                      \"math,fast\"       math OR fast\n"
        "                      \"math+fast\"       math AND fast\n"
        "                      \"!slow\"           everything except slow\n"
        "    --skip-tags <t>   skip tests carrying any of these tags (comma list)\n"
        "    --filter <text>   run tests whose name contains <text> (repeatable,\n"
        "                      prefix with ! to exclude)\n"
        "    --match <regex>   run tests whose name matches <regex>\n"
        "    --exclude <text>  exclude tests whose name contains <text>\n"
        "    --only            run only tests marked .only\n"
        "\n"
        "  execution\n"
        "    --fail-fast       stop at the first failing/crashing/timing-out test\n"
        "    --jobs <n>        run up to <n> tests in parallel processes\n"
        "                      (default: one per CPU)\n"
        "    -j <n>            alias for --jobs\n"
        "    --timeout <ms>    kill any test that runs longer than <ms>\n"
        "    --shuffle         randomize test order\n"
        "    --seed <n>        seed for --shuffle (deterministic)\n"
        "    --retries <n>     re-run failed/crashed/timed-out tests up to <n>\n"
        "                      times; a test that then passes is reported flaky\n"
        "    --rerun-failed    run only the tests that failed the previous run\n"
        "                      (persisted to ~/.cache/c-test/failed)\n"
        "    --backtrace       on crash, re-run the test under gdb and print a\n"
        "                      backtrace\n"
        "\n"
        "  output\n"
        "    --list            list the tests each binary contains and exit\n"
        "    --list --json     same, as JSON\n"
        "    --json            (with --list) emit JSON\n"
        "    --watch           re-run the suite when a source file or binary changes\n"
        "    --build-cmd <c>   (with --watch) run <c> before each re-run; skip the\n"
        "                      run if the build fails\n"
        "    --slow <ms>       annotate passing tests that take longer than <ms>\n"
        "    --junit <file>    write a JUnit XML report to <file>\n"
        "    --quiet           minimal dot output\n"
        "    --no-color        disable colors\n"
        "    --pretty          pretty reporter (default)\n"
        "    --tap             TAP reporter\n"
        "    --help            show this help\n"
        "\n"
        "  examples\n"
        "    c-test ./example ./crash                    run both suites\n"
        "    c-test ./example --tags math --jobs 4      parallel, math only\n"
        "    c-test ./crash --timeout 500               cap hangs at 500 ms\n"
        "    c-test --list ./example                     what a suite contains\n"
        "    c-test --watch ./example                    re-run on changes\n");
}

static int ct_take(const char *arg, const char *name, const char **out,
                   int *i, int argc, char **argv) {
    size_t n = strlen(name);
    if (strncmp(arg, name, n) != 0) return 0;
    if (arg[n] == '=') { *out = arg + n + 1; return 1; }
    if (arg[n] == '\0' && *i + 1 < argc) { *out = argv[++*i]; return 1; }
    return -1;
}

static int ct_splittab(char *line, char **f, int maxf) {
    int n = 0;
    f[n++] = line;
    while (n < maxf) {
        char *t = strchr(line, '\t');
        if (!t) break;
        *t = '\0';
        f[n++] = t + 1;
        line = t + 1;
    }
    return n;
}

/*
 * Spawn a child, redirect its stdout and stderr into a pipe, read everything,
 * and return the raw wait status. Returns -1 if the process could not start.
 */
static int ct_capture(const char *bin, char *const argv[], char **out) {
    int p[2];
    if (pipe(p) < 0) return -1;
    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0) {
        close(p[0]);
        close(p[1]);
        return -1;
    }
    if (pid == 0) {
        close(p[0]);
        dup2(p[1], 1);
        dup2(p[1], 2);
        close(p[1]);
        ct_setup_child();
        execvp(bin, argv);
        _exit(127);
    }
    close(p[1]);
    size_t cap = 8192, n = 0;
    char *buf = malloc(cap);
    char tmp[4096];
    ssize_t r;
    while ((r = read(p[0], tmp, sizeof tmp)) > 0) {
        if (n + (size_t)r + 1 > cap) {
            size_t nc = cap;
            while (nc < n + (size_t)r + 1) nc *= 2;
            buf = realloc(buf, nc);
            cap = nc;
        }
        memcpy(buf + n, tmp, (size_t)r);
        n += (size_t)r;
    }
    close(p[0]);
    buf[n] = '\0';
    int st;
    waitpid(pid, &st, 0);
    *out = buf;
    return st;
}

static void ct_bin_collect(const ct_opts *o, ct_bin *b) {
    char *argv[CT_ARGS_MAX];
    int n = 0;
    argv[n++] = (char *)b->path;
    argv[n++] = (char *)"--list";
    if (o->tags) { argv[n++] = (char *)"--tags"; argv[n++] = (char *)o->tags; }
    if (o->skip_tags) { argv[n++] = (char *)"--skip-tags"; argv[n++] = (char *)o->skip_tags; }
    for (int i = 0; i < o->nfilters; i++) {
        argv[n++] = (char *)"--filter";
        argv[n++] = (char *)o->filters[i];
    }
    if (o->match) { argv[n++] = (char *)"--match"; argv[n++] = (char *)o->match; }
    if (o->exclude) { argv[n++] = (char *)"--exclude"; argv[n++] = (char *)o->exclude; }
    if (o->only) argv[n++] = (char *)"--only";
    argv[n] = NULL;

    char *out = NULL;
    int st = ct_capture(b->path, argv, &out);
    int code = (st >= 0 && WIFEXITED(st)) ? WEXITSTATUS(st) : -1;
    if (code == 127) {
        fprintf(stderr, "c-test: %s: cannot execute (not a ctest test binary?)\n",
                b->path);
        free(out);
        return;
    }
    if (code < 0) {
        fprintf(stderr, "c-test: %s: --list failed to run\n", b->path);
        free(out);
        return;
    }
    if (code != 0) {
        fprintf(stderr, "c-test: %s: --list failed (status %d):\n%s", b->path,
                code, out ? out : "");
        free(out);
        return;
    }
    char *save = NULL;
    for (char *line = strtok_r(out, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        if (!*line) continue;
        char *f[3];
        if (ct_splittab(line, f, 3) < 3) continue;
        int line_no = atoi(f[2]);
        if (b->n == b->cap) {
            b->cap = b->cap ? b->cap * 2 : 16;
            b->tests = realloc(b->tests, (size_t)b->cap * sizeof *b->tests);
        }
        b->tests[b->n].name = strdup(f[0]);
        b->tests[b->n].file = strdup(f[1]);
        b->tests[b->n].line = line_no;
        b->n++;
    }
    free(out);
    if (b->n == 0) {
        fprintf(stderr, "c-test: %s: no tests found (not a ctest test binary?)\n",
                b->path);
    }
}

static void ct_bin_free(ct_bin *b) {
    for (int i = 0; i < b->n; i++) {
        free(b->tests[i].name);
        free(b->tests[i].file);
    }
    free(b->tests);
    b->tests = NULL;
    b->n = 0;
    b->cap = 0;
}

static pid_t ct_spawn(const char *bin, const char *name, int *rfd) {
    int p[2];
    if (pipe(p) < 0) return -1;
    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0) {
        close(p[0]);
        close(p[1]);
        return -1;
    }
    if (pid == 0) {
        close(p[0]);
        dup2(p[1], 1);
        dup2(p[1], 2);
        close(p[1]);
        ct_setup_child();
        execl(bin, bin, "--run", name, (char *)NULL);
        _exit(127);
    }
    close(p[1]);
    *rfd = p[0];
    return pid;
}

typedef struct ct_slot {
    pid_t pid;
    int fd;
    int used;
    const ct_run *t;
    double started;
    char *out;
    size_t outn;
    size_t outcap;
    int retries;
    int flaky;
} ct_slot;

typedef struct ct_det {
    ctest_status status;
    double seconds;
    char *skip_reason;
    ctest_failure failures[CT_MAX_FAILURES];
    int nfail;
    char detail[96];
    char *known;
    char *own_file[CT_MAX_FAILURES];
    char *own_expr[CT_MAX_FAILURES];
    char *own_msg[CT_MAX_FAILURES];
    char *own_case_desc[CT_MAX_FAILURES];
} ct_det;

static void ct_read_out(ct_slot *s) {
    char tmp[4096];
    ssize_t r;
    while ((r = read(s->fd, tmp, sizeof tmp)) > 0) {
        if (s->outn + (size_t)r + 1 > s->outcap) {
            size_t nc = s->outcap ? s->outcap * 2 : 8192;
            while (nc < s->outn + (size_t)r + 1) nc *= 2;
            s->out = realloc(s->out, nc);
            s->outcap = nc;
        }
        memcpy(s->out + s->outn, tmp, (size_t)r);
        s->outn += (size_t)r;
    }
    close(s->fd);
    s->fd = -1;
    if (s->out) s->out[s->outn] = '\0';
}

/*
 * Append whatever output the child has produced without blocking. A test may
 * emit protocol lines (`running\t<j>`) as it executes each parameterized
 * case, so POLLIN alone does not mean the child is done; we must keep its
 * pipe drained until POLLHUP (child exit) or the outer timeout fires.
 */
static void ct_drain(ct_slot *s) {
    int fl = fcntl(s->fd, F_GETFL, 0);
    if (fl >= 0) {
        fcntl(s->fd, F_SETFL, fl | O_NONBLOCK);
        char tmp[4096];
        ssize_t r;
        while ((r = read(s->fd, tmp, sizeof tmp)) > 0) {
            if (s->outn + (size_t)r + 1 > s->outcap) {
                size_t nc = s->outcap ? s->outcap * 2 : 8192;
                while (nc < s->outn + (size_t)r + 1) nc *= 2;
                s->out = realloc(s->out, nc);
                s->outcap = nc;
            }
            memcpy(s->out + s->outn, tmp, (size_t)r);
            s->outn += (size_t)r;
        }
        fcntl(s->fd, F_SETFL, fl);
    }
}

static int ct_parse_result(const char *buf, ct_det *d) {
    char *copy = strdup(buf ? buf : "");
    if (!copy) return -1;
    char *save = NULL;
    int got = 0;
    int cur_case = -1;
    char *cur_desc = NULL;
    for (char *line = strtok_r(copy, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        if (!got && strncmp(line, "result\t", 7) == 0) {
            char *f[4];
            if (ct_splittab(line + 7, f, 4) < 2) continue;
            d->seconds = atof(f[1]);
            if (f[0][0] == 'P') d->status = CT_PASS;
            else if (f[0][0] == 'S') d->status = CT_SKIP;
            else if (f[0][0] == 'F') d->status = CT_FAIL;
            else if (f[0][0] == 'T') d->status = CT_TIMEOUT;
            else continue;
            got = 1;
        } else if (got && strncmp(line, "case\t", 5) == 0) {
            char *f[3];
            if (ct_splittab(line + 5, f, 3) < 2) continue;
            free(cur_desc);
            cur_case = atoi(f[0]);
            cur_desc = strdup(f[1] ? f[1] : "");
        } else if (got && strncmp(line, "fail\t", 5) == 0) {
            char *f[5];
            int nf = ct_splittab(line + 5, f, 5);
            if (nf < 3) continue;
            if (d->nfail >= CT_MAX_FAILURES) continue;
            d->own_file[d->nfail] = strdup(f[0]);
            d->own_expr[d->nfail] = strdup(f[2] ? f[2] : "");
            d->failures[d->nfail].file = d->own_file[d->nfail];
            d->failures[d->nfail].line = atoi(f[1]);
            d->failures[d->nfail].expr = d->own_expr[d->nfail];
            if (cur_case >= 0) {
                d->own_case_desc[d->nfail] = strdup(cur_desc ? cur_desc : "");
                d->failures[d->nfail].case_index = cur_case;
                d->failures[d->nfail].case_desc = d->own_case_desc[d->nfail];
            } else {
                d->failures[d->nfail].case_index = -1;
                d->failures[d->nfail].case_desc = NULL;
            }
            if (nf >= 4 && f[3] && *f[3]) {
                d->own_msg[d->nfail] = strdup(f[3]);
                d->failures[d->nfail].msg = d->own_msg[d->nfail];
            } else {
                d->failures[d->nfail].msg = NULL;
            }
            d->nfail++;
        } else if (got && strncmp(line, "skip\t", 5) == 0) {
            if (!d->skip_reason) d->skip_reason = strdup(line + 5);
        } else if (got && strncmp(line, "known\t", 6) == 0) {
            if (!d->known) d->known = strdup(line + 6);
        }
    }
    free(cur_desc);
    free(copy);
    return got ? 0 : -1;
}

static void ct_synth_fail(ct_det *d, const ct_run *t, int code) {
    snprintf(d->detail, sizeof d->detail, "exited unexpectedly (status %d)", code);
    d->nfail = 1;
    d->failures[0].file = t->file;
    d->failures[0].line = t->line;
    d->failures[0].expr = d->detail;
    d->failures[0].case_index = -1;
    d->failures[0].case_desc = NULL;
}

/*
 * Attribute a crash/timeout to the parameterized case that was executing: the
 * child prints `running\t<j>\t<hex>` before each case and flushes, so the last
 * such line in the captured output identifies the failing case.
 */
static void ct_attr_last_running(const char *out, ctest_failure *f,
                                 char **desc_own) {
    if (!out) return;
    const char *p = out;
    while ((p = strstr(p, "running\t")) != NULL) {
        const char *e = strchr(p + 8, '\n');
        if (e && (e - (p + 8)) > 2) {
            const char *tab = memchr(p + 8, '\t', (size_t)(e - (p + 8)));
            if (tab) {
                int idx = atoi(p + 8);
                size_t len = (size_t)(e - tab - 1);
                char *desc = malloc(len + 1);
                memcpy(desc, tab + 1, len);
                desc[len] = '\0';
                f->case_index = idx;
                *desc_own = desc;
                f->case_desc = desc;
            }
        }
        if (!e) break;
        p = e + 1;
    }
}

static void ct_collect(ct_slot *s, const ct_run *t, double timeout_ms,
                       int is_timeout, ct_det *d) {
    memset(d, 0, sizeof *d);
    ct_read_out(s);
    if (is_timeout) {
        d->status = CT_TIMEOUT;
        snprintf(d->detail, sizeof d->detail, "timed out after %d ms",
                 (int)timeout_ms);
        d->nfail = 1;
        d->failures[0].file = t->file;
        d->failures[0].line = t->line;
        d->failures[0].expr = d->detail;
        d->failures[0].case_index = -1;
        ct_attr_last_running(s->out, &d->failures[0], &d->own_case_desc[0]);
        return;
    }
    int st;
    waitpid(s->pid, &st, 0);
    s->pid = 0;
    if (WIFSIGNALED(st)) {
        d->status = CT_CRASH;
        snprintf(d->detail, sizeof d->detail, "crashed: %s",
                 strsignal(WTERMSIG(st)));
        d->nfail = 1;
        d->failures[0].file = t->file;
        d->failures[0].line = t->line;
        d->failures[0].expr = d->detail;
        d->failures[0].case_index = -1;
        ct_attr_last_running(s->out, &d->failures[0], &d->own_case_desc[0]);
        return;
    }
    int code = WIFEXITED(st) ? WEXITSTATUS(st) : 0;
    if (ct_parse_result(s->out, d) == 0) {
        if (d->status == CT_FAIL && d->nfail == 0)
            ct_synth_fail(d, t, code);
        return;
    }
    d->status = code == 0 ? CT_PASS : CT_FAIL;
    if (code != 0) ct_synth_fail(d, t, code);
}

static void ct_det_free(ct_det *d) {
    free(d->skip_reason);
    free(d->known);
    for (int i = 0; i < d->nfail; i++) {
        free(d->own_file[i]);
        free(d->own_expr[i]);
        free(d->own_msg[i]);
        free(d->own_case_desc[i]);
    }
}

static int ct_poll_ready(const struct pollfd *fds, int nf, int fd) {
    for (int i = 0; i < nf; i++)
        if (fds[i].fd == fd)
            return (fds[i].revents & (POLLIN | POLLHUP | POLLERR)) != 0;
    return 0;
}

static int ct_poll_hup(const struct pollfd *fds, int nf, int fd) {
    for (int i = 0; i < nf; i++)
        if (fds[i].fd == fd)
            return (fds[i].revents & (POLLHUP | POLLERR)) != 0;
    return 0;
}

static int g_name_width;
static int g_total;
static const char *g_last_bin;
static int g_slow_ms;

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

static void ct_print_captured(const char *s) {
    if (!s || !*s) return;
    const char *dim = ct_ansi(CT_DIM), *rs = ct_ansi(CT_RESET);
    const char *p = s;
    while (*p) {
        size_t len = strcspn(p, "\n");
        if (len > 0 && strncmp(p, "result\t", 7) != 0 &&
            strncmp(p, "fail\t", 5) != 0 && strncmp(p, "skip\t", 5) != 0 &&
            strncmp(p, "known\t", 6) != 0 && strncmp(p, "running\t", 8) != 0 &&
            strncmp(p, "case\t", 5) != 0)
            printf("      %s%.*s%s\n", dim, (int)len, p, rs);
        p += len;
        if (*p == '\n') p++;
    }
}

static void ct_rule(void) {
    const char *r = ct_color()
        ? "────────────────────────────────────────"
        : "----------------------------------------";
    printf("  %s%s%s\n", ct_ansi(CT_DIM), r, ct_ansi(CT_RESET));
}

static void ct_pretty_begin(int total) {
    g_total = total;
    g_last_bin = NULL;
    const char *dim = ct_ansi(CT_DIM), *rs = ct_ansi(CT_RESET);
    printf("\n  %s%s%s %sctest v%s%s %s%d test%s%s\n",
           ct_ansi(CT_CYAN), ct_glyph(CT_GLYPH_FLASK), rs,
           ct_ansi(CT_BOLD), CT_VERSION, rs,
           dim, total, total == 1 ? "" : "s", rs);
    ct_rule();
}

static void ct_pretty_result(const ctest_result *r) {
    const char *dim = ct_ansi(CT_DIM), *bold = ct_ansi(CT_BOLD), *rs = ct_ansi(CT_RESET);
    if (g_last_bin != r->file) {
        g_last_bin = r->file;
        printf("\n  %s%s%s %s%s%s\n", ct_ansi(CT_CYAN), ct_glyph(CT_GLYPH_CUBE), rs,
               ct_ansi(CT_CYAN), r->file, rs);
    }
    if (r->status == CT_PASS) {
        printf("  %s%s%s %-*s %s%s %s%5.1f ms%s", ct_ansi(CT_GREEN), ct_g_ok(), rs,
               g_name_width, r->name, dim, ct_glyph(CT_GLYPH_CLOCK), rs,
               r->seconds * 1000.0, rs);
        if (r->flaky) printf("  %s(flaky)%s", ct_ansi(CT_YELLOW), rs);
        if (g_slow_ms > 0 && r->seconds * 1000.0 >= (double)g_slow_ms)
            printf("  %s[SLOW]%s", ct_ansi(CT_YELLOW), rs);
        printf("\n");
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
        if (r->status == CT_FAIL) ct_print_captured(r->captured);
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
            printf("#   %s:%d %s\n", f->file, f->line, f->expr);
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
            printf("#   %s:%d %s\n", f->file, f->line, f->expr);
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

static int ct_default_jobs(void) {
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 1;
}

static char **g_rerun_set;
static int g_rerun_n;
static char **g_failed_ids;
static int g_failed_n;

static void ct_mkdir_p(const char *path) {
    char *p = strdup(path);
    if (!p) return;
    for (char *c = p; *c; c++) {
        if (*c == '/') {
            *c = '\0';
            if (*p) mkdir(p, 0755);
            *c = '/';
        }
    }
    if (*p) mkdir(p, 0755);
    free(p);
}

static const char *ct_state_path(void) {
    static char buf[4096];
    const char *xdg = getenv("XDG_CACHE_HOME");
    const char *home = getenv("HOME");
    if (xdg && *xdg)
        snprintf(buf, sizeof buf, "%s/c-test/failed", xdg);
    else
        snprintf(buf, sizeof buf, "%s/.cache/c-test/failed",
                 home && *home ? home : ".");
    return buf;
}

static void ct_state_mkdir(void) {
    char *p = strdup(ct_state_path());
    if (!p) return;
    char *slash = strrchr(p, '/');
    if (slash) *slash = '\0';
    ct_mkdir_p(p);
    free(p);
}

static int ct_rerun_load(void) {
    FILE *f = fopen(ct_state_path(), "r");
    if (!f) return -1;
    char line[8192];
    while (fgets(line, sizeof line, f)) {
        size_t l = strlen(line);
        while (l && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = '\0';
        if (!l) continue;
        g_rerun_set = realloc(g_rerun_set,
                              (size_t)(g_rerun_n + 1) * sizeof *g_rerun_set);
        g_rerun_set[g_rerun_n++] = strdup(line);
    }
    fclose(f);
    return g_rerun_n > 0 ? 0 : -1;
}

static void ct_rerun_save(void) {
    ct_state_mkdir();
    FILE *f = fopen(ct_state_path(), "w");
    if (!f) return;
    for (int i = 0; i < g_failed_n; i++) fprintf(f, "%s\n", g_failed_ids[i]);
    fclose(f);
}

static int ct_build(const ct_opts *o) {
    if (!o->build_cmd) return 0;
    const char *dim = ct_ansi(CT_DIM), *rs = ct_ansi(CT_RESET);
    printf("\n  %sbuilding: %s%s\n", dim, o->build_cmd, rs);
    fflush(stdout);
    int st = system(o->build_cmd);
    if (st != 0) {
        printf("  %sbuild failed (status %d)%s\n", ct_ansi(CT_RED), st,
               ct_ansi(CT_RESET));
        return -1;
    }
    return 0;
}

static FILE *g_junit;

static void ct_junit_esc(FILE *f, const char *s) {
    if (!s) return;
    for (; *s; s++) {
        switch (*s) {
        case '&': fputs("&amp;", f); break;
        case '<': fputs("&lt;", f); break;
        case '>': fputs("&gt;", f); break;
        case '"': fputs("&quot;", f); break;
        case '\'': fputs("&apos;", f); break;
        default: fputc((unsigned char)*s, f);
        }
    }
}

static void ct_junit_open(const char *path) {
    if (!path) return;
    g_junit = fopen(path, "w");
    if (g_junit) fprintf(g_junit,
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<testsuites>\n");
}

static void ct_junit_test(const ctest_result *r) {
    if (!g_junit) return;
    const char *class = r->file ? r->file : "unknown";
    fprintf(g_junit, "  <testcase classname=\"");
    ct_junit_esc(g_junit, class);
    fprintf(g_junit, "\" name=\"");
    ct_junit_esc(g_junit, r->name);
    fprintf(g_junit, "\" time=\"%.3f\"", r->seconds);
    if (r->status == CT_SKIP || (r->status == CT_FAIL && r->known_id)) {
        fputs(">\n    <skipped/>\n  </testcase>\n", g_junit);
    } else if (r->status != CT_PASS) {
        const char *type = r->status == CT_CRASH ? "crash"
                          : r->status == CT_TIMEOUT ? "timeout" : "failure";
        fputs(">\n    <failure type=\"", g_junit);
        fputs(type, g_junit);
        fputs("\" message=\"", g_junit);
        if (r->fail_count > 0) ct_junit_esc(g_junit, r->failures[0].expr);
        fputs("\">", g_junit);
        for (int i = 0; i < r->fail_count; i++) {
            const ctest_failure *f = &r->failures[i];
            fprintf(g_junit, "%s:%d %s", f->file, f->line, f->expr);
            if (f->case_index >= 0) {
                fprintf(g_junit, " [case %d: %s]", f->case_index,
                        f->case_desc ? f->case_desc : "");
            }
            if (f->msg) fprintf(g_junit, " (%s)", f->msg);
            fputc('\n', g_junit);
        }
        fputs("</failure>\n  </testcase>\n", g_junit);
    } else {
        fputs("/>\n", g_junit);
    }
}

static void ct_junit_close(void) {
    if (!g_junit) return;
    fputs("</testsuites>\n", g_junit);
    fclose(g_junit);
    g_junit = NULL;
}

static void ct_gdb_backtrace(const ct_run *t) {
    char *argv[] = {
        (char *)"gdb", (char *)"-q", (char *)"-batch",
        (char *)"-ex", (char *)"set pagination off",
        (char *)"-ex", (char *)"run",
        (char *)"-ex", (char *)"bt",
        (char *)"--args", (char *)t->bin, (char *)"--run", (char *)t->name,
        NULL,
    };
    char *out = NULL;
    int st = ct_capture("gdb", argv, &out);
    const char *dim = ct_ansi(CT_DIM), *rs = ct_ansi(CT_RESET);
    if (st == -1) {
        printf("    %s(gdb not available; install gdb for backtraces)%s\n",
               dim, rs);
        return;
    }
    char *save = NULL;
    for (char *line = strtok_r(out ? out : "", "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        if (line[0] == '#') printf("      %s%s%s\n", dim, line, rs);
    }
    free(out);
}

static int ct_slot_spawn(ct_slot *s, const ct_run *t, int retries) {
    int fd;
    pid_t pid = ct_spawn(t->bin, t->name, &fd);
    if (pid < 0) return -1;
    s->pid = pid;
    s->fd = fd;
    s->t = t;
    s->started = ct_now();
    s->out = NULL;
    s->outn = 0;
    s->outcap = 0;
    s->retries = retries;
    s->used = 1;
    return 0;
}

static void ct_note_failed(const ct_run *t) {
    char id[8192];
    snprintf(id, sizeof id, "%s\t%s", t->bin, t->name);
    g_failed_ids = realloc(g_failed_ids,
                           (size_t)(g_failed_n + 1) * sizeof *g_failed_ids);
    g_failed_ids[g_failed_n++] = strdup(id);
}

typedef struct ct_state {
    const ctest_reporter *R;
    int passed, failed, crashed, timedout, skipped, known;
    ctest_slow slow[5];
    int nslow;
    int stop;
    int collect;
} ct_state;

static void ct_finalize(const ct_opts *o, ct_state *st, const ct_run *t,
                        const ct_det *d, const ct_slot *s) {
    ctest_result res = {0};
    res.name = t->name;
    res.file = t->bin;
    res.line = t->line;
    res.status = d->status;
    res.skip_reason = d->skip_reason;
    res.seconds = d->seconds;
    res.fail_count = d->nfail;
    res.failures = d->failures;
    res.captured = s->out;
    res.flaky = s->flaky;
    res.known_id = d->known;
    ct_junit_test(&res);
    st->R->result(&res);
    switch (d->status) {
    case CT_PASS: st->passed++; break;
    case CT_SKIP: st->skipped++; break;
    case CT_CRASH:
        st->crashed++;
        if (o->backtrace) ct_gdb_backtrace(t);
        if (o->fail_fast) st->stop = 1;
        break;
    case CT_TIMEOUT: st->timedout++; if (o->fail_fast) st->stop = 1; break;
    case CT_FAIL:
        if (d->known) {
            st->known++;
        } else {
            st->failed++;
            if (o->fail_fast) st->stop = 1;
        }
        break;
    }
    if (d->status != CT_PASS && d->status != CT_SKIP &&
        !(d->status == CT_FAIL && d->known) && st->collect)
        ct_note_failed(t);
    ct_slow_insert(st->slow, &st->nslow, t->name, d->seconds);
}

static int ct_execute(const ct_opts *o, const ct_run *tests, int n, int collect) {
    const ctest_reporter *R = o->tap ? &ct_tap : o->quiet ? &ct_quiet : &ct_pretty;
    int jobs = o->jobs > 0 ? o->jobs : ct_default_jobs();
    if (jobs > n && n > 0) jobs = n;
    double timeout = o->timeout_ms > 0 ? (double)o->timeout_ms / 1000.0 : 0.0;
    g_slow_ms = o->slow_ms;

    g_name_width = 0;
    for (int i = 0; i < n; i++) {
        size_t l = strlen(tests[i].name);
        if (l > (size_t)g_name_width) g_name_width = (int)l;
    }
    g_total = n;
    R->begin(n);
    ct_junit_open(o->junit);

    ct_state st;
    memset(&st, 0, sizeof st);
    st.R = R;
    st.collect = collect;
    double t_start = ct_now();

    ct_slot *slots = calloc((size_t)jobs, sizeof *slots);
    struct pollfd *fds = malloc((size_t)jobs * sizeof *fds);
    size_t next = 0;

    while (next < (size_t)n || !st.stop) {
        for (int i = 0; i < jobs; i++) {
            if (slots[i].used) continue;
            if (next >= (size_t)n || st.stop) continue;
            const ct_run *t = &tests[next];
            if (ct_slot_spawn(&slots[i], t, o->retries) != 0) {
                ctest_result res = {0};
                res.name = t->name;
                res.file = t->bin;
                res.line = t->line;
                res.status = CT_FAIL;
                ctest_failure f = { t->file, t->line,
                                    "failed to spawn test process", NULL,
                                    -1, NULL };
                res.fail_count = 1;
                res.failures = &f;
                ct_junit_test(&res);
                R->result(&res);
                st.failed++;
                if (st.collect) ct_note_failed(t);
                if (o->fail_fast) st.stop = 1;
            }
            next++;
        }
        if (st.stop) break;
        if (next >= (size_t)n) {
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
            const ct_run *t = slots[i].t;
            ct_det d;
            int is_to = 0;
            if (timeout > 0 && ct_now() - slots[i].started >= timeout) {
                kill(slots[i].pid, SIGKILL);
                is_to = 1;
            } else if (!ct_poll_hup(fds, nf, slots[i].fd)) {
                if (ct_poll_ready(fds, nf, slots[i].fd)) ct_drain(&slots[i]);
                continue;
            }
            ct_collect(&slots[i], t, (double)o->timeout_ms, is_to, &d);

            int bad = (d.status == CT_FAIL && !d.known) ||
                      d.status == CT_CRASH ||
                      d.status == CT_TIMEOUT;
            if (bad && slots[i].retries > 0 && !st.stop) {
                slots[i].retries--;
                slots[i].flaky = 1;
                ct_det_free(&d);
                free(slots[i].out);
                slots[i].out = NULL;
                if (ct_slot_spawn(&slots[i], t, slots[i].retries) == 0) {
                    slots[i].used = 1;
                    continue;
                }
                slots[i].used = 0;
                slots[i].flaky = 0;
                ct_det d2;
                memset(&d2, 0, sizeof d2);
                d2.status = CT_FAIL;
                ct_synth_fail(&d2, t, 127);
                ct_finalize(o, &st, t, &d2, &slots[i]);
                ct_det_free(&d2);
                continue;
            }

            ct_finalize(o, &st, t, &d, &slots[i]);
            ct_det_free(&d);
            free(slots[i].out);
            slots[i].out = NULL;
            slots[i].used = 0;
        }
    }

    for (int i = 0; i < jobs; i++) {
        if (!slots[i].used) continue;
        kill(slots[i].pid, SIGKILL);
        waitpid(slots[i].pid, NULL, 0);
        char drain[64];
        while (read(slots[i].fd, drain, sizeof drain) > 0) {}
        close(slots[i].fd);
        free(slots[i].out);
    }
    free(fds);
    free(slots);

    double t_end = ct_now();
    ctest_summary sum;
    memset(&sum, 0, sizeof sum);
    sum.passed = st.passed;
    sum.failed = st.failed;
    sum.crashed = st.crashed;
    sum.timedout = st.timedout;
    sum.skipped = st.skipped;
    sum.known = st.known;
    sum.seconds = t_end - t_start;
    sum.nslow = st.nslow;
    for (int i = 0; i < st.nslow; i++) sum.slow[i] = st.slow[i];
    R->end(&sum);
    ct_junit_close();

    return (st.failed + st.crashed + st.timedout) > 0 ? 1 : 0;
}

typedef struct ct_wfile { char *path; time_t mtime; } ct_wfile;

static ct_wfile *g_wf;
static int g_nwf;
static int g_capwf;
static time_t *g_bin_mtimes;

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

static int ct_watch_poll(const ct_opts *o) {
    ct_wfile *nw = NULL;
    int nn = 0, cap = 0;
    ct_wscan(&nw, &nn, &cap, ".");
    int changed = (nn != g_nwf);
    for (int b = 0; b < o->nbins && !changed; b++) {
        struct stat st;
        time_t m = stat(o->bins[b], &st) == 0 ? st.st_mtime : 0;
        if (m != g_bin_mtimes[b]) changed = 1;
    }
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
    for (int b = 0; b < o->nbins; b++) {
        struct stat st;
        g_bin_mtimes[b] = stat(o->bins[b], &st) == 0 ? st.st_mtime : 0;
    }
    return changed;
}

static int ct_render_list(const ct_bin *bins, int nbins, int total) {
    const char *cy = ct_ansi(CT_CYAN), *dim = ct_ansi(CT_DIM), *rs = ct_ansi(CT_RESET);
    printf("  %s%s%s %sctest v%s%s %s%d test%s%s\n\n",
           cy, ct_glyph(CT_GLYPH_FLASK), rs,
           ct_ansi(CT_BOLD), CT_VERSION, rs,
           dim, total, total == 1 ? "" : "s", rs);
    int w = 0;
    for (int b = 0; b < nbins; b++)
        for (int i = 0; i < bins[b].n; i++) {
            size_t l = strlen(bins[b].tests[i].name);
            if (l > (size_t)w) w = (int)l;
        }
    for (int b = 0; b < nbins; b++) {
        if (bins[b].n == 0) continue;
        printf("  %s%s%s %s%s%s\n", cy, ct_glyph(CT_GLYPH_CUBE), rs, cy,
               bins[b].path, rs);
        for (int i = 0; i < bins[b].n; i++)
            printf("  %-*s  %s%s:%d%s\n", w, bins[b].tests[i].name, dim,
                   bins[b].tests[i].file, bins[b].tests[i].line, rs);
    }
    printf("\n");
    return 0;
}

static void ct_json_esc(const char *s) {
    for (; *s; s++) {
        switch (*s) {
        case '"': fputs("\\\"", stdout); break;
        case '\\': fputs("\\\\", stdout); break;
        case '\n': fputs("\\n", stdout); break;
        case '\t': fputs("\\t", stdout); break;
        case '\r': fputs("\\r", stdout); break;
        default:
            if ((unsigned char)*s < 0x20)
                printf("\\u%04x", (unsigned char)*s);
            else
                putchar(*s);
        }
    }
}

static int ct_render_list_json(const ct_bin *bins, int nbins) {
    printf("[\n");
    int first = 1;
    for (int b = 0; b < nbins; b++) {
        for (int i = 0; i < bins[b].n; i++) {
            if (!first) printf(",\n");
            first = 0;
            printf("  {");
            printf("\"binary\": \"");
            ct_json_esc(bins[b].path);
            printf("\", \"name\": \"");
            ct_json_esc(bins[b].tests[i].name);
            printf("\", \"file\": \"");
            ct_json_esc(bins[b].tests[i].file);
            printf("\", \"line\": %d}", bins[b].tests[i].line);
        }
    }
    printf("\n]\n");
    return 0;
}

static int ct_collect_all(const ct_opts *o, ct_bin *bins, ct_run **runs_out,
                          int *n_out) {
    int total = 0;
    for (int b = 0; b < o->nbins; b++) total += bins[b].n;
    ct_run *runs = total > 0 ? malloc((size_t)total * sizeof *runs) : NULL;
    int n = 0;
    for (int b = 0; b < o->nbins; b++) {
        for (int i = 0; i < bins[b].n; i++) {
            runs[n].bin = bins[b].path;
            runs[n].name = bins[b].tests[i].name;
            runs[n].file = bins[b].tests[i].file;
            runs[n].line = bins[b].tests[i].line;
            n++;
        }
    }
    *runs_out = runs;
    *n_out = n;
    return n;
}

static void ct_shuffle_runs(ct_run *arr, size_t n) {
    for (size_t i = n - 1; i > 0; i--) {
        size_t j = ct_rand() % (i + 1);
        ct_run t = arr[i];
        arr[i] = arr[j];
        arr[j] = t;
    }
}

int main(int argc, char **argv) {
    ct_opts o;
    memset(&o, 0, sizeof o);
    o.seed = (unsigned)time(NULL);
    int bad = 0;
    for (int i = 1; i < argc && !bad; i++) {
        const char *a = argv[i];
        const char *v;
        int r;
        if (strcmp(a, "--help") == 0) { ct_usage(stdout); return 0; }
        else if (strcmp(a, "--list") == 0) o.list = 1;
        else if (strcmp(a, "--json") == 0) o.json = 1;
        else if (strcmp(a, "--rerun-failed") == 0) o.rerun_failed = 1;
        else if (strcmp(a, "--backtrace") == 0) o.backtrace = 1;
        else if (strcmp(a, "--only") == 0) o.only = 1;
        else if (strcmp(a, "--fail-fast") == 0) o.fail_fast = 1;
        else if (strcmp(a, "--shuffle") == 0) o.shuffle = 1;
        else if (strcmp(a, "--watch") == 0) o.watch = 1;
        else if (strcmp(a, "--quiet") == 0 || strcmp(a, "-q") == 0) o.quiet = 1;
        else if (strcmp(a, "--no-color") == 0) g_force_no_color = 1;
        else if (strcmp(a, "--pretty") == 0) o.tap = 0;
        else if (strcmp(a, "--tap") == 0) o.tap = 1;
        else if (strcmp(a, "-j") == 0 && i + 1 < argc) o.jobs = atoi(argv[++i]);
        else if (a[0] == '-' && a[1] == 'j' && a[2] != '\0') o.jobs = atoi(a + 2);
        else if ((r = ct_take(a, "--jobs", &v, &i, argc, argv)) != 0) {
            if (r < 0) bad = 1; else o.jobs = atoi(v);
        }
        else if ((r = ct_take(a, "--timeout", &v, &i, argc, argv)) != 0) {
            if (r < 0) bad = 1; else o.timeout_ms = atoi(v);
        }
        else if ((r = ct_take(a, "--retries", &v, &i, argc, argv)) != 0) {
            if (r < 0) bad = 1; else o.retries = atoi(v);
        }
        else if ((r = ct_take(a, "--slow", &v, &i, argc, argv)) != 0) {
            if (r < 0) bad = 1; else o.slow_ms = atoi(v);
        }
        else if ((r = ct_take(a, "--junit", &v, &i, argc, argv)) != 0) {
            if (r < 0) bad = 1; else o.junit = v;
        }
        else if ((r = ct_take(a, "--build-cmd", &v, &i, argc, argv)) != 0) {
            if (r < 0) bad = 1; else o.build_cmd = v;
        }
        else if ((r = ct_take(a, "--seed", &v, &i, argc, argv)) != 0) {
            if (r < 0) bad = 1; else o.seed = (unsigned)strtoul(v, NULL, 10);
        }
        else if ((r = ct_take(a, "--tags", &v, &i, argc, argv)) != 0 ||
                 (r = ct_take(a, "--tag", &v, &i, argc, argv)) != 0) {
            if (r < 0) bad = 1; else o.tags = v;
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
        else if ((r = ct_take(a, "--filter", &v, &i, argc, argv)) != 0) {
            if (r < 0) bad = 1;
            else if (o.nfilters < CT_MAX_FILTERS) o.filters[o.nfilters++] = v;
            else bad = 1;
        }
        else if (a[0] == '-') {
            fprintf(stderr, "c-test: unknown option %s\n", a);
            bad = 1;
        }
        else if (o.nbins < CT_MAX_BINS) {
            o.bins[o.nbins++] = a;
        }
        else {
            fprintf(stderr, "c-test: too many test binaries\n");
            bad = 1;
        }
    }
    if (bad) { ct_usage(stderr); return 2; }
    if (o.watch && o.rerun_failed) {
        fprintf(stderr, "c-test: --rerun-failed cannot be combined with --watch\n");
        return 2;
    }
    if (o.nbins == 0) {
        fprintf(stderr, "c-test: no test binaries given\n");
        ct_usage(stderr);
        return 2;
    }

    ct_bin *bins = calloc((size_t)o.nbins, sizeof *bins);
    int valid = 0;
    for (int b = 0; b < o.nbins; b++) {
        bins[b].path = o.bins[b];
        ct_bin_collect(&o, &bins[b]);
        if (bins[b].n > 0) valid++;
    }
    if (valid == 0) {
        fprintf(stderr, "c-test: no usable test binaries\n");
        for (int b = 0; b < o.nbins; b++) ct_bin_free(&bins[b]);
        free(bins);
        return 1;
    }

    if (o.list) {
        int total = 0;
        for (int b = 0; b < o.nbins; b++) total += bins[b].n;
        int rc = o.json ? ct_render_list_json(bins, o.nbins)
                        : ct_render_list(bins, o.nbins, total);
        for (int b = 0; b < o.nbins; b++) ct_bin_free(&bins[b]);
        free(bins);
        return rc;
    }

    if (o.watch) {
        g_bin_mtimes = malloc((size_t)o.nbins * sizeof *g_bin_mtimes);
        ct_watch_poll(&o);
        for (;;) {
            if (ct_build(&o) == 0) {
                ct_run *runs = NULL;
                int n = 0;
                ct_collect_all(&o, bins, &runs, &n);
                if (o.shuffle) {
                    ct_seed(o.seed);
                    ct_shuffle_runs(runs, (size_t)n);
                }
                int rc = ct_execute(&o, runs, n, 0);
                (void)rc;
                free(runs);
            }
            printf("\n  %sWatching for changes (Ctrl-C to stop)%s\n",
                   ct_ansi(CT_DIM), ct_ansi(CT_RESET));
            fflush(stdout);
            while (!ct_watch_poll(&o)) {
                struct timespec ts = { 0, 200000000L };
                nanosleep(&ts, NULL);
            }
            if (ct_color()) printf("\033[2J\033[H");
            fflush(NULL);
            for (int b = 0; b < o.nbins; b++) ct_bin_free(&bins[b]);
            for (int b = 0; b < o.nbins; b++) {
                bins[b].path = o.bins[b];
                ct_bin_collect(&o, &bins[b]);
            }
        }
    }

    ct_run *runs = NULL;
    int n = 0;
    ct_collect_all(&o, bins, &runs, &n);

    if (o.rerun_failed) {
        if (ct_rerun_load() != 0) {
            fprintf(stderr, "c-test: no saved failures "
                    "(run the suite once first)\n");
            free(runs);
            for (int b = 0; b < o.nbins; b++) ct_bin_free(&bins[b]);
            free(bins);
            return 2;
        }
        ct_run *nr = NULL;
        int nn = 0;
        for (int i = 0; i < n; i++) {
            char id[8192];
            snprintf(id, sizeof id, "%s\t%s", runs[i].bin, runs[i].name);
            for (int j = 0; j < g_rerun_n; j++) {
                if (strcmp(id, g_rerun_set[j]) == 0) {
                    nr = realloc(nr, (size_t)(nn + 1) * sizeof *nr);
                    nr[nn++] = runs[i];
                    break;
                }
            }
        }
        free(runs);
        runs = nr;
        n = nn;
        for (int i = 0; i < g_rerun_n; i++) free(g_rerun_set[i]);
        free(g_rerun_set);
        g_rerun_set = NULL;
        g_rerun_n = 0;
        if (n == 0) {
            fprintf(stderr, "c-test: no saved failures match the given binaries\n");
            for (int b = 0; b < o.nbins; b++) ct_bin_free(&bins[b]);
            free(bins);
            return 1;
        }
        printf("%sre-running %d saved failure%s%s\n", ct_ansi(CT_DIM), n,
               n == 1 ? "" : "s", ct_ansi(CT_RESET));
        fflush(stdout);
    }

    if (o.shuffle) {
        ct_seed(o.seed);
        ct_shuffle_runs(runs, (size_t)n);
    }
    int rc = ct_execute(&o, runs, n, 1);
    free(runs);

    ct_rerun_save();
    for (int i = 0; i < g_failed_n; i++) free(g_failed_ids[i]);
    free(g_failed_ids);
    g_failed_ids = NULL;
    g_failed_n = 0;

    for (int b = 0; b < o.nbins; b++) ct_bin_free(&bins[b]);
    free(bins);
    return rc;
}
