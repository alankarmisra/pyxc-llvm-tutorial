#include <cstdio>

static long long fib(long long n) {
  if (n < 2) return n;
  return fib(n - 1) + fib(n - 2);
}

int main() {
  long long n = 41;
  long long result = fib(n);
  std::printf("%lld\n", result);
  return 0;
}
