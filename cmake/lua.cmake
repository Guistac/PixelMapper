message(STATUS "== Including Lua")

set(LUA_DIRECTORY ${DEPENDENCIES_DIRECTORY}/lua)

set(LUA_SOURCE_FILES
    ${LUA_DIRECTORY}/lapi.c
    ${LUA_DIRECTORY}/lauxlib.c
    ${LUA_DIRECTORY}/lbaselib.c
    ${LUA_DIRECTORY}/lcode.c
    ${LUA_DIRECTORY}/lcorolib.c
    ${LUA_DIRECTORY}/lctype.c
    ${LUA_DIRECTORY}/ldblib.c
    ${LUA_DIRECTORY}/ldebug.c
    ${LUA_DIRECTORY}/ldo.c
    ${LUA_DIRECTORY}/ldump.c
    ${LUA_DIRECTORY}/lfunc.c
    ${LUA_DIRECTORY}/lgc.c
    ${LUA_DIRECTORY}/linit.c
    ${LUA_DIRECTORY}/liolib.c
    ${LUA_DIRECTORY}/llex.c
    ${LUA_DIRECTORY}/lmathlib.c
    ${LUA_DIRECTORY}/lmem.c
    ${LUA_DIRECTORY}/loadlib.c
    ${LUA_DIRECTORY}/lobject.c
    ${LUA_DIRECTORY}/lopcodes.c
    ${LUA_DIRECTORY}/loslib.c
    ${LUA_DIRECTORY}/lparser.c
    ${LUA_DIRECTORY}/lstate.c
    ${LUA_DIRECTORY}/lstring.c
    ${LUA_DIRECTORY}/lstrlib.c
    ${LUA_DIRECTORY}/ltable.c
    ${LUA_DIRECTORY}/ltablib.c
    ${LUA_DIRECTORY}/ltm.c
    ${LUA_DIRECTORY}/lundump.c
    ${LUA_DIRECTORY}/lutf8lib.c
    ${LUA_DIRECTORY}/lvm.c
    ${LUA_DIRECTORY}/lzio.c
)

source_group(TREE ${LUA_DIRECTORY} FILES ${LUA_SOURCE_FILES})

add_library(lua STATIC ${LUA_SOURCE_FILES})

target_include_directories(lua PUBLIC ${LUA_DIRECTORY})

if(APPLE)
    target_compile_definitions(lua PRIVATE LUA_USE_MACOSX)
elseif(UNIX)
    target_compile_definitions(lua PRIVATE LUA_USE_LINUX)
endif()
