#include "../include/runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/// putchard - putchar that takes a double and returns 0.
double putchard(double X) {
  fputc((char)X, stderr);
  return 0;
}

/// printd - printf that takes a double prints it as "%f\n", returning 0.
double printd(double X) {
  fprintf(stderr, "%f", X);
  return 0;
}

/// printlf - print a newline to stderr, returning 0.
double printlf(void) {
  fputc('\n', stderr);
  return 0;
}

/// flushd - flush stderr, returning 0.
double flushd(void) {
  fflush(stderr);
  return 0;
}

/// seedrand - seed libc RNG and return 0.
double seedrand(double Seed) {
  if (Seed < 0)
    Seed = -Seed;
  srand((unsigned int)Seed);
  return 0;
}

/// randd - random value in [0, MaxExclusive), or 0 when MaxExclusive <= 0.
double randd(double MaxExclusive) {
  if (MaxExclusive <= 0)
    return 0;
  return ((double)rand() / ((double)RAND_MAX + 1.0)) * MaxExclusive;
}

/// clockms - wall clock time in milliseconds since Unix epoch.
double clockms(void) {
  struct timespec TS;
  if (timespec_get(&TS, TIME_UTC) != TIME_UTC)
    return 0;
  return (double)TS.tv_sec * 1000.0 + (double)TS.tv_nsec / 1000000.0;
}
