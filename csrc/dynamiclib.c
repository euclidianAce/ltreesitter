
#include <stddef.h>

#include "dynamiclib.h"

bool dynlib_open(char const *name, Dynlib *handle, char const **out_error) {
#ifdef _WIN32
	*handle = LoadLibrary(name);
	if (!*handle) {
		*out_error = GetLastError();
		return false;
	}
#elif LTREESITTER_USE_LIBUV
	if (uv_dlopen(name, handle) != 0) {
		*out_error = uv_dlerror(handle);
		return false;
	} else if (!*handle) {
		*out_error = "Out of memory";
		return false;
	}
#else
	*handle = dlopen(name, RTLD_NOW | RTLD_LOCAL);
	if (!*handle) {
		*out_error = dlerror();
		return false;
	}
#endif
	return true;
}

void *dynlib_sym(Dynlib *handle, char const *sym_name) {
#ifdef _WIN32
	FARPROC sym = GetProcAddress(*handle, sym_name);
	return *(void**)(&sym);
#elif LTREESITTER_USE_LIBUV
	void *sym = NULL;
	if (uv_dlsym(handle, sym_name, &sym) == 0) {
		return sym;
	}
	return NULL;
#else
	return dlsym(*handle, sym_name);
#endif
}

void dynlib_close(Dynlib *handle) {
#ifdef _WIN32
	FreeLibrary(handle);
#elif LTREESITTER_USE_LIBUV
	uv_dlclose(handle);
#else
	dlclose(handle);
#endif
}
