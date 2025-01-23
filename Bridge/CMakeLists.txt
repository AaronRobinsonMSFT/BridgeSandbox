cmake_minimum_required (VERSION 3.20)
project (bridge LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add the generated inc/ directory
include_directories(${GENERATED_INC})

include(FetchContent)
FetchContent_Declare(
    dncp
    GIT_REPOSITORY https://github.com/AaronRobinsonMSFT/DNCP.git
    GIT_TAG        803b819cb6476571cf6974a618b8dcb87ba1f86d
)
FetchContent_MakeAvailable(dncp)

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

if (MSVC)
    add_compile_options(/permissive-)
    add_compile_options(/Zi)
endif()

add_library(bridge SHARED bridge.cpp tracker.cpp)

if (MSVC)
    target_link_options(bridge PUBLIC /DEBUG)
endif()

target_link_libraries(bridge dncp::dncp)

if (NOT MSVC)
    target_link_libraries(bridge dncp::winhdrs)
endif()

install(TARGETS bridge DESTINATION $<CONFIG>/${CMAKE_GENERATOR_PLATFORM})
if (WIN32)
    install(FILES $<TARGET_PDB_FILE:${PROJECT_NAME}> DESTINATION $<CONFIG>/${CMAKE_GENERATOR_PLATFORM})
endif()