/*
 * import bo in server process
 *
 * Copyright 2021
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * Sophia Gong BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <GL/glew.h>
#include <GL/glut.h>
//#include "glut_wrap.h"

#include "drm.h"
#include "xf86drmMode.h"
#include "xf86drm.h"
#include "amdgpu.h"
#include "amdgpu_drm.h"
#include "amdgpu_internal.h"

#define handle_error(msg) do { perror(msg); exit(EXIT_FAILURE); } while(0)
#define MAX_CARDS_SUPPORTED     128
static GLuint TexWidth = 256, TexHeight = 256;

int serv_recvfd()
{
	int server_sockfd, client_sockfd;
	int server_len, client_len;
	struct sockaddr_un server_address;
	struct sockaddr_un client_address;
	//int bytes;
	int fd;
	struct msghdr msg = {0};
	struct cmsghdr *cmsg;
	char buf[CMSG_SPACE(sizeof(int))], dummy;

	unlink ("server_socket");

	server_sockfd = socket (AF_UNIX, SOCK_STREAM, 0);
	server_address.sun_family = AF_UNIX;
	strcpy (server_address.sun_path, "server_socket");
	server_len = sizeof (server_address);

	bind (server_sockfd, (struct sockaddr *)&server_address, server_len);
	listen (server_sockfd, 5);

	printf ("Server is waiting for client connect...\n");

	client_len = sizeof (client_address);
	client_sockfd = accept (server_sockfd, (struct sockaddr *)&server_address, (socklen_t *)&client_len);
	if (client_sockfd == -1) {
		perror ("accept");
		exit (EXIT_FAILURE);
	}
	
	printf ("The server is waiting for client data...\n");

#if 0	
	if ((bytes = read (client_sockfd, &fd, 4)) == -1) {
		perror ("read");
		exit (EXIT_FAILURE);
	}
#endif

	memset(buf, '\0', sizeof(buf));
	
	struct iovec io = { .iov_base = &dummy, .iov_len = sizeof(dummy) };
	
	msg.msg_iov = &io;
	msg.msg_iovlen = 1;
	msg.msg_control = buf;
	msg.msg_controllen = sizeof(buf);
	
	if (recvmsg (client_sockfd, &msg, 0) < 0)
		handle_error ("Failed to receive message");
	
	cmsg = CMSG_FIRSTHDR(&msg);
	
	fd = *(int *) CMSG_DATA(cmsg);
	printf ("server receives fd %d\n", fd);
	
	close (client_sockfd);
	unlink ("server socket");

	return fd;
} 
	
int open_drmdev()
{
	drmDevicePtr devices[MAX_CARDS_SUPPORTED];
	drmVersionPtr version;
	int drm_count, drm_node;
	int drm_fd;

	drm_count = drmGetDevices2(0, devices, MAX_CARDS_SUPPORTED);
	if (drm_count < 0) {
		printf("drmGetDevices2() returned an error %d\n",
			drm_count);
		return 0;
	}

	//assert(drm_count == 1);
	
	if (devices[0]->bustype != DRM_BUS_PCI) {
		printf("failed to get pci drm device\n");
		return 0;
	}

	/* If this is not AMD GPU vender ID, skip*/
	if (devices[0]->deviceinfo.pci->vendor_id != 0x1002) {
		printf("it's not an amdgpu device\n");
		return 0;
	}

	drm_node = DRM_NODE_PRIMARY;

	drm_fd = open(devices[0]->nodes[drm_node],
	O_RDWR | O_CLOEXEC);
	
	if(drm_fd < 0){
		handle_error ("open drm device error.");
	} else {
		printf("Open drm fd %d\n", drm_fd);
	}

	version = drmGetVersion(drm_fd);
	if (!version) {
		printf("Warning: Cannot get version for %s."
			"Error is %s\n",
		devices[0]->nodes[drm_node],
		strerror(errno));
		close(drm_fd);
		return 0;
	}
	
	if (strcmp(version->name, "amdgpu")) {
		/* This is not AMDGPU driver, skip.*/
		drmFreeVersion(version);
		close(drm_fd);
		return 0;
	}
	
	drmFreeVersion(version);
	return drm_fd;
}
	
amdgpu_bo_handle import_texbo(int dev_fd, int tex_fd)
{
	uint32_t major_version, minor_version;
	amdgpu_device_handle device_handle;
	struct amdgpu_bo_import_result res = {0};
	amdgpu_bo_handle bo;
	int r = 0;
	
	r = amdgpu_device_initialize(dev_fd, &major_version,
		&minor_version, &device_handle);
	
	if (r) {
		printf("init drm device failed\n");
		return NULL;
	}
	
	r = amdgpu_bo_import(device_handle, amdgpu_bo_handle_type_dma_buf_fd, (uint32_t)tex_fd, &res);
	if (r) {
		printf("import bo failed\n");
		return NULL;
	}
	
	bo = res.buf_handle;
	
	return bo;
}
	
int main(int argc, char *argv[]) 
{
	int bo_fd;
	int dev_fd;
	amdgpu_bo_handle tex_bo;
	GLuint texture;
	int r;
	uint32_t *ptr;
	//uint32_t BUFFER_SIZE = 256*256*4;
	
	bo_fd = serv_recvfd();
	
	dev_fd = open_drmdev();
	if (!dev_fd) {
		printf("open drm device failed\n");
		return 0;
	}
	
	tex_bo = import_texbo(dev_fd, bo_fd);

		
	r = amdgpu_bo_cpu_map(tex_bo, (void **)&ptr);
	if (!ptr || r) {
		printf("remap import bo failed\n");
		return 0;
	}

#if 0
	// test cpu addr	
	for (int i = 0; i < (BUFFER_SIZE / 4); ++i)
		ptr[i] = 0x0f0f00ff;
#endif
	if (!tex_bo || !(tex_bo->cpu_ptr)) {
		printf("failed to get import texture bo\n");
		return 0;
	}
	printf("ptr[0] is 0x%08x\n", ptr[0]);
	
	glutInit(&argc, argv);
	glutInitWindowPosition(0, 0);
	glutInitWindowSize(600, 600);
	glutInitDisplayMode( GLUT_RGBA | GLUT_DEPTH | GLUT_DOUBLE );
	
	glutCreateWindow( "import-tex" );
	
	glEnable(GL_TEXTURE_2D);
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);

	glClearColor(0.0, 0.0, 0.0, 0.0);
	glClear(GL_COLOR_BUFFER_BIT);

	//glViewport(0, 0, TexWidth, TexHeight);
	
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TexWidth, TexHeight, 0,
		GL_RGBA, GL_UNSIGNED_BYTE, tex_bo->cpu_ptr);
	
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	
	glBegin(GL_POLYGON);
	glTexCoord2f(0, 0);  glVertex2f(0, 0);
	glTexCoord2f(1, 0);  glVertex2f(TexWidth, 0);
	glTexCoord2f(1, 1);  glVertex2f(TexWidth, TexHeight);
	glTexCoord2f(0, 1);  glVertex2f(0, TexHeight);
	glEnd();
	
	glutSwapBuffers();

	glutMainLoop();
	//close(dev_fd);
}
