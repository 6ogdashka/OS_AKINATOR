cmake_minimum_required(VERSION 3.16)

project(akinator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Qt meta-object generation (client GUI + server QObject classes + tests).
set(CMAKE_AUTOMOC ON)

# Единая папка вывода: важно для Windows, чтобы akinator-server.exe,
# akinator-client.exe и тесты находили gamecore.dll рядом с собой.
# RUNTIME — exe, LIBRARY — .dll/.so, ARCHIVE — статические .a/.lib.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# MSVC без /utf-8 неверно читает русские строковые литералы в исходниках.
if(MSVC)
    add_compile_options(/utf-8)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Network Widgets Test)

enable_testing()

add_subdirectory(gamecore)
add_subdirectory(common)
add_subdirectory(server)
add_subdirectory(client)
add_subdirectory(tests)
