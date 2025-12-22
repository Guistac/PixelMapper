cmake_minimum_required(VERSION 3.20.0)

#======= Configure Workspace ========
project(PixelMapperProject
		VERSION 0.2
		DESCRIPTION "Nique Madrix"
		LANGUAGES C CXX)



#======== Configure Compiler ========
set(CMAKE_CXX_STANDARD 20) #we are using C++20
set(CMAKE_CXX_STANDARD_REQUIRED True)



#========== Configure Dependencies ===========‡

set(DEPENDENCIES_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/deps)

# GUI
set(GuiDepsFolder ${DEPENDENCIES_DIRECTORY}/Gui)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/glfw.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/dearimgui.cmake)

# Graphics
set(graphicsDepsFolder ${DEPENDENCIES_DIRECTORY}/Graphics)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/glad.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/glm.cmake)


# I/O
set(ioDepsFolder ${DEPENDENCIES_DIRECTORY}/IO)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/tinyxml2.cmake)

set(PixelMapperDeps
	glfw
    dearimgui
    glm
	glad
    tinyxml2
)



#=========== Configure Executable =============

set(PROJECT_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

set(PROJECT_SRC_FILES
	${PROJECT_SRC_DIR}/main.cpp
	${PROJECT_SRC_DIR}/PixelMapper.h
	${PROJECT_SRC_DIR}/PixelMapper.cpp
)

# utility to match source file hierarchy in IDE
source_group(TREE ${PROJECT_SRC_DIR} FILES ${PROJECT_SRC_FILES})

add_executable(PixelMapper ${PROJECT_SRC_FILES})
target_include_directories(PixelMapper PUBLIC ${PROJECT_SRC_DIR})
target_link_libraries(PixelMapper PUBLIC ${PixelMapperDeps})