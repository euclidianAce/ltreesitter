#ifndef LTREESITTER_TREE_H
#define LTREESITTER_TREE_H

void ltreesitter_create_tree_metatable(lua_State *L);
struct ltreesitter_Tree *ltreesitter_check_tree(lua_State *L, int idx);
void push_tree(lua_State *L, const TSLanguage *lang, TSTree *t, bool own_str, const char *src, size_t src_len);

#endif
