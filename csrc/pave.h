/* pave.h

- pave /pāv/
	transitive verb

	To cover uniformly, as if with pavement

- An attempt to pave over platform/compiler differences via macros.
	This single header is intended to just be copy-pasted into your project.

- License:
	This file is released under the CC0 license:
	https://creativecommons.org/publicdomain/zero/1.0/legalcode.txt

	TL;DR: This work is released to the public domain to the fullest extent
	of the law and comes with no warranty

- Defined macros:

	pave_export
		Export a function so it is publicly visible to other objects

	pave_thread_local
		A storage specifier to make a variable thread local.
		Uses _Thread_local or thread_local keywords when present. Uses compiler
		specific attributes otherwise.

	pave_always_inline
		Ignores the compiler's heuristics for inlining and forces a function to
		be inlined. Will likely need to be paired with `static` and `inline`.

	pave_never_inline
		Ignores the compiler's heuristics for inlining and forces a function to
		never be inlined.

	pave_if_c23_else(when_c23, when_not_c23)
		Expands to `when_c23` when __STDC_VERSION__ >= 202311L
		otherwise expands to `when_not_c23`

	pave_sym(base)
		Creates a ‘unique’ symbol by appending the current line number to `base`

	pave_static_assert(condition, message)
		For C11/C++11 onwards will use _Static_assert or static_assert keyword,
		otherwise will expand to an array declaration with negative size when `condition` is false

	pave_likely(condition)
	pave_unlikely(condition)
		Usage:
			`if pave_likely(some_condition) { ... }`
			`while pave_likely(some_condition) { ... }`
		Expands to `(condition)` possibly with branch hinting annotations that
		denote `condition` is likely to be true or false

- Changelog:
	Local changes for ltreesitter:
		fixed usage of __builtin_expect to coerce to bool
	v0.1.0:
		Initial version
		Support for c89/95 through c23
		Support for c++98 through c++23
		Added pave_if_c23_else
		Added pave_export
		Added pave_thread_local
		Added pave_always_inline
		Added pave_never_inline
		Added pave_sym
		Added pave_static_assert
		Added pave_if_likely
		Added pave_if_unlikely

- TODOs:
		Atomics. Are there consistent enough compiler extensions pre C11 for this to be worth it?
*/

#ifndef pave_h
#define pave_h

#define pave_c95 199409L
#define pave_c99 199901L
#define pave_c11 201112L
#define pave_c17 201710L
#define pave_c23 202311L

#define pave_cpp98 199711L
#define pave_cpp11 201103L
#define pave_cpp14 201402L
#define pave_cpp17 201703L
#define pave_cpp20 202002L
#define pave_cpp23 202302L

#if defined(__STDC_VERSION__) && !defined(__cplusplus)
#	define pave_c_at_least(n) (__STDC_VERSION__ >= (n))
#else
#	define pave_c_at_least(_) 0
#endif

#if defined(__cplusplus)
#	define pave_cpp_at_least(n) (__cplusplus >= (n))
#else
#	define pave_cpp_at_least(_) 0
#endif

#if defined(__GNUC__)
#	define pavep_gcc_at_least(major, minor, patch) ( \
		(__GNUC__ > (major)) || \
		(__GNUC__ == (major) && __GNUC_MINOR__ >= (minor) && __GNUC_PATCH__ >= (patch)) \
	)
#else
#	define pavep_gcc_at_least(major, minor, patch) 0
#endif

#if pave_c_at_least(pave_c99) || pave_cpp_at_least(pave_cpp11)
#	define pave_compile_error(message) _Pragma("error " message) !?!?!?!?! This is an error! !?!?!?!?!
#else
#	define pave_compile_error(message) !?!?!?!?! This is an error! !?!?!?!?!
#endif

#if pave_c_at_least(pave_c23)
#	define pave_if_c23(x) x
#	define pave_if_c23_else(x, _) x
#else
#	define pave_if_c23(_)
#	define pave_if_c23_else(_, y) y
#endif

#if defined(__cplusplus)
#	define pave_if_cpp(x) x
#	define pave_if_cpp_else(x, _) x
#else
#	define pave_if_cpp(_)
#	define pave_if_cpp_else(_, y) y
#endif

#if pave_cpp_at_least(pave_cpp11) || pave_c_at_least(pave_c23)
#	define pave_supports_attributes 1
#	define pave_if_attributes(x) x
#	define pave_if_attributes_else(x, _) x
#else
#	define pave_supports_attributes 0
#	define pave_if_attributes(_)
#	define pave_if_attributes_else(_, y) y
#endif

