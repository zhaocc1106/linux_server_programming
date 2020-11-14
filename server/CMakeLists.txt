cmake_minimum_required(VERSION 3.10.2)
project(server)

set(CMAKE_CXX_STANDARD 14)

include_directories(include)

add_executable(server
        main.cpp
        include/simple_tcp_server.hpp
        include/simple_file_down_server.hpp
        include/tools.hpp include/multi_connection_server.hpp
        include/libev_multi_con_server.hpp
        include/libev_half_reactive_server.hpp
        include/thread_safe_queue.hpp)

# boost begin
set(Boost_DETAILED_FAILURE_MSG ON)
set(Boost_DEBUG on)
set(BOOST_ROOT /home/zhaocc/environments/boost_1_74_0)
set(BOOST_INCLUDEDIR ${BOOST_ROOT})
set(BOOST_LIBRARYDIR ${BOOST_ROOT}/stage/lib)
find_package(Boost COMPONENTS REQUIRED)
include_directories(${BOOST_ROOT})
target_link_libraries(server ${Boost_LIBRARIES})
# boost end

target_link_libraries(server pthread event)