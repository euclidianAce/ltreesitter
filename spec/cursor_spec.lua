local assert = require("luassert")
local ts = require("ltreesitter")
local util = require("spec.util")

describe("Cursor", function()
	local p
	local str = {
		[[ /* this is a comment */ int main(void) { return 0; } ]],
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
	describe("current_node", function()
		it("should return the correct node", function()
			-- should be at the (function_declarator) for 'int main'
			local c = assert(assert(root[1]
				:child(1)
				:child(1), "Unable to get node children")
				:create_cursor(), "Unable to create cursor from node")

			assert.are.equal("function_declarator", assert(c:current_node(), "current_node didn't return a node"):type())
		end)
	end)
	describe("current_field_name", function()
		it("should return the correct string", function()
			local c = assert(assert(root[1]
				:child(1), "Unable to get node child")
				:create_cursor(), "Unable to create cursor from node")

			assert(c:goto_first_child())
			-- should be at the (primitive_type) for 'int main'

			assert.are.equal(c:current_field_name(), "type")
		end)
	end)

	describe("goto_first_child", function()
		it("should return true on success and false on failure", function()
			local c = assert(root[1]:create_cursor(), "Unable to create cursor from node")
			assert(c:goto_first_child())
			assert(not c:goto_first_child())
		end)
	end)

	describe("goto_next_sibling", function()
		it("should return true on success and false on failure", function()
			local c = assert(root[1]:create_cursor(), "Unable to create cursor from node")
			assert(not c:goto_next_sibling())
			assert(c:goto_first_child())
			assert(c:goto_next_sibling())
			assert(not c:goto_next_sibling())
		end)
	end)
	describe("goto_parent", function()
		it("should return true on success and false on failure", function()
			local c = assert(root[1]:create_cursor(), "Unable to create cursor from node")
			assert(c:goto_first_child())
			assert(c:goto_parent())
			assert(not c:goto_parent())
		end)
	end)
	describe("reset", function()
		it("should place the cursor at the given node", function()
			local c = assert(root[1]:create_cursor(), "Unable to create cursor from node")
			c:reset(root[1]:child(0))
			assert.are.equal(root[1]:child(0), c:current_node())
		end)
	end)
end)
