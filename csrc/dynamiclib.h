#ifndef DYNAMICLIB_H
#define DYNAMICLIB_H

#ifdef LTREESITTER_USE_LIBUV
#include <uv.h>
typedef uv_lib_t dl_handle;
#else
typedef void dl_handle;
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#endif

#ifdef _WIN32
#define DL_EXT "dll"
#else
#define DL_EXT "so"
#endif

#include <stdbool.h>

bool open_dynamic_lib(const char *name, dl_handle **handle);
void *dynamic_sym(dl_handle *handle, const char *sym_name);
void close_dynamic_lib(dl_handle *handle);
const char *dynamic_lib_error(dl_handle *handle);

#endif