#ifndef pave_export
#	if defined(__GNUC__) || defined(__clang__)
#		define pave_export pave_if_attributes_else([[gnu::visibility("default")]], __attribute__((visibility("default"))))
#	elif defined(_MSC_VER)
#		define pave_export __declspec(dllexport)
#	else
#		define pave_export pave_compile_error("pave_export not implemented for this platform")
#	endif
#endif /* pave_export */

#ifndef pave_thread_local
#	if (pave_c_at_least(pave_c23) && !defined(__STDC_NO_THREADS__)) || pave_cpp_at_least(pave_cpp11)
#		define pave_thread_local thread_local
#	elif (pave_c_at_least(pave_c11) && !defined(__STDC_NO_THREADS__)) && !defined(__cplusplus)
#		define pave_thread_local _Thread_local
#	elif defined(__GNUC__) || defined(__clang__)
#		define pave_thread_local __thread
#	elif defined(_MSC_VER)
#		define pave_thread_local __declspec(thread)
#	else
#		define pave_thread_local pave_compile_error("pave_thread_local not implemented for this platform")
#	endif
#endif /* pave_thread_local */

#ifndef pave_always_inline
#	if defined(__GNUC__) || defined(__clang__)
#		define pave_always_inline pave_if_attributes_else([[gnu::always_inline]], __attribute__((always_inline)))
#	elif defined(_MSC_VER)
#		define pave_always_inline pave_if_attributes_else([[msvc::forceinline]], __forceinline)
#	else
#		define pave_always_inline pave_compile_error("pave_always_inline not implemented for this platform")
#	endif
#endif /* pave_always_inline */

#ifndef pave_never_inline
#	if defined(__GNUC__) || defined(__clang__)
#		define pave_never_inline pave_if_attributes_else([[gnu::noinline]], __attribute__((noinline)))
#	elif defined(_MSC_VER)
#		define pave_never_inline pave_if_attributes_else([[msvc::noinline]], __declspec(noinline))
#	else
#		define pave_never_inline pave_compile_error("pave_never_inline not implemented for this platform")
#	endif
#endif /* pave_never_inline */

#ifndef pave_sym
#	define pavep_concat2(x, y, z) x##y##z
#	define pavep_concat1(x, y, z) pavep_concat2(x, y, z)
#	define pave_sym(base) pavep_concat1(base, _, __LINE__)
#endif /* pave_sym */

/* Note, since this uses a declaration of an array for older versions, don't use it in struct/union definitions */
#ifndef pave_static_assert
#	if (defined(__cpp_static_assert) && __cpp_static_assert >= 200410L) || pave_c_at_least(pave_c23)
#		define pave_static_assert(condition, message) static_assert(condition, message)
#	elif (pave_c_at_least(pave_c11) && !defined(__cplusplus))
#		define pave_static_assert(condition, message) _Static_assert(condition, message)
#	elif pavep_gcc_at_least(4,6,0)
#		define pave_static_assert(condition, message) __extension__ _Static_assert(condition, message)
#	else
#		define pave_static_assert(condition, message) void pave_sym(static_assert)(char static_assert[(condition) ? 1 : -1])
#	endif
#endif /* pave_static_assert */

#ifndef pave_likely
#	if pave_cpp_at_least(pave_cpp20)
#		define pave_likely(x) (x) [[likely]]
#	elif defined(__clang__)
#		if pave_cpp_at_least(pave_cpp11)
#			define pave_likely(x) (x) [[likely]]
#		elif pave_c_at_least(pave_c23)
#			define pave_likely(x) (x) [[clang::likely]]
#		else
#			define pave_likely(x) (__builtin_expect(!!(x), 0))
#		endif
#	elif defined(__GNUC__)
#		define pave_likely(x) (__builtin_expect(!!(x), 1))
#	else
#		define pave_likely(x) (x)
#	endif
#endif /* pave_likely */

#ifndef pave_unlikely
#	if pave_cpp_at_least(pave_cpp20)
#		define pave_unlikely(x) (x) [[unlikely]]
#	elif defined(__clang__)
#		if pave_cpp_at_least(pave_cpp11)
#			define pave_unlikely(x) (x) [[unlikely]]
#		elif pave_c_at_least(pave_c23)
#			define pave_unlikely(x) (x) [[clang::unlikely]]
#		else
#			define pave_unlikely(x) (__builtin_expect(!!(x), 0))
#		endif
#	elif defined(__GNUC__)
#		define pave_unlikely(x) (__builtin_expect(!!(x), 0))
#	else
#		define pave_unlikely(x) (x)
#	endif
#endif /* pave_unlikely */

#endif /* pave_h */
