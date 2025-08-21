#ifndef DYNAMICLIB_H
#define DYNAMICLIB_H

#ifdef LTREESITTER_USE_LIBUV
#include <uv.h>
typedef uv_lib_t Dynlib;
#else
typedef void *Dynlib;
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

#include <stdbool.h>

bool dynlib_open(const char *name, Dynlib *handle, const char **out_error);
void *dynlib_sym(Dynlib *handle, const char *sym_name);
void dynlib_close(Dynlib *handle);

#endif
