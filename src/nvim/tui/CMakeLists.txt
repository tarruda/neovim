cmake_minimum_required(VERSION 2.8.7)

project(tui)

set(LIBUNIBILIUM_USE_STATIC ON)
find_package(LibUnibilium REQUIRED)
include_directories(SYSTEM ${LIBUNIBILIUM_INCLUDE_DIRS})

set(LIBTERMKEY_USE_STATIC ON)
find_package(LibTermkey REQUIRED)
include_directories(SYSTEM ${LIBTERMEY_INCLUDE_DIRS})

set(LIBTICKIT_USE_STATIC ON)
find_package(LibTickit REQUIRED)
include_directories(SYSTEM ${LIBTICKIT_INCLUDE_DIRS})

add_library(tui MODULE tui.c)
set_target_properties(tui PROPERTIES PREFIX "")
# remove --no-undefined flag, some symbols are define in the main Nvim
# executable
set(CMAKE_MODULE_LINKER_FLAGS "")

target_link_libraries(tui
  "-Wl,--whole-archive"
  ${LIBUNIBILIUM_LIBRARIES}
  ${LIBTERMKEY_LIBRARIES}
  ${LIBTICKIT_LIBRARIES}
  "-Wl,--no-whole-archive")
