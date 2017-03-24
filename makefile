# Definitions
CC = clang
CXX = clang++
LD = lld
ASM = nasm
DEBUG = -g

CFLAGS = -Wall -c $(DEBUG)
LFLAGS = /nodefaultlib /subsystem:native /entry:_kmain

