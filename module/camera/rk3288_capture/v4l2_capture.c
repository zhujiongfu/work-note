/*
 * @file mxc_v4l2_capture.c
 *
 * Copyright 2017 zhujiongfu.
 *
 * Based on freescale v4l2 test application.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

/* Standard Include Files */
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>

/* Verification Test Environment Include Files */
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <asm/types.h>
#include <linux/videodev2.h>
#include <linux/v4l2-mediabus.h>
#include <string.h>
#include <malloc.h>
#include "v4l2_capture.h"

#define TEST_BUFFER_NUM 3

#define BIT(nr)		(1UL << (nr))
#define find_first_zero_bit(x) find_first_bit(~(x))

struct capture_config {
	char			device[50];
	int			shb_cnt;
	unsigned int		crop_width;
	unsigned int		crop_height;
	int			crop_top;
	int			crop_left;
	unsigned int		cap_width;
	unsigned int		cap_height;
	unsigned int		cap_fmt;
	int			cap_buf_cnt;
};

struct capture_buf {
	unsigned char		*start;
	size_t			offset;
	unsigned int		length;
};

struct capture_device {
	struct capture_buf	*cap_bufs;
	struct capture_config	*config;
	struct capture_data	*shm;
	char			*shb;
	int			shm_id;
};

static struct capture_config configs[] = {
	{
		.device = "/dev/video0",
		.shb_cnt = 2,
		.crop_width = 1024,
		.crop_height = 720,
		.crop_top = 0,
		.crop_left = 0,
		.cap_width = 640,
		.cap_height = 480,
		.cap_fmt = V4L2_PIX_FMT_NV12,
		.cap_buf_cnt = 2,
	},
};

/*
 * int g_rotate = 0;
 * int g_usb_camera = 0;
 */

static void print_pixelformat(char *prefix, int val)
{
	printf("%s: %c%c%c%c\n", prefix ? prefix : "pixelformat",
					val & 0xff,
					(val >> 8) & 0xff,
					(val >> 16) & 0xff,
					(val >> 24) & 0xff);
}

static int start_streaming(int fd_v4l, struct capture_device *dev)
{
        struct v4l2_buffer buf;
        enum v4l2_buf_type type;
        unsigned int i;
	int ret;

        for (i = 0; i < dev->config->cap_buf_cnt; i++) {
                memset(&buf, 0, sizeof (buf));
                buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;

		ret = ioctl(fd_v4l, VIDIOC_QUERYBUF, &buf);
                if (ret < 0) {
			perror("VIDIOC_QUERYBUF error");
                        return ret;
                }

                dev->cap_bufs[i].length = buf.length;
                dev->cap_bufs[i].offset = (size_t) buf.m.offset;
                dev->cap_bufs[i].start = mmap (NULL, 
					dev->cap_bufs[i].length,
					PROT_READ | PROT_WRITE, 
					MAP_SHARED,
					fd_v4l, dev->cap_bufs[i].offset);
		if (dev->cap_bufs[i].start == MAP_FAILED) {
			fprintf(stderr, "map %d buf error: %s", 
						i, strerror(errno));
			return -1;
		}
		memset(dev->cap_bufs[i].start, 0xFF, dev->cap_bufs[i].length);
        }

        for (i = 0; i < dev->config->cap_buf_cnt; i++) {
                memset(&buf, 0, sizeof (buf));
                buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                buf.memory = V4L2_MEMORY_MMAP;
                buf.index = i;
		buf.m.offset = dev->cap_bufs[i].offset;
	
		ret = ioctl(fd_v4l, VIDIOC_QBUF, &buf);
                if (ret < 0) {
			perror("VIDIOC_QBUF err");
                        return ret;
                }
        }

        type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	ioctl(fd_v4l, VIDIOC_STREAMOFF, &type);
	ret = ioctl(fd_v4l, VIDIOC_STREAMON, &type);
        if (ret < 0) {
		perror("VIDIOC_STREAMON error");
                return ret;
        }

        return 0;
}

static int stop_capturing(int fd_v4l)
{
        enum v4l2_buf_type type;

        type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        return ioctl (fd_v4l, VIDIOC_STREAMOFF, &type);
}

