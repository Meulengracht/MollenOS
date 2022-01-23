cmake_minimum_required(VERSION 3.14.3)
project(file2c C)

add_executable (file2c main.c)
install(TARGETS file2c DESTINATION bin)
