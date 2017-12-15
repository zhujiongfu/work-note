/*
 * v4l2 buffer definitions.
 *
 * Copyright (C) 2017 zhujiongfu
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 */

#ifndef __V4L2_CAPTURE_H
#define __V4L2_CAPTURE_H

#include <errno.h>
#include <stdio.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>

#define KEY_PATH	"/tmp"
#define MODULE_SEM_ID	0x391
#define MODULE_SHM_ID	0x123

#define free_sem(id) \
({ \
	union semun __sem_union; \
	int __ret; \
	__ret = semctl(id, 0, IPC_RMID, __sem_union); \
	if (__ret == -1) \
		perror("IPC_RMID semctl error"); \
 })

#define free_shm(id) \
({ \
	int __ret; \
	__ret = shmctl(id, IPC_RMID, 0); \
	if (__ret < 0) \
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
	} while (1); \
 })

#define sem_lock(sem_id) sem_op(sem_id, -1)
#define sem_unlock(sem_id) sem_op(sem_id, 1)

union semun {
	int			val;
	struct semid_ds		*buf;
	unsigned short		*array;
};

struct capture_data {	
	unsigned int		in;
	unsigned int		last_in;
	unsigned int		out;
	unsigned int		mask;
	unsigned int		buf_flag;
	unsigned int		buf_cnt;
	int			sem_id;
	int			width;
	int			height;
	unsigned int		fmt;
	unsigned int		sizeimage;
};

static int get_and_init_sem(const char *path, int id)
{
	key_t key;
	union semun sem_union;
	int tmp;
	int semid;

	key = ftok(path, id);
	if (key == -1) {
		perror("MODULE_SEM_ID error");
		return -1;
	}
	semid = semget(key, 1, 0666 | IPC_CREAT);
	if (!semid) {
		perror("semget error");
		return -1;
	}

	sem_union.val = 1;
	tmp = semctl(semid, 0, SETVAL, sem_union);
	if (tmp == -1) {
		semid = tmp;
		perror("semctl error");
	}

	return semid;
}

static void *alloc_shm(int *shmid, int id, size_t size, int flag)
{
	key_t key;
	void *shm;
	int tmp;

	key = ftok(KEY_PATH, id);
	if (key == (key_t)-1) {
		printf("ftok of 0x%0x id error.\n", id);
		return (void *)-1;
	}
	printf("size: %d\n", size);
	tmp = shmget(key, size, flag);
	if (tmp < 0) {
		printf("shmget of 0x%0x id error.\n", id);
		return (void *)-1;
	}
	printf("shmdid: %d\n", tmp);
	shm = shmat(tmp, NULL, 0);
	if (shm == (void *)-1) {
		printf("shmat of 0x%0x id error.\n", id);
		return (void *)-1;
	}

	*shmid = tmp;

	return shm;
}

static inline unsigned int find_first_bit(unsigned int word)
{
	int num = 0;

	if ((word & 0xffff) == 0) {
		word >>= 16;
		num += 16;
	}
	if ((word & 0xff) == 0) {
		word >>= 8;
		num += 8;
	}
	if ((word & 0xf) == 0) {
		word >>= 4;
		num += 4;
	}
	if ((word & 0x3) == 0) {
		word >>= 2;
		num +=2;
	}
	if ((word & 0x1) == 0)
		num += 1;
	return num;
}

#endif
