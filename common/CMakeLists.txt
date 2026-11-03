cmake_minimum_required(VERSION 3.16)

# akinator_protocol — STATIC-библиотека NDJSON-протокола на Qt::Core.
add_library(akinator_protocol STATIC
    protocol.cpp
    protocol.h
)

target_link_libraries(akinator_protocol PUBLIC Qt6::Core)
target_include_directories(akinator_protocol PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
