#include "ctest.h"
#include <string.h>
#include <time.h>
#include <unistd.h>

static int add(int a, int b) {
    return a + b;
}

static int pow2(int n) {
    int r = 1;
    for (int i = 0; i < n; i++) r *= 2;
    return r;
}

static int is_prime(int n) {
    if (n < 2) return 0;
    for (int d = 2; d * d <= n; d++)
        if (n % d == 0) return 0;
    return 1;
}

it(e_add, "should add numbers")
{
    expect(add(1, 2) == 3);
}

it(e_prime, "should detect primes", .tags = {"math", "fast"})
{
    expect(is_prime(2));
    expect(is_prime(97));
    expect(!is_prime(1));
}

it(e_strcmp, "should compare strings", .tags = {"fast"})
{
    expect(strcmp("hello", "hello") == 0);
}

it(e_mismatch, "should print mismatched values", .tags = {"demo"})
{
    expect(add(1, 2) == 4);
}

it(e_float, "should be close to floating values", .platforms = "linux", .tags = {"math"})
{
    expect(0.1 + 0.2 > 0.3);
}

it(e_range, "should check ranges", .platforms = "linux", .tags = {"math"})
{
    expect(7 >= 1 && 7 <= 10);
}

it(e_pow2, "should compute powers", .tags = {"math", "fast"})
{
    expect(pow2(2) == 4);
    expect(pow2(8) == 256);
}

it(e_sleep, "should sleep briefly", .tags = {"slow"})
{
    struct timespec ts = { 0, 120000000L };
    nanosleep(&ts, NULL);
    expect(1 == 1);
}

it(e_win, "should run only on windows", .platforms = "windows")
{
    expect(0);
}

it(e_gnu99, "should require the gnu99 dialect", .std = "gnu99")
{
    expect(1 == 1);
}

it(e_c11, "should run under the c11 dialect", .std = "c11")
{
    expect(1 == 1);
}

it(e_skip, "should be manually skipped", .skip = 1)
{
    expect(0);
}

it(e_all, "should cover every assertion style with one expect")
{
    expect(add(2, 3) == 5);
    expect(strcmp("hi", "hi") == 0);
    expect(0.1 + 0.2 > 0.3);
    expect(!is_prime(100));
}

const struct ctest_test *const ctest_suite[] = {
    CT_IT(e_add),
    CT_IT(e_prime),
    CT_IT(e_strcmp),
    CT_IT(e_mismatch),
    CT_IT(e_float),
    CT_IT(e_range),
    CT_IT(e_pow2),
    CT_IT(e_sleep),
    CT_IT(e_win),
    CT_IT(e_gnu99),
    CT_IT(e_c11),
    CT_IT(e_skip),
    CT_IT(e_all),
    0
};
