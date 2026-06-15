message(STATUS "== Including Sol2")

set(SOL2_DIRECTORY ${DEPENDENCIES_DIRECTORY}/sol2)

add_library(sol2 INTERFACE)

target_include_directories(sol2 INTERFACE ${SOL2_DIRECTORY}/include)

target_link_libraries(sol2 INTERFACE lua)
