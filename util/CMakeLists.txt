find_package(Lua51 REQUIRED)
message(STATUS "Lua include dir: ${LUA_INCLUDE_DIR}")
message(STATUS "Lua libraries dir: ${LUA_LIBRARIES}")
message(STATUS "Lua version: ${LUA_VERSION_STRING}")
include_directories(${LUA_INCLUDE_DIR})
add_library(configuration configuration.cc lua_loader.cc)
target_link_libraries(configuration ${LUA_LIBRARIES})