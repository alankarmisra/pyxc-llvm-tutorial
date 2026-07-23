/* runtime.c — minimal runtime for linking pyxc-emitted object files in tests.
 *
 * Provides the same printd/putchard functions that are baked into the pyxc
 * binary for JIT mode. When --emit obj produces a standalone .o, tests link
 * against this file so the emitted code can produce observable output.
 */

/* Avoid relying on system headers in test environments. */
int printf(const char *format, ...);
int putchar(int c);

double printd(double x) {
  printf("%f\n", x);
  return 0.0;
}

double putchard(double x) {
  putchar((char)x);
  return 0.0;
}
