cmake_minimum_required(VERSION 3.20)
project(tcgp CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra)

file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS src/*.cpp)
list(REMOVE_ITEM ENGINE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/run.cpp)
add_library(tcgp_engine ${ENGINE_SOURCES})
target_include_directories(tcgp_engine PUBLIC src)

add_executable(tcgp src/main.cpp)
target_link_libraries(tcgp tcgp_engine)

find_package(Threads REQUIRED)
add_executable(tcgp-run src/run.cpp)
target_link_libraries(tcgp-run tcgp_engine Threads::Threads)

add_executable(test_rules tests/test_rules.cpp)
target_link_libraries(test_rules tcgp_engine)

add_executable(test_effects tests/test_effects.cpp)
target_link_libraries(test_effects tcgp_engine)

add_executable(test_players tests/test_players.cpp)
target_link_libraries(test_players tcgp_engine)

add_executable(check_states tests/check_states.cpp)
target_link_libraries(check_states tcgp_engine)

enable_testing()
add_test(NAME rules COMMAND test_rules)
add_test(NAME effects COMMAND test_effects)
add_test(NAME players COMMAND test_players)
