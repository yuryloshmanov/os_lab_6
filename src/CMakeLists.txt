cmake_minimum_required(VERSION 3.17)
project(lab6)

set(CMAKE_CXX_STANDARD 14)
set(ZMQ_INCLUDE_DIR /usr/local/include)

find_library(ZMQ NAMES zmq PATHS /usr/local/lib)

add_library(mq_tools STATIC lib/src/mq_tools.cpp lib/mq_tools.hpp)

add_executable(server server.cpp)
add_executable(client client.cpp)

target_include_directories(server PUBLIC ${ZMQ_INCLUDE_DIR})
target_include_directories(client PUBLIC ${ZMQ_INCLUDE_DIR})
target_include_directories(mq_tools PUBLIC ${ZMQ_INCLUDE_DIR})

target_link_libraries(server PUBLIC ${ZMQ} mq_tools)
target_link_libraries(client PUBLIC ${ZMQ} mq_tools)
