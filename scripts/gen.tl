
local ts <const> = require("ltreesitter")
local p <const> = assert(ts.load(os.getenv("HOME") .. "/dev/parsers/c.so", "c"))

local ti <const> = table.insert

local function parse_file(filename: string): ts.Tree, string
   local fh <const> = assert(io.open(filename, "r"))
   local content = fh:read("*a")
   fh:close()

   return p:parse_string(content), content
end

local file <const> = "ltreesitter.c"
local mod_name <const> = file:match("([^%.%/%\\]+)%.c")

local dtl_name <const> = "ltreesitter.d.tl"
local html_name <const> = "docs/index.html"

local type RecordDef = {string:string|RecordDef}
local records <const>: RecordDef = {}

local inlines <const>: {string} = {}

local type DocDef = {string:DocDef|string}
local docs <const> = {}

local function trim(s: string): string
   return s:match("^%s*(.-)%s*$")
end

local function split(str: string, del: string, no_patt: boolean): function(): string
   local i = 0
   return function(): string
      if not i then return end
      i = i + 1
      local prev_i = i
      local s: number
      s, i = str:find(del, i, no_patt)
      return str:sub(prev_i, (s or 0) - 1)
   end
end

local function insert_into(dest: {string:any}, name: string, content: string)
   local components <const>: {string} = {}
   for comp in split(name, ".", true) do
      table.insert(components, comp)
   end

   if #components > 1 then
      local base = dest[components[1]] as {string:any} or {}
      local current = base
      local len = #components
      for i = 2, len - 1 do
         if not current[components[i]] then
            current[components[i]] = {}
         end
         current = current[components[i]] as {string:any}
      end
      current[components[len]] = content
      dest[components[1]] = base
   else
      dest[components[1]] = content
   end
end

-- TODO: should probably just use the teal parser to pull out these type definitions
local directives: {string:function(ts.Node, string)} = {
   ["@teal-export"] = function(n: ts.Node, content: string)
      local name, typedef = content:match("%@teal%-export%s+(.-)%s*:%s*(.*)")
      local doc_s, doc_e = typedef:find("%[%[.*%]%]")
      local documentation: string
      if doc_s then
         documentation = trim(typedef:sub(doc_s+2, doc_e-2)):gsub("\n", "<br>")
         typedef = typedef:sub(1, doc_s-1)
      end
      typedef = typedef:gsub("[*/]", "")
      if name then
         if documentation then
            insert_into(docs, name, "<" .. typedef .. ">" .. trim(documentation))
         end
         local c_src_start = n:start_point()
         typedef = typedef .. " -- from " .. math.floor(c_src_start.row)
         insert_into(records as {string:any}, name, typedef)
      end
   end,

   ["@teal-inline"] = function(n: ts.Node, content: string)
      local start = n:start_point()
      for inline_content in content:gmatch("@teal%-inline%s*%[%[(.-)%]%]") do
         ti(inlines, inline_content:gsub("\n*$", "") .. " -- from " .. math.floor(start.row))
      end
   end,

}

local tree, src = parse_file(file)

for c in p
   :query[[ (comment) @the-comment ]]
   :capture(tree:root())
do
   local c_src = c:source()
   for patt, cb in pairs(directives) do
      if c_src:find(patt, 1, true) then
         cb(c, c_src)
      end
   end
end

-- Sort the record definitions so we don't get an ugly diff every time we regenerate docs
local record NamedRecordDef
   name: string
   def: string|{NamedRecordDef}
end
local record_def_sorter <const> = function(a: NamedRecordDef, b: NamedRecordDef): boolean
   return a.name < b.name
end

local function get_sorted_records(tree: RecordDef): {NamedRecordDef}
   local defs: {NamedRecordDef} = {}
   for entry_name, entry_def in pairs(tree) do
      if entry_def is RecordDef then
         ti(defs, {name = entry_name, def = get_sorted_records(entry_def)})
      else
         ti(defs, {name = entry_name, def = entry_def})
      end
   end
   table.sort(defs, record_def_sorter)
   return defs
end

-- generate the record definition
local output: {string} = {}
local module: {NamedRecordDef} = get_sorted_records(records)

local function generate_record_defs(defs: {NamedRecordDef}, indent: number)
   if not defs then return end
   indent = indent or 0
   for i, def in ipairs(defs) do
      ti(output, ("   "):rep(indent))
      local rec_def <const> = def.def
      if rec_def is {NamedRecordDef} then
         ti(output, "record ")
         ti(output, def.name)
         ti(output, "\n")
         generate_record_defs(rec_def, indent + 1)
         ti(output, ("   "):rep(indent))
         ti(output, "end\n")
      else
         ti(output, def.name)
         ti(output, ": ")
         ti(output, def.def as string)
         ti(output, "\n")
      end
   end
