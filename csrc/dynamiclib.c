
#include <stddef.h>

#include <ltreesitter/dynamiclib.h>

bool ltreesitter_open_dynamic_lib(const char *name, ltreesitter_Dynlib *handle, const char **out_error) {
#ifdef _WIN32
	*handle = LoadLibrary(name);
	if (!lib) *out_error = GetLastError();
#elif LTREESITTER_USE_LIBUV
	if (uv_dlopen(name, handle) != 0) {
		*out_error = uv_dlerror(handle);
	} else if (!*handle) {
		*out_error = "Out of memory";
	}
#else
	*handle = dlopen(name, RTLD_NOW | RTLD_LOCAL);
	if (!*handle)
		*out_error = dlerror();
#endif
	return !!*handle;
}

void *ltreesitter_dynamic_sym(ltreesitter_Dynlib *handle, const char *sym_name) {
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

void ltreesitter_close_dynamic_lib(ltreesitter_Dynlib *handle) {
#ifdef _WIN32
	FreeLibrary(handle);
#elif LTREESITTER_USE_LIBUV
	uv_dlclose(handle);
#else
	dlclose(handle);
#endif
}
