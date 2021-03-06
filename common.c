/*
 * replika, a set of tools for dealing with hashmapped disk images
 * Copyright (C) 2015 Lennert Buytenhek
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version
 * 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License version 2.1 for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License version 2.1 along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street - Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#define _FILE_OFFSET_BITS 64

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include "common.h"

#define MIN_THREADS	4
#define MAX_THREADS	16

static void xgettimeofday(struct timeval *tv, struct timezone *tz)
{
	if (gettimeofday(tv, tz) < 0) {
		perror("gettimeofday");
		exit(1);
	}
}

static void xpthread_create(pthread_t *thread, const pthread_attr_t *attr,
			    void *(*start_routine)(void *), void *arg)
{
	int ret;

	ret = pthread_create(thread, attr, start_routine, arg);
	if (ret) {
		fprintf(stderr, "pthread_create: %s\n", strerror(ret));
		exit(1);
	}
}

static void xpthread_join(pthread_t thread, void **retval)
{
	int ret;

	ret = pthread_join(thread, retval);
	if (ret) {
		fprintf(stderr, "pthread_join: %s\n", strerror(ret));
		exit(1);
	}
}

ssize_t xpread(int fd, void *buf, size_t count, off_t offset)
{
	off_t off;

	off = 0;
	while (off < count) {
		ssize_t ret;

		ret = pread(fd, buf + off, count - off, offset + off);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			perror("pread");
			exit(1);
		}

		if (ret == 0)
			break;

		off += ret;
	}

	return off;
}

ssize_t xpwrite(int fd, const void *buf, size_t count, off_t offset)
{
	off_t off;

	off = 0;
	while (off < count) {
		ssize_t ret;

		ret = pwrite(fd, buf + off, count - off, offset + off);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			perror("pwrite");
			exit(1);
		}

		off += ret;
	}

	return off;
}

static void xsem_init(sem_t *sem, int pshared, unsigned int value)
{
	if (sem_init(sem, pshared, value) < 0) {
		perror("sem_init");
		exit(1);
	}
}

void xsem_post(sem_t *sem)
{
	if (sem_post(sem) < 0) {
		perror("sem_post");
		exit(1);
	}
}

void xsem_wait(sem_t *sem)
{
	if (sem_wait(sem) < 0) {
		perror("sem_wait");
		exit(1);
	}
}

static void xsem_destroy(sem_t *sem)
{
	if (sem_destroy(sem) < 0) {
		perror("sem_destroy");
		exit(1);
	}
}

static int num_threads(void)
{
	int ncpus;

	ncpus = sysconf(_SC_NPROCESSORS_ONLN);
	if (ncpus < MIN_THREADS)
		return MIN_THREADS;
	if (ncpus > MAX_THREADS)
		return MAX_THREADS;

	return ncpus;
}

void run_threads(void *(*handler)(void *))
{
	struct worker_thread wt[MAX_THREADS];
	pthread_t tid[MAX_THREADS];
	int nthreads;
	int i;

	nthreads = num_threads();

	for (i = 0; i < nthreads; i++) {
		xsem_init(&wt[i].sem0, 0, 0);
		xsem_init(&wt[i].sem1, 0, 0);
		wt[i].next = &wt[(i + 1) % nthreads];
	}

	for (i = 0; i < nthreads; i++)
		xpthread_create(tid + i, NULL, handler, wt + i);

	xsem_post(&wt[0].sem0);
	xsem_post(&wt[0].sem1);

	for (i = 0; i < nthreads; i++)
		xpthread_join(tid[i], NULL);

	for (i = 0; i < nthreads; i++) {
		xsem_destroy(&wt[i].sem0);
		xsem_destroy(&wt[i].sem1);
	}
}

static int stderr_is_tty = -1;
static struct timeval lasttime;

int should_report_progress(void)
{
	struct timeval curtime;
	long long diff;

	if (stderr_is_tty == -1) {
		if (isatty(2)) {
			stderr_is_tty = 1;
		} else if (errno == EINVAL || errno == ENOTTY) {
			stderr_is_tty = 0;
		} else {
			perror("isatty");
			exit(1);
		}
	}

	if (!stderr_is_tty)
		return 0;

	xgettimeofday(&curtime, NULL);

	diff = (curtime.tv_sec - lasttime.tv_sec) * 1000000LL;
	diff += curtime.tv_usec - lasttime.tv_usec;

	if (diff >= 1000000) {
		lasttime = curtime;
		return 1;
	}

	return 0;
}

void progress_reported(void)
{
	if (stderr_is_tty)
		xgettimeofday(&lasttime, NULL);
}
