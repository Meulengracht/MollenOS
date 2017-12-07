# Makefile for building the standard c++abi runtime-libraries for userspace
# This will produce the file libunwind.lib
INCLUDES = -I./include -I./ -I../include -I../include/cxx 

# to generate $(wildcard ./*.S) $(SOURCES:.S=.o)
SOURCES = $(wildcard ./arch/$(arch)/*.s) $(wildcard ./*.c) libunwind.cpp Unwind-EHABI.cpp
OBJECTS = $(SOURCES:.s=.o) $(SOURCES:.c=.o) $(SOURCES:.cpp=.o)

ASFLAGS = -f win32 -Xvc
CFLAGS = $(GCFLAGS) -DNDEBUG -D_DLL -DCRTDLL $(INCLUDES)
CXXFLAGS = $(GCXXFLAGS) -DNDEBUG -D_DLL -DCRTDLL -fno-ms-compatibility $(INCLUDES)
LFLAGS = $(GLFLAGS) /noentry /dll ../build/libc.lib ../build/libcrt.lib

# default-target
.PHONY: all
all: ../deploy/libunwind.dll

.PHONE: generate
generate: 
	gcc -E $(CFLAGS) UnwindRegistersRestore.S > restore.asm
	gcc -E $(CFLAGS) UnwindRegistersSave.S > save.asm
	perl x86_64-xlate.pl nasm restore.s restore.asm
	perl x86_64-xlate.pl nasm save.s save.asm
	rm restore.s
	rm save.s

../deploy/libunwind.dll: $(OBJECTS)
	$(LD) $(LFLAGS) $(OBJECTS) /out:$@

%.o : %.cpp
	$(CXX) -c $(CXXFLAGS) -o $@ $<
%.o : %.c
	$(CC) -c $(CFLAGS) -fno-ms-compatibility -o $@ $<
%.o : %.s
	$(AS) $(ASFLAGS) $< -o $@

.PHONY: clean
clean:
	@rm -f ../deploy/libunwind.dll
	@rm -f ../deploy/libunwind.lib
	#@rm -f $(OBJECTS)