# c-test

A unit-testing library for C that makes tests stupid easy.
Supports C99 on Clang, GCC, and MSVC. Supports C++ 11 on Clang and GCC, with C++ 20 on MSVC.

```c 
// test.c
#include <ctest.h>

it("should work") {
    expect(2 + 2 == 4);
}

it("should fail") {
    expect(2 + 2 == 5);
}
```

```bash
cc test.c -o test -DCTEST
c-test ./test
```

