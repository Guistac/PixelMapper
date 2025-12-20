
message(STATUS "== Including ASIO")

#SET(GLFW_BUILD_DOCS FALSE CACHE BOOL "" FORCE)

set(ASIO_DIRECTORY ${DEPENDENCIES_DIRECTORY}/asio)

add_library(asio INTERFACE)

target_include_directories(asio INTERFACE
    ${ASIO_DIRECTORY}/asio/include/
)

target_compile_definitions(asio INTERFACE
    ASIO_STANDALONE
    ASIO_HAS_STD_INVOKE_RESULT
)