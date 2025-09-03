-- An example linter that checks for two things:
--  - Casting of malloc
--  - Declaration of reserved names
--
-- Usage:
--  c-lint.lua <c source files>...

local ts = require "ltreesitter"
local c = ts.require "c"

local parser = c:parser()
local query = c:query[[
((cast_expression
   value: (call_expression function: (identifier) @function-name) @cast)
 (#check-cast! @function-name @cast))

((function_definition
   declarator: (function_declarator declarator: (identifier) @fn-name))
 (#check-reserved-function-name! @fn-name))

((type_definition
   declarator: (type_identifier) @name)
 (#check-reserved-typedef-name! @name))

((preproc_def name: (identifier) @name)
 (#check-reserved-macro-name! @name))

((preproc_function_def name: (identifier) @name)
 (#check-reserved-macro-name! @name))

((preproc_call
   directive: (preproc_directive) @directive
   argument: (preproc_arg) @arg)
 (#eq? @directive "#undef")
 (#check-undef-keyword! @arg))

((enumerator name: (identifier) @name)
 (#check-reserved-enumeration-constant-name! @name))

((struct_specifier body: (field_declaration_list (field_declaration declarator: (field_identifier) @name)))
 (#check-identifier! @name "Struct field name"))

((union_specifier body: (field_declaration_list (field_declaration declarator: (field_identifier) @name)))
 (#check-identifier! @name "Union field name"))

((labeled_statement label: (statement_identifier) @name)
 (#check-identifier! @name "Label name"))

((_ declarator: (identifier) @name)
 (#check-identifier! @name "Variable name"))

((preproc_params (identifier) @name)
 (#check-identifier! @name "Macro argument name"))
]]

local success = true
local current_file_name
local function report(point, ...)
	success = false
	io.stderr:write(current_file_name, ":", point.row + 1, ":", point.column + 1, ": ", ...)
	io.stderr:write("\n")
	coroutine.yield()
end

local allocation_functions = {
	malloc = true,
	calloc = true,
	realloc = true,
	reallocarray = true,
}

local keywords = {
	alignas = true,
	alignof = true,
	auto = true,
	bool = true,
	["break"] = true,
	case = true,
	char = true,
	const = true,
	constexpr = true,
	continue = true,
	default = true,
	["do"] = true,
	double = true,
	["else"] = true,
	enum = true,
	extern = true,
	["false"] = true,
	float = true,
	["for"] = true,
	["goto"] = true,
	["if"] = true,
	inline = true,
	int = true,
	long = true,
	nullptr = true,
	register = true,
	restrict = true,
	["return"] = true,
	short = true,
	signed = true,
	sizeof = true,
	static = true,
	static_assert = true,
	struct = true,
	switch = true,
	thread_local = true,
	["true"] = true,
	typedef = true,
	typeof = true,
	typeof_unqual = true,
	union = true,
	unsigned = true,
	void = true,
	volatile = true,
	["while"] = true,
	_Alignas = true,
	_Alignof = true,
	_Atomic = true,
	_BitInt = true,
	_Bool = true,
	_Complex = true,
	_Decimal128 = true,
	_Decimal32 = true,
	_Decimal64 = true,
	_Generic = true,
	_Imaginary = true,
	_Noreturn = true,
	_Static_assert = true,
	_Thread_local = true,
}

local function check_common(name_node, kind)
	kind = kind or "Name"
	local name = name_node:source()
	if keywords[name] then
		report(name_node:start_point(), kind, " ‘", name, "’ may not be used since it is a keyword")
	end
	if name:sub(1, 2) == "__" then
		report(name_node:start_point(), kind, " ‘", name, "’ is reserved by the C language since it begins with two underscores")
	elseif name:match("^_[A-Z]") then
		report(name_node:start_point(), kind, " ‘", name, "’ is reserved by the C language since it begins with an underscore followed by an uppercase letter")
	end
	if name:match("_t$") then
		report(name_node:start_point(), kind, " ‘", name, "’ is reserved by the POSIX standard since it ends with ‘_t’")
	end
end

local predicates = {
	["check-cast!"] = function(name_node, cast)
		local name = name_node:source()
		if not allocation_functions[name] then return end
		report(cast:start_point(), "Don't cast ‘", name, "’ in C. If the proper header isn't included the resulting behavior is undefined and casting may silence that warning")
	end,

	["check-reserved-function-name!"] = function(name_node)
		local point = name_node:start_point()
		local name = name_node:source()

		check_common(name_node, "Function name")

		local complex_h_names = {
			cerf = true, cerfc = true, cexp2 = true, cexpm1 = true,
			clog10 = true, clog1p = true, clog2 = true,
			clgamma = true, ctgamma = true, csinpi = true, ccospi = true,
			ctanpi = true, casinpi = true, cacospi = true, catanpi = true,
			ccompoundn = true, cpown = true, cpowr = true, crootn = true,
			crsqrt = true, cexp10m1 = true, cexp10 = true, cexp2m1 = true,
			clog10p1 = true, clog2p1 = true, clogp1 = true,
		}
		if complex_h_names[name] or name:match("[fl]$") and complex_h_names[name:sub(1, -2)] then
			report(point, "Function name ‘", name, "’ is reserved by <complex.h>")
		end

		local function check_prefix_followed_by_lowercase_letter(prefix, header, other_header)
			if name:match("^" .. prefix .. "[a-z]") then
				report(
					point,
					"Function name ‘", name, "’ is reserved by <", header, ".h>",
					other_header and " and <" .. other_header .. ".h>" or "",
					" since it starts with ‘", prefix, "’ followed by a lowercase letter"
				)
			end
		end

		check_prefix_followed_by_lowercase_letter("is", "ctype", "wctype")
		check_prefix_followed_by_lowercase_letter("str", "stdlib", "inttypes")
		check_prefix_followed_by_lowercase_letter("wcs", "stdlib", "inttypes")
		check_prefix_followed_by_lowercase_letter("cr_", "math")
		check_prefix_followed_by_lowercase_letter("atomic_", "stdatomic")
		check_prefix_followed_by_lowercase_letter("cnd_", "threads")
		check_prefix_followed_by_lowercase_letter("mtx_", "threads")
		check_prefix_followed_by_lowercase_letter("thrd_", "threads")
		check_prefix_followed_by_lowercase_letter("tss_", "threads")
	end,

	["check-reserved-typedef-name!"] = function(name_node)
		check_common(name_node, "Typedef name")

		local point = name_node:start_point()
		local name = name_node:source()

		local function check_prefix_followed_by_lowercase_letter(prefix, header)
			if name:match("^" .. prefix .. "[a-z]") then
				report(point, "Typedef name ‘", name, "’ is reserved by <", header, ".h> since it starts with ‘", prefix, "’ followed by a lowercase letter")
			end
		end

		if name:match("^int.*_t$") then
			report(point, "Typedef name ‘", name, "’ is reserved by <stdint.h> since it starts with ‘int’ and ends with ‘_t’")
		end
		if name:match("^uint.*_t$") then
			report(point, "Typedef name ‘", name, "’ is reserved by <stdint.h> since it starts with ‘uint’ and ends with ‘_t’")
		end

		check_prefix_followed_by_lowercase_letter("atomic_", "stdatomic")
		check_prefix_followed_by_lowercase_letter("memory_", "stdatomic")
		check_prefix_followed_by_lowercase_letter("cnd_", "threads")
		check_prefix_followed_by_lowercase_letter("mtx_", "threads")
		check_prefix_followed_by_lowercase_letter("thrd_", "threads")
		check_prefix_followed_by_lowercase_letter("tss_", "threads")
	end,

	["check-reserved-macro-name!"] = function(name_node)
		check_common(name_node, "Macro name")

		local point = name_node:start_point()
		local name = name_node:source()

		if keywords[name] then
			report(point, "It is undefined behavior to ‘#define ", name, "’ since it is a keyword")
		end

		if name:match("^E[A-Z0-9]") then
			report(point, "Macro name ‘", name, "’ is reserved by <errno.h> since it starts with ‘E’ followed by ", name:sub(2,2):match("[0-9]") and "a digit" or "an uppercase letter")
		end
		do
			local prefix = name:match("^INT") or name:match("^UINT")
			local suffix = prefix
				and name:match("_MAX$")
				or name:match("_MIN$")
				or name:match("_WIDTH$")
				or name:match("_C$")

			if prefix and suffix then
				report(point, "Macro name ‘", name, "’ is reserved by <stdint.h> since it starts with ‘", prefix, "’ and ends with ‘", suffix, "’")
			end
		end

		do
			local prefix = name:match("^PRI") or name:match("^SCN")
			if prefix then
				local letter = name:sub(4, 4)
				if letter == "X" then
					report(point, "Macro name ‘", name, "’ is reserved by <inttypes.h> since it starts with ‘", prefix, "X’")
				elseif letter:match("[a-z]") then
					report(point, "Macro name ‘", name, "’ is reserved by <inttypes.h> since it starts with ‘", prefix, "’ followed by a lowercase letter")
				end
			end
		end

		local function check_prefix_followed_by_uppercase_letter(prefix, header)
			if name:match("^" .. prefix .. "[A-Z]") then
				report(point, "Macro name ‘", name, "’ is reserved by <", header, ".h> since it starts with ‘", prefix, "’ followed by an uppercase letter")
			end
		end

		check_prefix_followed_by_uppercase_letter("FE_", "fenv")
		check_prefix_followed_by_uppercase_letter("FLT_", "float")
		check_prefix_followed_by_uppercase_letter("DBL_", "float")
		check_prefix_followed_by_uppercase_letter("LDBL_", "float")
		check_prefix_followed_by_uppercase_letter("DEC_", "float")
		check_prefix_followed_by_uppercase_letter("DEC32_", "float")
		check_prefix_followed_by_uppercase_letter("DEC64_", "float")
		check_prefix_followed_by_uppercase_letter("DEC128_", "float")
		check_prefix_followed_by_uppercase_letter("LC_", "locale")
		check_prefix_followed_by_uppercase_letter("FP_", "math")
		check_prefix_followed_by_uppercase_letter("MATH_", "math")
		check_prefix_followed_by_uppercase_letter("SIG", "signal")
		check_prefix_followed_by_uppercase_letter("SIG_", "signal")
		check_prefix_followed_by_uppercase_letter("TIME_", "time")
		check_prefix_followed_by_uppercase_letter("ATOMIC_", "stdatomic")
	end,

	["check-reserved-enumeration-constant-name!"] = function(name_node)
		check_common(name_node, "Enumeration constant name")

		local point = name_node:start_point()
		local name = name_node:source()

		local function check(prefix, header)
			if name:match("^" .. prefix .. "[a-z]") then
				report(point, "Enumeration constant name ‘", name, "’ is reserved by <", header, ".h> since it starts with ", prefix, " followed by a lowercase letter")
			end
		end

		check("memory_order_", "stdatomic")
		check("cnd_", "threads")
		check("mtx_", "threads")
		check("thrd_", "threads")
		check("tss_", "threads")
	end,

	["check-undef-keyword!"] = function(name_node)
		local name = name_node:source():match("^%s*(.*)%s*$")
		if keywords[name] then
			report(name_node:start_point(), "It is undefined behavior to ‘#undef ", name, "’ since it is a keyword")
		end
	end,

	["check-identifier!"] = check_common,
}

for k, v in pairs(predicates) do
	predicates[k] = function(...)
		return select(2, assert(coroutine.resume(coroutine.create(v), ...)))
	end
end

for i = 1, select("#", ...) do
	current_file_name = select(i, ...)
	local file = assert(io.open(current_file_name, "r"))
	local contents = file:read("*a")
	file:close()

	local tree = parser:parse_string(contents)
	query:exec(tree:root(), predicates)

	-- print(tree)
end

os.exit(success and 0 or 1)
