cmake_minimum_required(VERSION 3.22.1)
project("sargo_spoofer")

add_library(main SHARED main.cpp)

find_library(log-lib log)
find_library(android-lib android)

target_link_libraries(main ${log-lib} ${android-lib})