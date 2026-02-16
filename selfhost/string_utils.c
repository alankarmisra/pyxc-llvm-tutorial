#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>

// Most string functions are already in C standard library,
// but we provide wrappers for consistency and ease of use from pyxc

#ifdef __cplusplus
extern "C" {
#endif

// String length
int64_t pyxc_strlen(const char* s) {
    return (int64_t)strlen(s);
}

// String compare
int32_t pyxc_strcmp(const char* a, const char* b) {
    return strcmp(a, b);
}

// String copy
char* pyxc_strcpy(char* dest, const char* src) {
    return strcpy(dest, src);
}

// String concatenate
char* pyxc_strcat(char* dest, const char* src) {
    return strcat(dest, src);
}

// String duplicate (allocates memory)
char* pyxc_strdup(const char* s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char* dup = (char*)malloc(len);
    if (dup) {
        memcpy(dup, s, len);
    }
    return dup;
}

// Find character in string
char* pyxc_strchr(const char* s, int ch) {
    return strchr(s, ch);
}

// String copy with limit
char* pyxc_strncpy(char* dest, const char* src, int64_t n) {
    return strncpy(dest, src, (size_t)n);
}

// String concatenate with limit
char* pyxc_strncat(char* dest, const char* src, int64_t n) {
    return strncat(dest, src, (size_t)n);
}

// Character classification
int32_t pyxc_isalpha(int32_t ch) {
    return isalpha(ch);
}

int32_t pyxc_isdigit(int32_t ch) {
    return isdigit(ch);
}

int32_t pyxc_isalnum(int32_t ch) {
    return isalnum(ch);
}

int32_t pyxc_isspace(int32_t ch) {
    return isspace(ch);
}

int32_t pyxc_toupper(int32_t ch) {
    return toupper(ch);
}

int32_t pyxc_tolower(int32_t ch) {
    return tolower(ch);
}

#ifdef __cplusplus
} // extern "C"
#endif
