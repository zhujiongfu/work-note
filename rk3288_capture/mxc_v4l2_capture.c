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
#include <errno.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* Verification Test Environment Include Files */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/sem.h>
#include <fcntl.h>
#include <asm/types.h>
#include <linux/videodev2.h>
#include <linux/v4l2-mediabus.h>
#include <string.h>
#include <malloc.h>

#define TEST_BUFFER_NUM 3
#define KEY_PATH	"/tmp"
#define MODULE_SEM_ID	"0x333"
#define MODULE_SHD_ID	"0x666"
#define MODULE_SHB_ID	"0x999"

#define init_shm(shm_id, mem, id, size) \
({ \
	key_t __key; \
	__key = ftok(KEY_PATH, id); \
	if (__key) { \
		printf("ftok of 0x%0x id error.\n", id); \
		return -1; \
	} \
	shm_id = shmget(__key, size, 0666 | IPC_CREAT); \
	if (shd_id < 0) { \
		printf("shmget of 0x%0x id error.\n", id); \
		return -1; \
	} \
	mem = shmat(shm_id, 0, 0); \
	if (mem == (void *)-1) { \
		printf("shmat of 0x%0x id error.\n", id); \
		return -1; \
	} \
 })

#define free_shm(id) \
({ \
	int __ret; \
	ret = shmctl(id, IPC_RMID, 0); \
	if (ret < 0) \
		printf("shmctl of %d error.\n", id); \
 })