static int setup_v4l_capture(const int fd_v4l, struct capture_config *config)
{
        struct v4l2_format fmt;
        struct v4l2_control ctrl;
	struct v4l2_crop crop;
	/* struct v4l2_mxc_dest_crop of; */
	struct v4l2_fmtdesc ffmt;
	int ret = 0;

	ffmt.index = 0;
	ret = ioctl(fd_v4l, VIDIOC_ENUM_FMT, &ffmt);
	while (ret >= 0) {
		print_pixelformat("sensor frame format", ffmt.pixelformat);
		ffmt.index++;
		ret = ioctl(fd_v4l, VIDIOC_ENUM_FMT, &ffmt);
	}

#if 0
	/* UVC driver does not implement CROP */
	crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (ioctl(fd_v4l, VIDIOC_G_CROP, &crop) < 0) {
		printf("VIDIOC_G_CROP failed\n");
		return -1;
	}

	crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	crop.c.width = g_in_width;
	crop.c.height = g_in_height;
	crop.c.top = g_top;
	crop.c.left = g_left;
	if (ioctl(fd_v4l, VIDIOC_S_CROP, &crop) < 0) {
		printf("VIDIOC_S_CROP failed\n");
		return -1;
	}
#endif

        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.pixelformat = config->cap_fmt;
        fmt.fmt.pix.width = config->cap_width;
        fmt.fmt.pix.height = config->cap_height;
	fmt.fmt.pix.field = V4L2_FIELD_ANY;
        fmt.fmt.pix.bytesperline = config->cap_width;
       	fmt.fmt.pix.sizeimage = 0;

	ret = ioctl(fd_v4l, VIDIOC_S_FMT, &fmt);
        if (ret < 0) {
                printf("set format failed\n");
                return -1;
        }

        /*
	 * Set rotation
	 * It's mxc-specific definition for rotation.
	 */
#if 0 
	if (g_usb_camera != 1) {
		ctrl.id = V4L2_CID_PRIVATE_BASE + 0;
		ctrl.value = g_rotate;
		if (ioctl(fd_v4l, VIDIOC_S_CTRL, &ctrl) < 0)
		{
			printf("set ctrl failed\n");
			return 0;
		}
	}
#endif

        struct v4l2_requestbuffers req;
        memset(&req, 0, sizeof (req));
        req.count = config->cap_buf_cnt;
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_MMAP;
	ret = ioctl(fd_v4l, VIDIOC_REQBUFS, &req);
        if (ret < 0)
		perror("VIDIOC_REQBUFS error");

        return ret;
}

/* It is for circle buffer */
static void set_write_index(struct capture_data *shm)
{
	unsigned int l;

	sem_lock(shm->sem_id);
	/* l = shm->mask & (shm->mask + 1 - (shm->in - shm->out)); */
	l = shm->mask & (shm->in - shm->out);
	if (l == 0) {
		shm->in = shm->out + 1;
	}
	sem_unlock(shm->sem_id);
}

static void put_one_buffer(struct capture_device *dev, 
					struct v4l2_buffer *buf)
{
	unsigned int size = dev->shm->sizeimage;
	unsigned int in;
	unsigned int or;

	/* set_write_index(dev->shm); */
	sem_lock(dev->shm->sem_id);
	or = dev->shm->buf_flag & dev->shm->mask;
	switch (dev->shm->buf_flag) {
	case 0x0:
	case 0x2:
		in = BIT(0);
		break;
	case 0x1:
		in = BIT(1);
		break;
	default:
		in = dev->shm->last_in;
		dev->shm->buf_flag &= ~(dev->shm->last_in);
	}
	sem_unlock(dev->shm->sem_id);
	memcpy(dev->shb + size * (in - 1), 
		(dev->cap_bufs + buf->index)->start, size);

	sem_lock(dev->shm->sem_id);
	dev->shm->buf_flag |= in;
	dev->shm->last_in = in;
	sem_unlock(dev->shm->sem_id);
	/* printf("flag: %u\n", dev->shm->buf_flag); */
}

static void do_handle_cap(int fd_v4l, struct capture_device *dev)
{
	struct v4l2_buffer buf;
	int ret;

	for (;;) {
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		ret = ioctl(fd_v4l, VIDIOC_DQBUF, &buf);
		if (ret < 0) {
			perror("VIDIOC_DQBUF error");
			break;
		}
		put_one_buffer(dev, &buf);
		ioctl(fd_v4l, VIDIOC_QBUF, &buf);
                memset(&buf, 0, sizeof (buf));
	}
}

static void free_shm_and_sem(struct capture_device *dev)
{
	free_sem(dev->shm->sem_id);
	free_shm(dev->shm_id);
}

