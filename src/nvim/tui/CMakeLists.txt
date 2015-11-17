cmake_minimum_required(VERSION 2.8.7)
project(NEOVIM_TUI)

set(TUI_LUA_SOURCES ${PROJECT_SOURCE_DIR}/tui.lua)
set(GENERATED_EMBEDDED_LUA ${GENERATED_DIR}/embedded_lua.c)
set(EMBEDDED_LUA_GENERATOR ${CMAKE_SOURCE_DIR}/scripts/embed.lua)
set(TUI_C_SOURCES ${GENERATED_EMBEDDED_LUA} tui.c)

add_custom_command(OUTPUT ${GENERATED_EMBEDDED_LUA}
  COMMAND ${LUA_PRG} ${EMBEDDED_LUA_GENERATOR}
                     tui_prog
                     ${TUI_LUA_SOURCES}
                     ${GENERATED_EMBEDDED_LUA}
  DEPENDS
    ${EMBEDDED_LUA_GENERATOR}
    ${TUI_LUA_SOURCES})

add_executable(nvim-tui ${TUI_C_SOURCES})
target_link_libraries(nvim-tui ${LUAJIT_LIBRARIES} -ldl -lm)
