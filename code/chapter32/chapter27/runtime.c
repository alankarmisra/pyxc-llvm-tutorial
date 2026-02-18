#include <stdio.h>
#include <stdint.h>

#ifdef _WIN32
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

DLLEXPORT int8_t putchari8(int8_t X) {
  fputc((unsigned char)X, stderr);
  return 0;
}
DLLEXPORT int16_t putchari16(int16_t X) {
  fputc((unsigned char)X, stderr);
  return 0;
}
DLLEXPORT int32_t putchari32(int32_t X) {
  fputc((unsigned char)X, stderr);
  return 0;
}
DLLEXPORT int64_t putchari64(int64_t X) {
  fputc((unsigned char)X, stderr);
  return 0;
}
DLLEXPORT uint8_t putcharu8(uint8_t X) {
  fputc((unsigned char)X, stderr);
  return 0;
}
DLLEXPORT uint16_t putcharu16(uint16_t X) {
  fputc((unsigned char)X, stderr);
  return 0;
}
DLLEXPORT uint32_t putcharu32(uint32_t X) {
  fputc((unsigned char)X, stderr);
  return 0;
}
DLLEXPORT uint64_t putcharu64(uint64_t X) {
  fputc((unsigned char)X, stderr);
  return 0;
}
DLLEXPORT float putcharf32(float X) {
  fputc((unsigned char)X, stderr);
  return 0;
}
DLLEXPORT double putcharf64(double X) {
  fputc((unsigned char)X, stderr);
  return 0;
}
DLLEXPORT int64_t putchari(int64_t X) {
  fputc((unsigned char)X, stderr);
  return 0;
}
DLLEXPORT double putchard(double X) {
  fputc((unsigned char)X, stderr);
  return 0;
}
DLLEXPORT double printchard(double X) {
  fputc((unsigned char)X, stderr);
  return 0;
}

DLLEXPORT int8_t printi8(int8_t X) {
  fprintf(stderr, "%d", (int)X);
  return 0;
}
DLLEXPORT int16_t printi16(int16_t X) {
  fprintf(stderr, "%d", (int)X);
  return 0;
}
DLLEXPORT int32_t printi32(int32_t X) {
  fprintf(stderr, "%d", X);
  return 0;
}
DLLEXPORT int64_t printi64(int64_t X) {
  fprintf(stderr, "%lld", (long long)X);
  return 0;
}
DLLEXPORT uint8_t printu8(uint8_t X) {
  fprintf(stderr, "%u", (unsigned)X);
  return 0;
}
DLLEXPORT uint16_t printu16(uint16_t X) {
  fprintf(stderr, "%u", (unsigned)X);
  return 0;
}
DLLEXPORT uint32_t printu32(uint32_t X) {
  fprintf(stderr, "%u", X);
  return 0;
}
DLLEXPORT uint64_t printu64(uint64_t X) {
  fprintf(stderr, "%llu", (unsigned long long)X);
  return 0;
}
DLLEXPORT float printfloat32(float X) {
  fprintf(stderr, "%f", (double)X);
  return 0;
}
DLLEXPORT double printfloat64(double X) {
  fprintf(stderr, "%f", X);
  return 0;
}
DLLEXPORT int64_t printi(int64_t X) {
  fprintf(stderr, "%lld", (long long)X);
  return 0;
}
DLLEXPORT double printd(double X) {
  fprintf(stderr, "%f", X);
  return 0;
}
