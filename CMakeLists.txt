cmake_minimum_required(VERSION 2.4)

project(picasso CXX)

message(status ${CMAKE_INCLUDE_PATH})

set(CMAKE_BUILD_TYPE Release)


file(
        GLOB_RECURSE
        source_files
        source/*
)

add_executable(picasso ${source_files})