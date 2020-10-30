cmake_minimum_required(VERSION 3.14)
project(client)

set(CMAKE_CXX_STANDARD 14)

include_directories(include)

add_executable(client
        include/simple_tcp_client.hpp main.cpp)