#define sem_op(sem_id, op) \
({ \
	struct sembuf __sembuf; \
	int __ret; \
	__sembuf.sem_num = 0; \
	__sembuf.sem_op = op; \
	__sembuf.sem_flg = SEM_UNDO; \
	do { \
		__ret = semop(sem_id, &__sembuf, 1); \
		if (__ret == 0) \
			break; \
		fprintf(stderr, "sem op %d error: %s", op, strerror(errno)); \
		sleep(1); \
	while (1); \
 })

#define sem_lock(sem_id) sem_op(sem_id, -1)
#define sem_unlock(sem_id) sem_op(sem_id, 1)

struct capture_config {
	char device[50];
	int shb_cnt;
	struct {
		unsigned int width;
		unsigned int height;
		int top;
		int left;
	} crop;
	struct {
		unsigned int width;
		unsigned int height;
		unsigned int fmt;
		int buf_cnt;
	} cap;
};

struct capture_buf {
	unsigned char *start;
	size_t offset;
	unsigned int length;
};

struct capture_device {
	struct capture_buf *cap_bufs;
	struct capture_config *config;
	struct capture_data *shd;
	int		shd_id;
	char		*shb;
	int		shb_id;
	bool		is_first_loop;
};

static struct capture_buf buffers[TEST_BUFFER_NUM];
static struct capture_config configs[] = {
	{
		.device = "/dev/video0",
		.crop = {
			.width = 1024,
			.height = 720,
			.top = 0,
			.left = 0,
		},
		.cap = {
			.width = 640,
			.height = 480,
			.fmt = V4L2_PIX_FMT_NV12,
			.buf_cnt = 8,
		},
	},
};

int g_capture_count = 100;
int g_rotate = 0;
int g_usb_camera = 0;

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

        for (i = 0; i < dev->config->cap->buf_cnt; i++) {
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

        for (i = 0; i < dev->config->cap->buf_cnt; i++) {
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

	ffmt.index = 0;
	while (ioctl(fd_v4l, VIDIOC_ENUM_FMT, &ffmt) >= 0) {
		print_pixelformat("sensor frame format", ffmt.pixelformat);
		ffmt.index++;
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
        fmt.fmt.pix.pixelformat = config->cap.fmt;
        fmt.fmt.pix.width = config->cap.width;
        fmt.fmt.pix.height = config->cap.height;
	fmt.fmt.pix.field = V4L2_FIELD_ANY;
        fmt.fmt.pix.bytesperline = config->cap.width;
       	fmt.fmt.pix.sizeimage = 0;

        if (ioctl(fd_v4l, VIDIOC_S_FMT, &fmt) < 0) {
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
        req.count = config->cap->buf_cnt;
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_MMAP;

        if (ioctl(fd_v4l, VIDIOC_REQBUFS, &req) < 0) {
                printf("setup_v4l_capture: VIDIOC_REQBUFS failed\n");
                return -1;
        }

        return 0;
}

static inline unsigned int unused_buffer(struct capture_data *shd)
{
	return (shd->size - 1) & (shd->size - (shd->in - shd->out));
}

static void put_one_buffer(struct capture_device *dev, 
					struct v4l2_buffer *buf)
{
	unsigned int l;
	int ret;

	l = unused_buffer(dev->shd);
	while (l == 0) {
		if (dev->is_first_loop) {
			dev->is_first_loop = false;
			break;
		}
		sem_lock(dev->shd->sem_id);
		l = unused_buffer(dev->shd);
		if (l == 0) {
			dev->shd->out++;
		} else {
			sem_unlock(dev->shd->sem_id);
		}

		break;
	}
	memcpy(dev->shb[dev->shd->in++], 
		dev->cap_bufs[buf->index].start, dev->shd->info->sizeimage);
}

static void do_handle_cap(int fd_v4l, struct capture_device *dev)
{
	struct v4l2_buffer buf;
	int ret;

	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_MMAP;
	for (;;) {
		ret = ioctl(fd_v4l, VIDIOC_DQBUF, &buf);
		if (ret < 0) {
			perror("VIDIOC_DQBUF error");
			return;
		}
	}
}

static int start_capturing(const int fd_v4l, struct capture_device *dev)
{
        struct v4l2_buffer buf;
        struct v4l2_format fmt;
	int ret = 0;
	int fd_flags = fcntl(fd, F_GETFL);

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

	ret = init_shm_with_fmt(dev, fmt);
	if (ret < 0) {
		printf("Failed to init shm.\n");
		return ret;
	}
	
	ret = start_streaming(fd_v4l, dev);
        if (ret < 0) {
                printf("start_streaming failed\n");
		free_capture_dev(dev);
		return ret;
        }

	dev->shd->ready = true;

	fcntl(fd_v4l, F_SETFL, fd_flags);
        while (1) {
		fd_set fds;

		FD_ZERO(&fds);
		FD_SET(fd_v4l, &fds);

		ret = select(fd + 1, &fds, NULL, NULL, NULL);
		if (ret == -1 || ret == 0) {
			perror("select error");
			sleep(1);
			continue;
		}

		ret = FD_ISSET(fd_v4l, &fds);
		if (ret)
			do_handle_cap(fd_v4l, dev);

                memset(&buf, 0, sizeof (buf));
                buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                buf.memory = V4L2_MEMORY_MMAP;
                if (ioctl (fd_v4l, VIDIOC_DQBUF, &buf) < 0)
                        printf("VIDIOC_DQBUF failed.\n");

                if (ioctl (fd_v4l, VIDIOC_QBUF, &buf) < 0) {
			printf("VIDIOC_QBUF failed\n");
                       	break;
		}
	}

        return ret;
}

static void free_sem(const int sem_id)
{
	union semun sem_union;
	int ret = 0;

	ret = semctl(sem_id, 0, IPC_RMID, sem_union);
	if (ret == -1)
		perror("IPC_RMID semctl error");
}

static int init_sem(struct capture_device *dev)
{
	key_t key;
	union semun sem_union;
	int ret;

	key = ftok(KEY_PATH, MODULE_SEM_ID);
	if (key) {
		perror("MODULE_SEM_ID error");
		return -1;
	}
	dev->shd->sem_id = semget(key, 1, 0666 | IPC_CREAT);

	sem_union.val = 1;
	ret = semctl(dev->shd->sem_id, 0, SETVAL, sem_union);
	if (ret == -1)
		perror("semctl error");

	return ret;
}

static int init_shm_with_fmt(struct capture_device *dev, 
			struct v4l2_format fmt)
{
	int ret;

	ret = init_shm(dev->shd_id, dev->shd, MODULE_SHD_ID, 
				sizeof(struct capture_data));
	if (ret < 0) {
		printf("Failed to init shd.\n");
		return ret;
	}
	dev->shd->info.width = fmt.fmt.pix.width;
	dev->shd->info.height = fmt.fmt.pix.height;
	dev->shd->info.fmt = fmt.fmt.pix.pixelformat;
	dev->shd->info.sizeimage = fmt.fmt.pix.sizeimage;
	dev->shd->ready = false;

	dev->shd->in = 0;
	dev->shd->out = 0;
	/* size must be the power of 2 */
	dev->shd->size = 8;

	ret = init_sem(dev);
	if (ret < 0) {
		printf("Failed to init sem.\n");
		goto err_sem;
	}
	 
	ret = init_shm(dev->shb_id, dev->shb, MODULE_SHB_ID,
			dev->shd->info->sizeimage * dev->config->shb_cnt);
	if (ret < 0) {
		printf("Failed to init shb.\n");
		goto err_shb;
	}

	return 0;

err_shb:
	free_sem(dev->shd->sem_id);
err_sem:
	ret = shmctl(dev->shd_id, IPC_RMID, 0);
	if (ret < 0)
		printf("shmctl of shd_id.\n");

	return ret;
}

static void free_capture_dev(struct capture_device *dev)
{
	free(dev->cap_bufs);
	free_sem(dev->shd->sem_id);
	free_shm(dev->shd_id);
	free_shm(dev->shb_id);
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
	dev->cap_bufs = (struct capture_buf *)malloc(dev->config->cap->buf_cnt
				* sizeof(struct capture_buf));
	if (!dev->cap_bufs) {
		printf("Failed to alloc mem.\n");
		ret = -1;
		goto err_mem;
	}

	ret = open(dev->config->device, O_RDWR, 0);
	if (ret < 0) {
		printf("Unable to open %s\n", dev->config->device);
		goto err_open;
	}

        ret = setup_v4l_capture(fd_v4l, dev->config);
	if (ret < 0)
		goto err_setup;

        ret = start_capturing(fd_v4l, dev);

	ret = stop_capturing(fd_v4l);
        if (ret < 0) {
                printf("stop_capturing failed\n");
        }

err_setup:
        close(fd_v4l);
err_open:
	free_capture_dev(dev);
err_mem:
	free(dev);
	
	return ret;
}
