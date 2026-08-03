#include "ctest.h"
#include <stdlib.h>
#include <unistd.h>

it(c_segv, "should segfault", .tags = {"crash"})
{
    *(volatile int *)0 = 1;
}



it(c_abort, "should abort", .tags = {"crash"})
{
    abort();
}

// it(c_hang, "should hang forever", .tags = {"hang", "slow"})
// {
//     for (;;) usleep(100000);
// }

it(c_pass, "should be a normal passing test", .tags = {"crash"})
{
    expect(2 + 2 == 4);
}

const struct ctest_test *const ctest_suite[] = {
    CT_IT(c_segv),
    CT_IT(c_abort),
    CT_IT(c_pass),
    0
};