end

ti(output, "local record ltreesitter\n")
ti(output, "   -- Exports\n\n")
generate_record_defs(module, 1)
ti(output, "\n   -- Inlines\n")
ti(output, table.concat(inlines, "\n"))
ti(output, "\nend\n\nreturn ltreesitter")

local fname = file:match("([^%.%/%\\]+%.c)")
local dtl_src = "-- Autogenerated type definitions from " .. fname .. "\n\n" .. table.concat(output) .. "\n"

do
   io.write("Generating ", dtl_name, "...\n")
   io.write("   Writing types to file...")
   local fh = io.open(dtl_name, "w")
   fh:write(dtl_src)
   fh:close()
   io.write(" Done.\n")

   io.write("   Type checking ", dtl_name, "...")
   local tl = require("tl")
   local res = assert(tl.lex(dtl_src))
   local _, code = tl.parse_program(res, nil, dtl_name)
   local errs = tl.type_check(code)
   if #errs > 0 then
      io.stderr:write("Resulting module had errors:\n")
      for _, err in ipairs(errs) do
         io.stderr:write( string.format(
            "%d:%d:%s\n",
            err.y, err.x, err.msg
         ))
      end
   else
      io.write(" Done.\n")
   end
end

local record DocInfo
   {string}
   mod: string
   index: string
   doc: string
end

local function traverse_docs(t: DocDef, prefix: string, entries: {DocInfo}): {DocInfo}
   prefix = prefix or ""
   entries = entries or {}
   for name, v in pairs(t) do
      if v is DocDef then
         traverse_docs(v, prefix .. name .. ".", entries)
      elseif v is string then
         local full_name = prefix .. name
         local t, content = v:match("^<(.-)>(.*)$")
         if not t then
            t = "any"
            content = v
         end
         if not content then
            content = ""
         end

         table.insert(entries, {
            full_name,
            mod = prefix:sub(1, -2),
            index = string.format(
               '<a href="%s#%s">%s</a>',
               html_name, full_name, full_name
            ),
            doc = string.format(
               '<h2><a name="%s"><code>%s: %s</code></a></h2>\n<p>%s</p>',
               full_name, full_name, t, content
            )
         })
      end
   end
   return entries
end

local doc_entries = traverse_docs(docs)

table.sort(doc_entries, function(a: DocInfo, b: DocInfo): boolean
   if a.mod == b.mod then
      return a[1] < b[1]
   end
   return a.mod < b.mod
end)

-- dark mode lua manual sorta look
local default_style <const> = [==[<style>
html {
   background-color: #16191a;
}

body {
   background-color: #181a1b;
   margin: auto;
   max-width: 70em;
   width: 90%;
   padding: 32px;
   font-family: Helvetica, Arial, sans-serif;
   color: #e8e6e3;
}

a {
   text-decoration: none;
   color: #3391ff;
}

h2 {
   color: #117dee;
}

</style>]==]

do
   io.write("Writing docs to ", html_name, "...")
   local fh = io.open(html_name, "w")
   local w = function(...: string) fh:write(...) fh:write("\n") end

   w"<html>"
   w(default_style)

   w"<body>"
   w'<table width="100%">'
   w"<tbody>"
   w"<tr>"

   local num_entries = math.floor(#doc_entries / 4)
   local function td() w"<td valign=top>" end

   local current_entries = 0
   local current_module = doc_entries[1].mod
   if current_module == "" then
      current_module = mod_name
   end

   local documentation = {}

   td()
   w("<h4>", current_module, "</h4>")
   w"<p>"

   for i, entry in ipairs(doc_entries) do
      if entry.mod ~= "" and entry.mod ~= current_module then
         w"</p>"
         current_module = entry.mod
         if current_entries > num_entries then
            current_entries = 0
            w"</td>"
            td()
         end
         w("<h4>", current_module, "</h4>")
         table.insert(documentation, "")
         w"<p>"
      end
      w(entry.index)
      table.insert(documentation, entry.doc)
      w"<br>"
      current_entries = current_entries + 1
   end

   w"</p></td></tr></tbody></table>"

   -- TODO: idk html that well, is there a page break or something that can be used here?
   w(("<br>"):rep(10))

   for _, doc in ipairs(documentation) do
      w(doc, "<br>")
   end

   w"</body></html>"
   fh:close()
   io.write(" Done.\n")
end
