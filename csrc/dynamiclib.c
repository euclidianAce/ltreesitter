
#include <stddef.h>
#include <string.h>

#include "dynamiclib.h"

static size_t copy_cstr(size_t dest_capacity, char *dest, char const *src) {
	size_t src_len = strlen(src);
	size_t result = src_len < dest_capacity ? src_len : dest_capacity;
	memcpy(dest, src, result);
	return result;
}

bool dynlib_open(char const *name, Dynlib *handle, size_t *out_error_buf_len, char *out_error_buf) {
#ifdef _WIN32
	*handle = (Dynlib){.opaque_handle = LoadLibrary(name)};
	if (!handle->opaque_handle) {
		DWORD err = GetLastError();
		*out_error_buf_len = FormatMessageA(
			FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL,
			err,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			out_error_buf,
			*out_error_buf_len,
			NULL
		);
		if (*out_error_buf_len == 0)
			*out_error_buf_len = copy_cstr(*out_error_buf_len, out_error_buf, "Unknown error");
		return false;
	}
#elif LTREESITTER_USE_LIBUV
	if (uv_dlopen(name, handle) != 0) {
		*out_error_buf_len = copy_cstr(*out_error_buf_len, out_error_buf, uv_dlerror(handle));
		return false;
	} else if (!*handle) {
		*out_error_buf_len = copy_cstr(*out_error_buf_len, out_error_buf, "Out of memory");
		return false;
	}
#else
	*handle = (Dynlib){.opaque_handle = dlopen(name, RTLD_NOW | RTLD_LOCAL)};
	if (!handle->opaque_handle) {
		*out_error_buf_len = copy_cstr(*out_error_buf_len, out_error_buf, dlerror());
		return false;
	}
#endif
	return true;
}

void *dynlib_sym(Dynlib *handle, char const *sym_name) {
#ifdef _WIN32
	FARPROC sym = GetProcAddress(handle->opaque_handle, sym_name);
	void *result;
	memcpy(&result, &sym, sizeof result);
	return result;
#elif LTREESITTER_USE_LIBUV
	void *sym = NULL;
	if (uv_dlsym(handle, sym_name, &sym) == 0)
		return sym;
	return NULL;
#else
	return dlsym(handle->opaque_handle, sym_name);
#endif
}

void dynlib_close(Dynlib *handle) {
#ifdef _WIN32
	FreeLibrary(handle->opaque_handle);
#elif LTREESITTER_USE_LIBUV
	uv_dlclose(handle);
#else
	dlclose(handle->opaque_handle);
#endif
}
