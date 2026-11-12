#pragma once

// A deliberately tiny test harness (docs/decisions: test framework). No
// macros that register tests behind your back, no global state: each test
// file has a register_*_tests() function that adds its tests to a Registry.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace test {

class Context {
 public:
  void check(bool ok, const char* expression, const char* file, int line) {
    ++m_checks;
    if (ok) return;
    ++m_failures;
    std::printf("    FAILED %s:%d: %s\n", file, line, expression);
  }
  int failures() const { return m_failures; }
  int checks() const { return m_checks; }

 private:
  int m_checks = 0;
  int m_failures = 0;
};

using TestFn = void (*)(Context& ctx);

class Registry {
 public:
  void add(const char* name, TestFn fn) { m_tests.push_back(Test{name, fn}); }

  // Runs every test whose name starts with `prefix`. Returns the number of
  // failed tests.
  int run(const char* prefix) {
    int failed = 0;
    int ran = 0;
    for (const Test& t : m_tests) {
      if (std::strncmp(t.name, prefix, std::strlen(prefix)) != 0) continue;
      ++ran;
      Context ctx;
      t.fn(ctx);
      const bool ok = ctx.failures() == 0;
      std::printf("[%s] %s (%d checks)\n", ok ? " OK " : "FAIL", t.name, ctx.checks());
      if (!ok) ++failed;
    }
    std::printf("%d of %d tests passed\n", ran - failed, ran);
    if (ran == 0) {
      std::printf("no tests match '%s'\n", prefix);
      return 1;
    }
    return failed;
  }

 private:
  struct Test {
    const char* name;
    TestFn fn;
  };
  std::vector<Test> m_tests;
};

}  // namespace test

// Each test function names its parameter `ctx`.
#define CHECK(expression) ctx.check(static_cast<bool>(expression), #expression, __FILE__, __LINE__)
#define CHECK_NEAR(a, b, tolerance) \
  ctx.check(std::fabs(static_cast<double>(a) - static_cast<double>(b)) <= (tolerance), #a " ~= " #b, __FILE__, __LINE__)
