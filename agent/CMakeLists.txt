cmake_minimum_required(VERSION 3.30)
project(ENPM808_Agent C)

set(CMAKE_C_STANDARD 11)

#add_executable(ENPM808_Agent main.c)
add_executable(ENPM808_Agent agent.c)

target_link_libraries(${PROJECT_NAME} winhttp)