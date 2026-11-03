cmake_minimum_required(VERSION 3.16)

# gamecore — SHARED-библиотека на чистом C++17 (без Qt).
# Содержит данные (имена/тексты/матрицу), filterCandidates и три подсказки.
add_library(gamecore SHARED
    src/gamecore.cpp
    generated/data.cpp
)

target_compile_definitions(gamecore PRIVATE GAMECORE_BUILD)

target_include_directories(gamecore
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/generated
)

if(NOT MSVC)
    # Символы из gamecore.h помечены GAMECORE_EXPORT, остальное скрыто.
    set_target_properties(gamecore PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
    )
endif()
