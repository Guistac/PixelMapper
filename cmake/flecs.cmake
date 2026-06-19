
message(STATUS "== Including FLECS")

set(FLECS_SHARED OFF CACHE BOOL "" FORCE)
set(FLECS_STATIC ON CACHE BOOL "" FORCE)

add_subdirectory(${DEPENDENCIES_DIRECTORY}/flecs)

add_library(flecs ALIAS flecs_static)