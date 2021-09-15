CC = gcc

# compiler flags:
#  -g    adds debugging information to the executable file
#  -Wall turns on most, but not all, compiler warnings
CFLAGS  = -g -O2 -Wall -DHAVE_LIBDRM_ATOMIC_PRIMITIVES=1 # -E -v  #-D__USE_BSD

# define any directories containing header files other than /usr/include
#
INCLUDES = -I/usr/include/libdrm -I/home/sophia/Sophia/drm -I/home/sophia/Sophia/drm/amdgpu

# define library paths in addition to /usr/lib
LFLAGS = -L/usr/lib/x86_64-linux-gnu

# define any libraries to link into executable:
#   if I want to link in libraries (libx.so or libx.a) I use the -llibname
#   option, something like (this will link in libmylib.so and libm.so: -lmylib -lm
LIBS = -ldrm -ldrm_amdgpu -lglut -lGL 

# the build target executable:
default: export_bo import_bo

export_bo:  export_bo.o
	$(CC) -o export_bo export_bo.o $(LFLAGS) $(LIBS)

export_bo.o: export_bo.c
	$(CC) $(CFLAGS) $(INCLUDES) -c export_bo.c

import_bo:  import_bo.o
	$(CC) -o import_bo import_bo.o $(LFLAGS) $(LIBS)

import_bo.o: import_bo.c
	$(CC) $(CFLAGS) $(INCLUDES) -c import_bo.c

clean:
	rm *.o *_bo
