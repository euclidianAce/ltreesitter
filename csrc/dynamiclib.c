
#include <stddef.h>

#include <ltreesitter/dynamiclib.h>

bool ltreesitter_open_dynamic_lib(const char *name, ltreesitter_Dynlib **handle) {
#ifdef _WIN32
	*handle = LoadLibrary(name);
	return *handle != NULL;
#elif LTREESITTER_USE_LIBUV
	*handle = malloc(sizeof(ltreesitter_Dynlib));
	return uv_dlopen(name, *handle) == 0;
#else
	*handle = dlopen(name, RTLD_NOW | RTLD_LOCAL);
	return *handle != NULL;
#endif
}

void *ltreesitter_dynamic_sym(ltreesitter_Dynlib *handle, const char *sym_name) {
#ifdef _WIN32
	FARPROC sym = GetProcAddress(handle, sym_name);
	return *(void**)(&sym);
#elif LTREESITTER_USE_LIBUV
	void *sym = NULL;
	if (uv_dlsym(handle, sym_name, &sym) == 0) {
		return sym;
	}
	return NULL;
#else
	return dlsym(handle, sym_name);
#endif
}

void ltreesitter_close_dynamic_lib(ltreesitter_Dynlib *handle) {
#ifdef _WIN32
	FreeLibrary(handle);
#elif LTREESITTER_USE_LIBUV
	uv_dlclose(handle);
	free(handle);
#else
	dlclose(handle);
#endif
}

#define UNUSED(x) ((void)(x))

const char *ltreesitter_dynamic_lib_error(ltreesitter_Dynlib *handle) {
#ifdef _WIN32
	// Does windows have an equivalent dlerror?
	UNUSED(handle);
	return "Error in LoadLibrary";
#elif LTREESITTER_USE_LIBUV
	return uv_dlerror(handle);
#else
	UNUSED(handle);
	return dlerror();
#endif
}
