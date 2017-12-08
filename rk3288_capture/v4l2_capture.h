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


struct capture_data {	
	unsigned int in;
	unsigned int out;
	unsigned int size;
	int sem_id;
	bool ready;
	struct {
		int width;
		int heitht;
		unsigned int fmt;
		unsigned int sizeimage;
	} info;
};

#endif
