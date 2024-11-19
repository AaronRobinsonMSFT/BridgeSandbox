cmake_minimum_required (VERSION 3.20)
project (bridge LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Add the generated inc/ directory
include_directories(${GENERATED_INC})

# Add JDK to path
include_directories(${JDK_INC})
if (WIN32)
    add_compile_definitions(WINDOWS)
    include_directories(${JDK_INC}/win32)
elseif (APPLE)
    add_compile_definitions(MACOS)
    include_directories(${JDK_INC}/darwin)
elseif (UNIX)
    add_compile_definitions(LINUX)
    include_directories(${JDK_INC}/linux)
endif()

add_library(bridge SHARED bridge.cpp)

install(TARGETS bridge DESTINATION $<CONFIG>/${CMAKE_GENERATOR_PLATFORM})
if (WIN32)
    install(FILES $<TARGET_PDB_FILE:${PROJECT_NAME}> DESTINATION $<CONFIG>/${CMAKE_GENERATOR_PLATFORM} OPTIONAL)
endif()