cmake_minimum_required(VERSION 3.30)
project(Spaghetti_Agent C)

set(CMAKE_C_STANDARD 11)

add_executable(Spaghetti_Agent agent.c)

target_link_libraries(${PROJECT_NAME} winhttp)
