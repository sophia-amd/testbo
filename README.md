# testbo
This test aims to verify import/export amdgpu bo in different processes

To compile:
1. sudo apt-get install freeglut3-dev libgles2 libdrm-amdgpu1
2. git clone https://gitlab.freedesktop.org/mesa/drm
3. cd testbo
4. set local drm path to Makefile, replace '-I/home/sophia/Sophia/drm -I/home/sophia/Sophia/drm/amdgpu'
5. make

To test:
1. sudo ./import_bo
2. sudo ./export_bo (export_bo will draw a red rectangle)
