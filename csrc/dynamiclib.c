
#include <stddef.h>

#include <ltreesitter/dynamiclib.h>

bool ltreesitter_open_dynamic_lib(const char *name, ltreesitter_Dynlib **handle, const char **out_error) {
	ltreesitter_Dynlib *lib = NULL;
#ifdef _WIN32
	lib = LoadLibrary(name);
	if (!lib) *out_error = GetLastError();
#elif LTREESITTER_USE_LIBUV
	{
		ltreesitter_Dynlib temp;
		if (uv_dlopen(name, &temp) != 0) {
			*out_error = uv_dlerror(&temp);
		} else {
			lib = malloc(sizeof *lib);
			if (!lib)
				*out_error = "Out of memory";
			else
				*lib = temp;
		}
	}
#else
	lib = dlopen(name, RTLD_NOW | RTLD_LOCAL);
	if (!lib)
		*out_error = dlerror();
#endif
	*handle = lib;
	return lib != NULL;
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
