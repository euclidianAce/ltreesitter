-- An example highlighter that emits C code highlighted with ANSI escape codes
--
-- Usage
--  c-highlight <c source files>...

local ts = require "ltreesitter"
local c = ts.require "c"

local parser = c:parser()
local query = c:query[[
(identifier) @variable

((identifier) @constant
 (#match? @constant "^[A-Z_][A-Z0-9_]*$"))

((identifier) @constant.builtin
 (#match? @constant.builtin "^_[A-Z][A-Za-z0-9_]*$"))

[ "break" "case" "const" "continue"
  "default" "do" "else" "enum" "extern"
  "for" "if" "inline" "return"
  "static" "struct" "switch" "typedef" "union"
  "volatile" "while"
  "#define" "#elif" "#else" "#endif"
  "#if" "#ifdef" "#ifndef" "#include"
  (preproc_directive) ] @keyword

[ "sizeof" ] @keyword.operator

[ "--" "-" "-=" "->"
  "=" "!" "!=" "*"
  "&" "&&" "+" "++"
  "/" "/=" "*="
  "+=" "<" "==" ">"
  ">=" "|" "||" ] @operator

[ "." "," ":" ";" ] @punctuation.delimiter
[ "(" ")" "{" "}" "[" "]" ] @punctuation.bracket

[ (string_literal) (system_lib_string) ] @string

(null) @constant
[ (number_literal) (char_literal) ] @number

(field_identifier) @property
(statement_identifier) @label

[ (type_identifier) (primitive_type) (sized_type_specifier) ] @type

(call_expression
  function: (identifier) @function)
(call_expression
  function: (field_expression
    field: (field_identifier) @function))
(function_declarator
  declarator: (identifier) @function)
(preproc_function_def
  name: (identifier) @function.special)

(comment) @comment
]]

local ansi_colors = {
	number = "31",
	string = "31",

	constant = "31",
	["constant.builtin"] = "91",

	["function"] = "92",
	["function.special"] = "92",

	type = "96",

	keyword = "35",

	operator = "36",
	["keyword.operator"] = "36",

	punctuation = "37",
	["punctuation.bracket"] = "37",
	["punctuation.delimiter"] = "37",
	delimiter = "37",
	comment = "37",

	-- property = ?
	-- label = ?
}

local csi = string.char(27) .. "["

for i = 1, select("#", ...) do
	local file = assert(io.open(select(i, ...), "r"))
	local contents = file:read("*a")
	file:close()

	local tree = parser:parse_string(contents)
	local last_emitted_byte_index = 0

	local decoration = {}

	for cap, name in query:capture(tree:root()) do
		local color = ansi_colors[name]
		if color then
			for i = cap:start_index(), cap:end_index() do
				decoration[i] = color
			end
		end
	end

	local previous_color
	local last_emitted_index = 0
	for i = 1, #contents do
		if decoration[i] ~= previous_color then
			if (last_emitted_index or 0) < i - 1 then
				io.write(contents:sub(last_emitted_index + 1, i - 1))
				last_emitted_index = i - 1
			end

			if decoration[i] then
				io.write(csi .. decoration[i] .. "m")
			else
				io.write(csi .. "0m")
			end
			previous_color = decoration[i]
		end
	end

	if last_emitted_index < #contents then
		if previous_color then
			io.write(csi .. "0m")
		end
		io.write(contents:sub(last_emitted_index + 1, -1))
	end
end
