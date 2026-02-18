#ifndef PYXC_RUNTIME_H
#define PYXC_RUNTIME_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
#define PYXC_RUNTIME_EXPORT __declspec(dllexport)
#else
#define PYXC_RUNTIME_EXPORT
#endif

PYXC_RUNTIME_EXPORT double putchard(double X);
PYXC_RUNTIME_EXPORT double printd(double X);
PYXC_RUNTIME_EXPORT double printlf(void);
PYXC_RUNTIME_EXPORT double flushd(void);
PYXC_RUNTIME_EXPORT double seedrand(double Seed);
PYXC_RUNTIME_EXPORT double randd(double MaxExclusive);
PYXC_RUNTIME_EXPORT double clockms(void);

#ifdef __cplusplus
}
#endif

#endif // PYXC_RUNTIME_H
