cmake_minimum_required(VERSION 3.16)

# akinator-server — авторитетный сервер: лобби на 2 игроков, кандидаты, ходы,
# подсказки, угадывание, реванш. Qt::Network + общий протокол + игровое ядро.
add_executable(akinator-server
    main.cpp
    server.cpp
    server.h
)

target_include_directories(akinator-server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(akinator-server
    PRIVATE
        Qt6::Core
        Qt6::Network
        akinator_protocol
        gamecore
)
