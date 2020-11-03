cmake_minimum_required(VERSION 3.14)
project(server)

set(CMAKE_CXX_STANDARD 14)

include_directories(include)

add_executable(server
        include/simple_tcp_server.hpp main.cpp include/simple_file_down_server.hpp)

target_link_libraries(server pthread)
