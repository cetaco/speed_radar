# meu_app/CMakeLists.txt
cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
zephyr_include_directories(.)
project(radar_app)

target_sources(app PRIVATE src/main.c)
