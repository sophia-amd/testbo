/*
 * export bo in client process
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

#define _GNU_SOURCE         /* See feature_test_macros(7) */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <xf86drmMode.h>
#include <xf86drm.h>
#include <libdrm/amdgpu.h>
#include <libdrm/amdgpu_drm.h>
#define handle_error(msg) do { perror(msg); exit(EXIT_FAILURE); } while(0)
#define MAX_CARDS_SUPPORTED     128
#define BUFFER_SIZE 256*256*4
#define BUFFER_ALIGN 0

void send_fd(int fd)
{
	struct msghdr msg = {0};
	struct cmsghdr *cmsg;
	char buf[CMSG_SPACE(sizeof(int))], dummy;
	struct sockaddr_un address;
	int sockfd;
	int len;
	//int bytes;
	int result;

	if ((sockfd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
		perror ("socket");
		exit (EXIT_FAILURE);
	}
	
	address.sun_family = AF_UNIX;
	strcpy (address.sun_path, "server_socket");
	len = sizeof (address);
	
	result = connect (sockfd, (struct sockaddr *)&address, len);
	if (result == -1) {
		printf ("ensure the server is up\n");
		perror ("connect");
		exit (EXIT_FAILURE);
	}
#if 0	
	if ((bytes = write(sockfd, &fd, 4)) == -1) {
		perror ("write");
		exit (EXIT_FAILURE);
	}
#endif

	memset(buf, '\0', sizeof(buf));
	struct iovec io = { .iov_base = &dummy, .iov_len = sizeof(dummy) };
	msg.msg_iov = &io;
	msg.msg_iovlen = 1;
	msg.msg_control = buf;
	msg.msg_controllen = sizeof(buf);
	cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	cmsg->cmsg_len = CMSG_LEN(sizeof(int));
	*(int *) CMSG_DATA(cmsg) = fd;
	//memcpy ((int *) CMSG_DATA(cmsg), fds, n * sizeof (int));

	if (sendmsg (sockfd, &msg, 0) < 0)
		handle_error ("Failed to send message");
	
	printf("client send fd %d\n", fd);
	close (sockfd);
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
		printf("Opened drm fd %d\n", drm_fd);
	}
	
	version = drmGetVersion(drm_fd);
	if (!version) {
		printf("Warning: Cannot get version for %s.\n",
			devices[0]->nodes[drm_node]);
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
	
amdgpu_bo_handle amdgpu_mem_alloc_map(
	amdgpu_device_handle device_handle,
	uint64_t size,
	uint64_t alignment,
	uint32_t type,
	uint64_t flags)
{
	struct amdgpu_bo_alloc_request req = {0};
	amdgpu_bo_handle buf_handle = NULL;
	uint32_t *ptr;
	int r;
	
	req.alloc_size = size;
	req.phys_alignment = alignment;
	req.preferred_heap = type;
	req.flags = flags;
	
	r = amdgpu_bo_alloc(device_handle, &req, &buf_handle);
	if (r) {
		printf("alloc amdgpu bo failed\n");
		return NULL;
	}
	
	r = amdgpu_bo_cpu_map(buf_handle, (void **)&ptr);
	if (!ptr || r) {
		printf("map amdgpu bo failed\n");
		goto error_free_bo;
	}
	
	//Todo: draw with ogl fbo
	for (int i = 0; i < (BUFFER_SIZE / 4); ++i)
		ptr[i] = 0xff0000ff;
	
	return buf_handle;
	
error_free_bo:
	r = amdgpu_bo_free(buf_handle);
	return NULL;
}
	
int main(int argc, char *argv[]) 
{
	int dev_fd;
	uint32_t major_version, minor_version;
	amdgpu_device_handle device_handle;
	amdgpu_bo_handle buf_handle = NULL;
	uint32_t shared_handle;
	int shared_fd;
	int r;
	
	dev_fd = open_drmdev();
	if (dev_fd)
	{
		r = amdgpu_device_initialize(dev_fd, &major_version,
			&minor_version, &device_handle);
	
		if (r) {
			printf("init drm device failed\n");
		close(dev_fd);
		return 0;
		}
	}
	
	buf_handle = amdgpu_mem_alloc_map(device_handle, BUFFER_SIZE, BUFFER_ALIGN, AMDGPU_GEM_DOMAIN_GTT, 0);
	if (!buf_handle) {
		printf("alloc bo failed\n");
		return 0;
	}
	
	r = amdgpu_bo_export(buf_handle, amdgpu_bo_handle_type_dma_buf_fd, &shared_handle);
	if (r) {
		printf("export bo failed\n");
		return 0;
	}
	
	shared_fd = (int)shared_handle;
	send_fd (shared_fd);
	//amdgpu_bo_cpu_unmap(buf_handle);
	//amdgpu_bo_free(buf_handle);
	//close(dev_fd);
	//exit(EXIT_SUCCESS);
}
