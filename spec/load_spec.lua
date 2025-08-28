local assert = require("luassert")
local ts = require("ltreesitter")
local util = require("spec.util")

local c_parser_to_load = os.getenv "LTREESITTER_TEST_LOAD_C_PARSER"

describe("ltreesitter.load", function()
	it("should return nil and an error message on failure", function()
		local ok, err = ts.load("./parser_that_doesnt_exist", "language_that_doesnt_exist")
		assert(ok == nil, "Expected load to return nil")
		assert(type(err) == "string", "Expected an error message")
	end)

	;(c_parser_to_load and it or pending)("should load a ltreesitter.Language from $LTREESITTER_TEST_LOAD_C_PARSER", function()
		local p = ts.load(c_parser_to_load, "c")
		util.assert_userdata_type(p, "ltreesitter.Language")
	end)
end)

describe("ltreesitter.require", function()
	it("should error on failure", function()
		assert.has.errors(function() ts.require("non_existent_thing") end)
	end)
	it("should return a ltreesitter.Language", function()
		local p = ts.require("c")
		util.assert_userdata_type(p, "ltreesitter.Language", "This test requires a c parser installed in your cpath")
	end)
end)
