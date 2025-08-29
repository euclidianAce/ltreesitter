#ifndef DYNAMICLIB_H
#define DYNAMICLIB_H

#include <stdbool.h>

#ifdef LTREESITTER_USE_LIBUV
#include <uv.h>
typedef uv_lib_t Dynlib;
#else
typedef struct {
	void *opaque_handle;
} Dynlib;
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#endif

#if defined _WIN32
#define LTREESITTER_DL_EXT "dll"
#elif defined __APPLE__
#define LTREESITTER_DL_EXT "dylib"
#else
#define LTREESITTER_DL_EXT "so"
#endif

bool dynlib_open(char const *name, Dynlib *handle, char const **out_error);
void *dynlib_sym(Dynlib *handle, char const *sym_name);
void dynlib_close(Dynlib *handle);

#endif
