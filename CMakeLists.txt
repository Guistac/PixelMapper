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


# System
set(ioDepsFolder ${DEPENDENCIES_DIRECTORY}/System)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/flecs.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/tinyxml2.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/asio.cmake)

# Scripting & Editor
include(${CMAKE_CURRENT_LIST_DIR}/cmake/lua.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/sol2.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/imguitextedit.cmake)

set(PixelMapperDeps
    dearimgui
    glm
	glad
    tinyxml2
	flecs
	lua
	sol2
	imguitextedit
	asio
)



#=========== Configure Executable =============

set(PROJECT_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

set(PROJECT_SRC_FILES
	${PROJECT_SRC_DIR}/main.cpp
	${PROJECT_SRC_DIR}/PixelMapper.h
	${PROJECT_SRC_DIR}/Common.h
	${PROJECT_SRC_DIR}/Shape.h
	${PROJECT_SRC_DIR}/Fixture.h
	${PROJECT_SRC_DIR}/Fixture.cpp
	${PROJECT_SRC_DIR}/CanvasBinding.h
	${PROJECT_SRC_DIR}/CanvasBinding.cpp
	${PROJECT_SRC_DIR}/Network.h
	${PROJECT_SRC_DIR}/Network.cpp
	${PROJECT_SRC_DIR}/Artnet.h
	${PROJECT_SRC_DIR}/Artnet.cpp
	${PROJECT_SRC_DIR}/ArtnetSender.h
	${PROJECT_SRC_DIR}/ArtnetSender.cpp
	${PROJECT_SRC_DIR}/Patch.h
	${PROJECT_SRC_DIR}/Patch.cpp
	${PROJECT_SRC_DIR}/App.h
	${PROJECT_SRC_DIR}/App.cpp
	${PROJECT_SRC_DIR}/PatchSerializer.h
	${PROJECT_SRC_DIR}/PatchSerializer.cpp
	${PROJECT_SRC_DIR}/FileWatcher.h
	${PROJECT_SRC_DIR}/Presets.h
	${PROJECT_SRC_DIR}/CueList.h
	${PROJECT_SRC_DIR}/CueList.cpp
	${PROJECT_SRC_DIR}/EffectBank.h
	${PROJECT_SRC_DIR}/EffectBank.cpp

	${PROJECT_SRC_DIR}/GenerativeEngine.h
	${PROJECT_SRC_DIR}/GenerativeEngine.cpp

	${PROJECT_SRC_DIR}/gui/PixelMapperGui.cpp
)

# utility to match source file hierarchy in IDE
source_group(TREE ${PROJECT_SRC_DIR} FILES ${PROJECT_SRC_FILES})

add_executable(PixelMapper ${PROJECT_SRC_FILES})
target_include_directories(PixelMapper PUBLIC ${PROJECT_SRC_DIR})
target_link_libraries(PixelMapper PRIVATE ${PixelMapperDeps})