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
    # Shared headers
    ${PROJECT_SRC_DIR}/shared/Common.h
    ${PROJECT_SRC_DIR}/shared/Shape.h
    ${PROJECT_SRC_DIR}/shared/PixelMapper.h
    ${PROJECT_SRC_DIR}/shared/Presets.h
    ${PROJECT_SRC_DIR}/shared/EffectBank.h
    ${PROJECT_SRC_DIR}/shared/Fixture.h
    ${PROJECT_SRC_DIR}/shared/Artnet.h
    ${PROJECT_SRC_DIR}/shared/Network.h
    ${PROJECT_SRC_DIR}/shared/network/CommandProtocol.h
    ${PROJECT_SRC_DIR}/shared/network/TelemetryProtocol.h
    ${PROJECT_SRC_DIR}/shared/network/SimpleJson.h
    ${PROJECT_SRC_DIR}/shared/network/AsioNetworkManager.h
    ${PROJECT_SRC_DIR}/shared/utils/FlecsUtils.h
    ${PROJECT_SRC_DIR}/shared/utils/Profiling.h
    ${PROJECT_SRC_DIR}/shared/components/PatchComponents.h
    ${PROJECT_SRC_DIR}/shared/components/AppComponents.h
    ${PROJECT_SRC_DIR}/shared/components/CueListComponents.h

    # Shared sources
    ${PROJECT_SRC_DIR}/shared/Artnet.cpp
    ${PROJECT_SRC_DIR}/shared/EffectBank.cpp
    ${PROJECT_SRC_DIR}/shared/Fixture.cpp
    ${PROJECT_SRC_DIR}/shared/Network.cpp
    ${PROJECT_SRC_DIR}/shared/network/AsioNetworkManager.cpp
    ${PROJECT_SRC_DIR}/shared/components/AppComponents.cpp
    ${PROJECT_SRC_DIR}/shared/components/PatchComponents.cpp

    # Server headers
    ${PROJECT_SRC_DIR}/server/App.h
    ${PROJECT_SRC_DIR}/server/ArtnetSender.h
    ${PROJECT_SRC_DIR}/server/CanvasBinding.h
    ${PROJECT_SRC_DIR}/server/CueList.h
    ${PROJECT_SRC_DIR}/server/FileWatcher.h
    ${PROJECT_SRC_DIR}/server/GenerativeEngine.h
    ${PROJECT_SRC_DIR}/server/Patch.h
    ${PROJECT_SRC_DIR}/server/PatchSerializer.h
    ${PROJECT_SRC_DIR}/server/network/ServerCommandHandlers.h

    # Server sources
    ${PROJECT_SRC_DIR}/server/App.cpp
    ${PROJECT_SRC_DIR}/server/ArtnetSender.cpp
    ${PROJECT_SRC_DIR}/server/CanvasBinding.cpp
    ${PROJECT_SRC_DIR}/server/CueList.cpp
    ${PROJECT_SRC_DIR}/server/GenerativeEngine.cpp
    ${PROJECT_SRC_DIR}/server/Patch.cpp
    ${PROJECT_SRC_DIR}/server/PatchSerializer.cpp
    ${PROJECT_SRC_DIR}/server/network/ServerCommandHandlers.cpp

    # Client headers
    ${PROJECT_SRC_DIR}/client/gui/ImGuiCanvas.h
    ${PROJECT_SRC_DIR}/client/gui/ImGuiHexView.h
    ${PROJECT_SRC_DIR}/client/utils/imguiconfig.h
    ${PROJECT_SRC_DIR}/client/network/ClientCommandHandlers.h

    # Client sources
    ${PROJECT_SRC_DIR}/client/gui/PixelMapperGui.cpp
    ${PROJECT_SRC_DIR}/client/network/ClientCommandHandlers.cpp

    # Main
    ${PROJECT_SRC_DIR}/main.cpp
)

# utility to match source file hierarchy in IDE
source_group(TREE ${PROJECT_SRC_DIR} FILES ${PROJECT_SRC_FILES})

# Library source lists
set(COMMON_SRC_FILES
    ${PROJECT_SRC_DIR}/shared/Artnet.cpp
    ${PROJECT_SRC_DIR}/shared/EffectBank.cpp
    ${PROJECT_SRC_DIR}/shared/Fixture.cpp
    ${PROJECT_SRC_DIR}/shared/Network.cpp
    ${PROJECT_SRC_DIR}/shared/network/AsioNetworkManager.cpp
    ${PROJECT_SRC_DIR}/shared/components/AppComponents.cpp
    ${PROJECT_SRC_DIR}/shared/components/PatchComponents.cpp
)

set(SERVER_SRC_FILES
    ${PROJECT_SRC_DIR}/server/App.cpp
    ${PROJECT_SRC_DIR}/server/ArtnetSender.cpp
    ${PROJECT_SRC_DIR}/server/CanvasBinding.cpp
    ${PROJECT_SRC_DIR}/server/CueList.cpp
    ${PROJECT_SRC_DIR}/server/GenerativeEngine.cpp
    ${PROJECT_SRC_DIR}/server/Patch.cpp
    ${PROJECT_SRC_DIR}/server/PatchSerializer.cpp
    ${PROJECT_SRC_DIR}/server/network/ServerCommandHandlers.cpp
)

set(CLIENT_SRC_FILES
    ${PROJECT_SRC_DIR}/client/gui/PixelMapperGui.cpp
    ${PROJECT_SRC_DIR}/client/network/ClientCommandHandlers.cpp
)

# 1. PixelMapper_CommonLib
add_library(PixelMapper_CommonLib OBJECT ${COMMON_SRC_FILES})
target_include_directories(PixelMapper_CommonLib PUBLIC ${PROJECT_SRC_DIR})
target_link_libraries(PixelMapper_CommonLib PUBLIC
    flecs
    asio
    glm
    sol2
    lua
)

# 2. PixelMapper_ServerLib
add_library(PixelMapper_ServerLib OBJECT ${SERVER_SRC_FILES})
target_include_directories(PixelMapper_ServerLib PUBLIC ${PROJECT_SRC_DIR})
target_link_libraries(PixelMapper_ServerLib PUBLIC
    PixelMapper_CommonLib
    flecs
    glad
    glfw
    sol2
    lua
    tinyxml2
    glm
)

# 3. PixelMapper_ClientLib
add_library(PixelMapper_ClientLib OBJECT ${CLIENT_SRC_FILES})
target_include_directories(PixelMapper_ClientLib PUBLIC ${PROJECT_SRC_DIR})
target_include_directories(dearimgui PUBLIC ${PROJECT_SRC_DIR}/client/utils)
target_link_libraries(PixelMapper_ClientLib PUBLIC
    PixelMapper_CommonLib
    dearimgui
    imguitextedit
    glad
    glfw
    sol2
    lua
    glm
    flecs
)

# Executable target linking object libraries and dependencies
add_executable(PixelMapper ${PROJECT_SRC_DIR}/main.cpp)
target_include_directories(PixelMapper PUBLIC ${PROJECT_SRC_DIR})
target_link_libraries(PixelMapper PRIVATE
    PixelMapper_CommonLib
    PixelMapper_ServerLib
    PixelMapper_ClientLib
    glfw
    glad
    glm
    flecs
    tinyxml2
    asio
    lua
    sol2
    imguitextedit
    dearimgui
)