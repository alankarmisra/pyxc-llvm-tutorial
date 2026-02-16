#include <stdio.h>
#include <stdint.h>

// File I/O wrappers for pyxc
// These are mostly direct wrappers around C stdio functions

#ifdef __cplusplus
extern "C" {
#endif

// Open file
void* pyxc_fopen(const char* path, const char* mode) {
    return (void*)fopen(path, mode);
}

// Close file
int32_t pyxc_fclose(void* file) {
    return fclose((FILE*)file);
}

// Get character
int32_t pyxc_fgetc(void* file) {
    return fgetc((FILE*)file);
}

// Unget character
int32_t pyxc_ungetc(int32_t ch, void* file) {
    return ungetc(ch, (FILE*)file);
}

// Read data
int64_t pyxc_fread(void* ptr, int64_t size, int64_t count, void* file) {
    return (int64_t)fread(ptr, (size_t)size, (size_t)count, (FILE*)file);
}

// Write data
int64_t pyxc_fwrite(const void* ptr, int64_t size, int64_t count, void* file) {
    return (int64_t)fwrite(ptr, (size_t)size, (size_t)count, (FILE*)file);
}

// Check end of file
int32_t pyxc_feof(void* file) {
    return feof((FILE*)file);
}

// Get file position
int64_t pyxc_ftell(void* file) {
    return (int64_t)ftell((FILE*)file);
}

// Set file position
int32_t pyxc_fseek(void* file, int64_t offset, int32_t whence) {
    return fseek((FILE*)file, (long)offset, whence);
}

// Flush file
int32_t pyxc_fflush(void* file) {
    return fflush((FILE*)file);
}

#ifdef __cplusplus
} // extern "C"
#endif
