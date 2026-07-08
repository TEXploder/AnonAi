cmake_minimum_required(VERSION 3.20)

project(AnonAI VERSION 0.1.0 LANGUAGES CXX)

add_library(AnonAI SHARED
    src/AiClient.cpp
    src/AnonAIPlugin.cpp
    src/ChatHistory.cpp
    src/Dialogs.cpp
    src/JsonUtil.cpp
    src/Settings.cpp
    src/Utf.cpp
)

target_compile_features(AnonAI PRIVATE cxx_std_17)
target_compile_definitions(AnonAI PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_include_directories(AnonAI PRIVATE src)
target_link_libraries(AnonAI PRIVATE winhttp crypt32 comctl32)

set_target_properties(AnonAI PROPERTIES
    PREFIX ""
    OUTPUT_NAME "AnonAI"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

if(MINGW)
    target_link_options(AnonAI PRIVATE -static -static-libgcc -static-libstdc++)
endif()