static int init_shm_with_fmt(struct capture_device *dev, 
			struct v4l2_format *fmt)
{
	int ret;
	size_t size;

	size = sizeof(struct capture_data) + 
			fmt->fmt.pix.sizeimage * dev->config->shb_cnt;
	dev->shm = (struct capture_data *)alloc_shm(&dev->shm_id, 
			MODULE_SHM_ID, size, 0666 | IPC_CREAT);
	if ((void *)dev->shm == (void *)-1) {
		printf("Failed to init shm.\n");
		return -1;
	}
	dev->shb = ((char *)dev->shm) + sizeof(struct capture_data);
	dev->shm->width = fmt->fmt.pix.width;
	dev->shm->height = fmt->fmt.pix.height;
	dev->shm->fmt = fmt->fmt.pix.pixelformat;
	dev->shm->sizeimage = fmt->fmt.pix.sizeimage;

	dev->shm->in = 0;
	dev->shm->out = 0;
	dev->shm->buf_flag = 0x0;
	dev->shm->buf_cnt = dev->config->shb_cnt;
	/* if circle, size must be the power of 2 */
	dev->shm->mask = (1 << dev->config->shb_cnt) - 1;

	dev->shm->sem_id = get_and_init_sem(KEY_PATH, MODULE_SEM_ID);
	if (dev->shm->sem_id < 0) {
		printf("Failed to init sem.\n");
		ret = -1;
		goto err_sem;
	}
	 
	return 0;

err_shb:
	free_sem(dev->shm->sem_id);
err_sem:
	shmctl(dev->shm_id, IPC_RMID, 0);

	return ret;
}

static int start_capturing(const int fd_v4l, struct capture_device *dev)
{
        struct v4l2_format fmt;
	int ret = 0;
	int fd_flags = fcntl(fd_v4l, F_GETFL);

        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	ret = ioctl(fd_v4l, VIDIOC_G_FMT, &fmt);
        if (ret < 0) {
		perror("VIDIOC_G_FMT error");
		return ret;
        } else {
                printf("\t Width = %d", fmt.fmt.pix.width);
                printf("\t Height = %d", fmt.fmt.pix.height);
                printf("\t Image size = %d\n", fmt.fmt.pix.sizeimage);
		print_pixelformat(0, fmt.fmt.pix.pixelformat);
        }

	ret = init_shm_with_fmt(dev, &fmt);
	if (ret < 0) {
		printf("Failed to init shm.\n");
		return ret;
	}
	
	ret = start_streaming(fd_v4l, dev);
        if (ret < 0) {
                printf("start_streaming failed\n");
		goto err_streaming;
        }

	fcntl(fd_v4l, F_SETFL, fd_flags);
        for (;;) {
		fd_set fds;

		FD_ZERO(&fds);
		FD_SET(fd_v4l, &fds);

		ret = select(fd_v4l + 1, &fds, NULL, NULL, NULL);
		if (ret == -1 || ret == 0) {
			perror("select error");
			sleep(1);
			continue;
		}

		ret = FD_ISSET(fd_v4l, &fds);
		if (ret)
			do_handle_cap(fd_v4l, dev);
	}

err_streaming:
	free_shm_and_sem(dev);

        return ret;
}

int main(int argc, char **argv)
{
	struct capture_device *dev;
        int fd_v4l;
	int ret = 0;

	dev = (struct capture_device *)malloc(sizeof(struct capture_device));
	if (!dev) {
		printf("Failed to allocate memory.\n");
		return -1;
	}

	dev->config = &configs[0];
	dev->cap_bufs = (struct capture_buf *)malloc(dev->config->cap_buf_cnt
				* sizeof(struct capture_buf));
	if (!dev->cap_bufs) {
		printf("Failed to alloc mem.\n");
		ret = -1;
		goto err_mem;
	}

	fd_v4l = open(dev->config->device, O_RDWR, 0);
	if (fd_v4l < 0) {
		printf("Unable to open %s\n", dev->config->device);
		ret = fd_v4l;
		goto err_open;
	}

        ret = setup_v4l_capture(fd_v4l, dev->config);
	if (ret < 0)
		goto err_setup;

        ret = start_capturing(fd_v4l, dev);
	if (ret < 0)
		printf("Failed to start capturing\n");

	ret = stop_capturing(fd_v4l);
        if (ret < 0)
                printf("stop_capturing failed\n");

err_setup:
        close(fd_v4l);
err_open:
	free(dev->cap_bufs);
err_mem:
	free(dev);
	
	return ret;
}
