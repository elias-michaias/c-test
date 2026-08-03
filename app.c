#include "ctest.h"
#include <stdio.h>

int real_logic(void) {
    return 1 + 1;
}

it("should exercise real logic", .tags = {"app"})
{
    expect(real_logic() == 2);
}

int main(void) {
    printf("app is running\n");
    return 0;
}