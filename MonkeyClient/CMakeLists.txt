cmake_minimum_required(VERSION 3.22)
project(MonkeyClient CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(SOURCES
    src/main.cpp
    src/Crash/CrashLogger.cpp
    src/IL2CPP/GameAPI.cpp
    src/Menu/UIButton.cpp
    src/Menu/MenuManager.cpp
    src/Mods/Settings.cpp
)

add_library(MonkeyClient SHARED ${SOURCES})

target_include_directories(MonkeyClient PRIVATE src)

# -Oz + no exceptions/RTTI matches typical Quest mod builds
target_compile_options(MonkeyClient PRIVATE -Oz -fno-exceptions -fno-rtti -fvisibility=hidden)

find_library(log-lib log)
target_link_libraries(MonkeyClient PRIVATE ${log-lib})

set_target_properties(MonkeyClient PROPERTIES
    OUTPUT_NAME "MonkeyClient"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build"
)
