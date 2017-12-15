/*
 * @file main.c
 *
 * Copyright 2017 zhujiongfu.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <unistd.h>
#include<sys/types.h>
#include "v4l2_capture.h"

static unsigned int get_rdy_buf_index(struct capture_data *shd)
{
	unsigned int out;

	do {
		sem_lock(shd->sem_id);
		printf("flag: 0x%0x\n", shd->buf_flag);
		switch (shd->buf_flag) {
		case 0x3:
		case 0x1:
			out = 1;
			break;
		case 0x2:
			out = 2;
			break;
		default:
			out = 0;
		}
		sem_unlock(shd->sem_id);
		if (out == 0)
			usleep(10000);
	} while (out == 0);

	return out;
}

int main(void)
{
	struct capture_data *shd;
	FILE *file;
	char *shb;
	int shd_id, shb_id;
	int semid;
	int out;
	int ret = 0;

	file = fopen("/tmp/stream.out", "wb");
	if (file == NULL) {
		printf("failed to create file.\n");
		return -1;
	}

	shd = (struct capture_data *)alloc_shm(&shd_id,
						MODULE_SHM_ID, 0, 0666);
	if ((void *)shd == (void *)-1) {
		printf("%s: failed to init shd.\n", __FILE__);
		ret = -1;
		goto err_shd;
	}
	shb = ((char *)shd) + sizeof(struct capture_data);

	printf("get shd size: %u\n", shd->sizeimage);

	semid = get_and_init_sem(KEY_PATH, MODULE_SEM_ID);
	if (semid < 0) {
		printf("%s: failed to get sem.\n", __FILE__);
		ret = -1;
		goto err_sem;
	}

	for (;;) {
		out = get_rdy_buf_index(shd);		
		out--;
		fwrite(shb + shd->sizeimage * out, shd->sizeimage, 1, file);
		printf("index: %u\n", out);
		/* sleep(1); */
		usleep(100000);
		sem_lock(shd->sem_id);
		shd->buf_flag &= ~(1 << out);
		sem_unlock(shd->sem_id);
	}

err_sem:
	free_shm(shd_id);
err_shd:
	fclose(file);

	return ret;
}
