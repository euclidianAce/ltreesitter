#ifndef DYNAMICLIB_H
#define DYNAMICLIB_H

#ifdef LTREESITTER_USE_LIBUV
#include <uv.h>
typedef uv_lib_t ltreesitter_Dynlib;
#else
typedef void *ltreesitter_Dynlib;
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

bool ltreesitter_open_dynamic_lib(const char *name, ltreesitter_Dynlib *handle, const char **out_error);
void *ltreesitter_dynamic_sym(ltreesitter_Dynlib *handle, const char *sym_name);
void ltreesitter_close_dynamic_lib(ltreesitter_Dynlib *handle);

#endif
