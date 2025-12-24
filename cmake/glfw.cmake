
message(STATUS "== Including GLFW")

SET(GLFW_BUILD_DOCS FALSE CACHE BOOL "" FORCE)
SET(GLFW_INSTALL FALSE CACHE BOOL "" FORCE)

add_subdirectory(${DEPENDENCIES_DIRECTORY}/glfw)

if(LINUX)
    target_link_libraries(glfw PUBLIC
        X11
    )
endif()