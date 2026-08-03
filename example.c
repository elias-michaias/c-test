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

it("should add numbers")
{
    expect(add(1, 2) == 3);
}

it("should detect primes", .tags = {"math", "fast"})
{
    expect(is_prime(2));
    expect(is_prime(97));
    expect(!is_prime(1));
}

it("should compare strings", .tags = {"fast"})
{
    expect(strcmp("hello", "hello") == 0);
}

it("should print mismatched values", .tags = {"demo"})
{
    expect(add(1, 2) == 4);
}

it("should be close to floating values", .platforms = "linux", .tags = {"math"})
{
    expect(0.1 + 0.2 > 0.3);
}

it("should check ranges", .platforms = "linux", .tags = {"math"})
{
    expect(7 >= 1 && 7 <= 10);
}

it("should compute powers", .tags = {"math", "fast"})
{
    expect(pow2(2) == 4);
    expect(pow2(8) == 256);
}

it("should sleep briefly", .tags = {"slow"})
{
    struct timespec ts = { 0, 120000000L };
    nanosleep(&ts, NULL);
    expect(1 == 1);
}

it("should run only on windows", .platforms = "windows")
{
    expect(0);
}

it("should require the gnu99 dialect", .std = "gnu99")
{
    expect(1 == 1);
}

it("should run under the c11 dialect", .std = "c11")
{
    expect(1 == 1);
}

it("should be manually skipped", .skip = 1)
{
    expect(0);
}

it("should cover every assertion style with one expect")
{
    expect(add(2, 3) == 5);
    expect(strcmp("hi", "hi") == 0);
    expect(0.1 + 0.2 > 0.3);
    expect(!is_prime(100));
}