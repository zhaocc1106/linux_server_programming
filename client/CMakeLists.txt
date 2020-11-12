cmake_minimum_required(VERSION 3.10.2)
project(client)

set(CMAKE_CXX_STANDARD 14)

include_directories(include)

add_executable(client
        include/simple_tcp_client.hpp main.cpp include/libevent_no_block_client.hpp)

# boost begin
set(Boost_DETAILED_FAILURE_MSG ON)
set(Boost_DEBUG on)
set(BOOST_ROOT /home/zhaocc/environments/boost_1_74_0)
set(BOOST_INCLUDEDIR ${BOOST_ROOT})
set(BOOST_LIBRARYDIR ${BOOST_ROOT}/stage/lib)
find_package(Boost COMPONENTS REQUIRED)
include_directories(${BOOST_ROOT})
target_link_libraries(client ${Boost_LIBRARIES})
# boost end

target_link_libraries(client pthread event)