#include "ctest.h"
#include <stdio.h>

int real_logic(void) {
    return 1 + 1;
}

it(a_logic, "should exercise real logic", .tags = {"app"})
{
    expect(real_logic() == 2);
}

const struct ctest_test *const ctest_suite[] = {
    CT_IT(a_logic),
    0
};

int main(void) {
    printf("app is running\n");
    return 0;
}
