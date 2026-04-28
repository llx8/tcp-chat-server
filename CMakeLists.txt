cmake_minimum_required(VERSION 3.10)
project(Epoll_ChatRoom_CPP VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SOURCE_DIR "source")
set(INCLUDE_DIR "include")
set(CLIENT_DIR "${SOURCE_DIR}/client")
set(SERVER_DIR "${SOURCE_DIR}/server")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

include_directories(${INCLUDE_DIR})

find_package(Threads REQUIRED)

# 编译服务端
file(GLOB SERVER_FILES "${SERVER_DIR}/*.cpp")
add_executable(Server ${SERVER_FILES})
target_link_libraries(Server Threads::Threads)

# 编译客户端
file(GLOB CLIENT_FILES "${CLIENT_DIR}/*.cpp")
add_executable(Client ${CLIENT_FILES})
target_link_libraries(Client Threads::Threads)