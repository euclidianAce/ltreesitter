
#include <stddef.h>

#include <ltreesitter/dynamiclib.h>

#ifdef _WIN32
#define DL_EXT "dll"
#else
#define DL_EXT "so"
#endif

bool open_dynamic_lib(const char *name, dl_handle **handle) {
#ifdef _WIN32
	*handle = LoadLibrary(name);
	return *handle != NULL;
#elif LTREESITTER_USE_LIBUV
	*handle = malloc(sizeof(dl_handle));
	return uv_dlopen(name, *handle) == 0;
#else
	*handle = dlopen(name, RTLD_NOW | RTLD_LOCAL);
	return *handle != NULL;
#endif
}

void *dynamic_sym(dl_handle *handle, const char *sym_name) {
#ifdef _WIN32
	return GetProcAddress(handle, sym_name);
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

void close_dynamic_lib(dl_handle *handle) {
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

const char *dynamic_lib_error(dl_handle *handle) {
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

