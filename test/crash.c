#include "ctest.h"
#include <stdlib.h>
#ifndef _WIN32
#include <unistd.h>
#endif

it("should segfault", .signal = SIGSEGV, .tags = {"crash"})
{
    *(volatile int *)0 = 1;
}

it("should abort", .abort = 1, .tags = {"crash"})
{
    abort();
}

// it("should hang forever", .tags = {"hang", "slow"})
// {
//     for (;;) usleep(100000);
// }

it("should be a normal passing test", .tags = {"crash"})
{
    expect(2 + 2 == 4);
}