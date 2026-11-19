cmake_minimum_required(VERSION 3.12)

project(web_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-Wall -Wextra -Werror=return-type)
endif()

include(CheckSymbolExists)
check_symbol_exists(epoll_pwait2 "sys/epoll.h" HAS_epoll_pwait2)
if (HAS_epoll_pwait2)
    add_definitions(-DHAS_epoll_pwait2)
endif()

find_package(Threads REQUIRED)

# 项目自带的双缓冲异步日志模块：源码就在 log/ 下，随本项目一起编译
add_library(async_log STATIC
    log/AsyncLogging.cpp
    log/CountDownLatch.cpp
    log/FileUtil.cpp
    log/LogFile.cpp
    log/Logging.cpp
    log/LogStream.cpp
    log/Thread.cpp)
target_include_directories(async_log PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/log)
target_link_libraries(async_log PUBLIC Threads::Threads)

include_directories(include)

# 每个 .cpp 编译成一个独立的示例程序
file(GLOB sources CONFIGURE_DEPENDS *.cpp)
foreach (source IN ITEMS ${sources})
    get_filename_component(name "${source}" NAME_WLE)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE async_log)
endforeach()