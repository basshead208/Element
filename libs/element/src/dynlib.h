
#ifndef EL_DYNLIB_H
#define EL_DYNLIB_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
#include "dlfcn-win32.h"
#else
#include <dlfcn.h>
#endif

static void* element_openlib (const char* path) {
    return dlopen (path, RTLD_LOCAL | RTLD_LAZY);
}

static void element_closelib (void* handle) {
    dlclose (handle);
}

static void* element_getsym (void* handle, const char* f) {
    return dlsym (handle, f);
}

#ifdef __cplusplus
}
#endif

#endif
