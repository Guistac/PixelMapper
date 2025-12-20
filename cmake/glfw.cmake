
message(STATUS "== Including GLFW")

set( GLFW_BUILD_EXAMPLES  OFF CACHE INTERNAL "" )
set( GLFW_BUILD_TESTS     OFF CACHE INTERNAL "" )
set( GLFW_BUILD_DOCS      OFF CACHE INTERNAL "" )
set( GLFW_INSTALL         OFF CACHE INTERNAL "" )

#set(GLFW_BUILD_WAYLAND  OFF CACHE INTERNAL "")
#set(GLFW_BUILD_X11      ON CACHE INTERNAL "")

add_subdirectory(${DEPENDENCIES_DIRECTORY}/glfw)

set_property(TARGET glfw            PROPERTY FOLDER ${glfw3Folder})
set_property(TARGET update_mappings PROPERTY FOLDER ${glfw3Folder})