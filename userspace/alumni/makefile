# Makefile for building a generic userspace application
# Include all the definitions for os
include ../../config/common.mk

APP_NAME := alumni
APP_SOURCES_CXX := surfaces/surface_vali.cpp \
			      $(wildcard *.cpp)

LFLAGS := $(lib_path)/libfreetype.lib

include ../application.mk