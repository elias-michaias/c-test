#include "ctest.h"
#include <string>
#include <vector>
#include <sstream>

it("basic c++ expect")
{
    expect(1 + 1 == 2);
    expect(true);
}

it("std::string operations")
{
    std::string s = "hello, world";
    expect(s.size() == 12);
    expect(s.find("world") != std::string::npos);
    expect(s.substr(0, 5) == "hello");
}

it("std::vector operations")
{
    std::vector<int> v;
    v.push_back(1);
    v.push_back(2);
    v.push_back(3);
    expect(v.size() == 3);
    expect(v[0] + v[1] + v[2] == 6);
}

it("std::ostringstream")
{
    std::ostringstream oss;
    oss << "value=" << 42;
    expect(oss.str() == "value=42");
}

typedef struct { int a; int b; int sum; } add_case;
static const add_case add_cases[] = {
    { 1,  2,  3  },
    { 10, 20, 30 },
    { -1, 1,  0  },
    { 0,  0,  0  },
};
it("parameterized addition", cases(add_case, add_cases))
{
    expect(it.a + it.b == it.sum);
}

typedef struct { const char *input; int expected_len; } str_case;
static const str_case str_cases[] = {
    { "hello",  5 },
    { "world",  5 },
    { "",       0 },
    { "c-test", 6 },
};
it("parameterized string length", cases(str_case, str_cases))
{
    std::string s(it.input);
    expect((int)s.size() == it.expected_len);
}

it("tagged as cpp", .tags = {"cpp", "lang"})
{
    expect(sizeof(int) > 0);
}

it("skipped c++ test", .skip = 1)
{
    expect(false);
}
