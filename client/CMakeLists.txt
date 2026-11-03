cmake_minimum_required(VERSION 3.16)

# akinator-client — GUI-клиент (Qt Widgets) поверх общего протокола.
add_executable(akinator-client
    main.cpp
    connection.cpp
    connection.h
    mainwindow.cpp
    mainwindow.h
)

target_include_directories(akinator-client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(akinator-client
    PRIVATE
        Qt6::Core
        Qt6::Network
        Qt6::Widgets
        akinator_protocol
        gamecore
)
