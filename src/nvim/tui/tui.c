#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

extern const char tui_prog[];

int main(int argc, char **argv)
{
  lua_State *L = lua_open();
  luaL_openlibs(L);
  luaL_dostring(L, tui_prog);

  return 0;
}
