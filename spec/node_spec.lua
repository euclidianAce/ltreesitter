local assert = require("luassert")
local ts = require("ltreesitter")
local util = require("spec.util")

describe("Node", function()
	local p
	local str = {
		[[ /* hello world */ ]],
		[[ /* hello world */ int main(void) { return 0; } ]],
	}
	local tree = {}
	local root = {}
	setup(function()
		p = assert(ts.require("c"))
		for i, v in ipairs(str) do
			tree[i] = assert(p:parse_string(v))
			root[i] = assert(tree[i]:root())
		end
	end)
	it("name should return the name of the node", function()
		assert.are.equal(root[1]:name(), "translation_unit")
	end)
	it("type should return the type of the node", function()
		assert.are.equal(root[1]:type(), "translation_unit")
	end)
	it("child should return a Node (and the correct child)", function()
		local n = root[1]:child(0)
		util.assert_userdata_type(n, "ltreesitter.TSNode")
		assert.are.equal(n:name(), "comment")
	end)
	it("child_count should return the correct number of children", function()
		local n = root[2]:child_count()
		assert.are.equal(type(n), "number")
		assert.are.equal(n, 2, "incorrect number of children")
	end)
	pending("child_by_field_name", function() end)
	it("children should iterate over all the children of a node", function()
		-- TODO: find a case where children and named_children differ
		local actual_child_names = {}
		local actual_child_types = {}
		for child in assert(root[2]:child(1)):children() do
			util.assert_userdata_type(child, "ltreesitter.TSNode")
			table.insert(actual_child_names, child:name())
			table.insert(actual_child_types, child:type())
		end
		assert.are.same(actual_child_names, {
			"primitive_type",
			"function_declarator",
			"compound_statement",
		})
		assert.are.same(actual_child_types, {
			"primitive_type",
			"function_declarator",
			"compound_statement",
		})
	end)
	it("named_children should iterate over all the named children of a node", function()
		local actual_child_names = {}
		local actual_child_types = {}
		for child in assert(root[2]:child(1)):named_children() do
			util.assert_userdata_type(child, "ltreesitter.TSNode")
			table.insert(actual_child_names, child:name())
			table.insert(actual_child_types, child:type())
		end
		assert.are.same(actual_child_names, {
			"primitive_type",
			"function_declarator",
			"compound_statement",
		})
		assert.are.same(actual_child_types, {
			"primitive_type",
			"function_declarator",
			"compound_statement",
		})
	end)

	pending("create_cursor", function() end)
	pending("byte_range", function() end)
	pending("start_byte", function() end)
	pending("end_byte", function() end)
	pending("start_point", function() end)
	pending("end_point", function() end)
	pending("source", function() end)
	pending("is_extra", function() end)
	pending("is_missing", function() end)
	pending("is_named", function() end)
	pending("named_child", function() end)
	pending("named_child_count", function() end)
	pending("next_named_sibling", function() end)
	pending("next_sibling", function() end)
	pending("prev_named_sibling", function() end)
	pending("prev_sibling", function() end)
end)
