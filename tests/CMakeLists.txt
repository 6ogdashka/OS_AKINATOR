cmake_minimum_required(VERSION 3.16)

find_package(Qt6 REQUIRED COMPONENTS Test)

# --- tst_gamecore: данные, filterCandidates, три подсказки ---
add_executable(tst_gamecore tst_gamecore.cpp)
target_link_libraries(tst_gamecore PRIVATE gamecore Qt6::Test)
add_test(NAME tst_gamecore COMMAND tst_gamecore)

# --- tst_protocol: (де)сериализация NDJSON ---
add_executable(tst_protocol tst_protocol.cpp)
target_link_libraries(tst_protocol PRIVATE akinator_protocol Qt6::Core Qt6::Test)
add_test(NAME tst_protocol COMMAND tst_protocol)
