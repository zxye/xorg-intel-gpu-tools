/*
 * Copyright © 2011 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Chris Wilson <chris@chris-wilson.co.uk>
 *
 */

#include "igt.h"
#include "igt_rand.h"
#include "igt_sysfs.h"

#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <time.h>
#include "drm.h"

#define LOCAL_I915_EXEC_NO_RELOC (1<<11)
#define LOCAL_I915_EXEC_HANDLE_LUT (1<<12)

#define LOCAL_I915_EXEC_BSD_SHIFT      (13)
#define LOCAL_I915_EXEC_BSD_MASK       (3 << LOCAL_I915_EXEC_BSD_SHIFT)

#define ENGINE_FLAGS  (I915_EXEC_RING_MASK | LOCAL_I915_EXEC_BSD_MASK)

#define FORKED 1
#define CHAINED 2
#define CONTEXT 4

static double elapsed(const struct timespec *start, const struct timespec *end)
{
	return ((end->tv_sec - start->tv_sec) +
		(end->tv_nsec - start->tv_nsec)*1e-9);
}

static double nop_on_ring(int fd, uint32_t handle, unsigned ring_id,
			  int timeout, unsigned long *out)
{
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 obj;
	struct timespec start, now;
	unsigned long count;

	memset(&obj, 0, sizeof(obj));
	obj.handle = handle;

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = to_user_pointer(&obj);
	execbuf.buffer_count = 1;
	execbuf.flags = ring_id;
	execbuf.flags |= LOCAL_I915_EXEC_HANDLE_LUT;
	execbuf.flags |= LOCAL_I915_EXEC_NO_RELOC;
	if (__gem_execbuf(fd, &execbuf)) {
		execbuf.flags = ring_id;
		gem_execbuf(fd, &execbuf);
	}
	intel_detect_and_clear_missed_interrupts(fd);

	count = 0;
	clock_gettime(CLOCK_MONOTONIC, &start);
	do {
		for (int loop = 0; loop < 1024; loop++)
			gem_execbuf(fd, &execbuf);

		count += 1024;
		clock_gettime(CLOCK_MONOTONIC, &now);
	} while (elapsed(&start, &now) < timeout);
	igt_assert_eq(intel_detect_and_clear_missed_interrupts(fd), 0);

	*out = count;
	return elapsed(&start, &now);
}

static void single(int fd, uint32_t handle,
		   unsigned ring_id, const char *ring_name)
{
	double time;
	unsigned long count;

	gem_require_ring(fd, ring_id);

	time = nop_on_ring(fd, handle, ring_id, 20, &count);
	igt_info("%s: %'lu cycles: %.3fus\n",
		 ring_name, count, time*1e6 / count);
}

static bool ignore_engine(int fd, unsigned engine)
{
	if (engine == 0)
		return true;

	if (gem_has_bsd2(fd) && engine == I915_EXEC_BSD)
		return true;

	return false;
}

static void parallel(int fd, uint32_t handle, int timeout)
{
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 obj;
	unsigned engines[16];
	const char *names[16];
	unsigned nengine;
	unsigned engine;
	unsigned long count;
	double time, sum;

	sum = 0;
	nengine = 0;
	for_each_engine(fd, engine) {
		if (ignore_engine(fd, engine))
			continue;

		engines[nengine] = engine;
		names[nengine] = e__->name;
		nengine++;

		time = nop_on_ring(fd, handle, engine, 1, &count) / count;
		sum += time;
		igt_debug("%s: %.3fus\n", e__->name, 1e6*time);
	}
	igt_require(nengine);
	igt_info("average (individually): %.3fus\n", sum/nengine*1e6);

	memset(&obj, 0, sizeof(obj));
	obj.handle = handle;

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = to_user_pointer(&obj);
	execbuf.buffer_count = 1;
	execbuf.flags |= LOCAL_I915_EXEC_HANDLE_LUT;
	execbuf.flags |= LOCAL_I915_EXEC_NO_RELOC;
	if (__gem_execbuf(fd, &execbuf)) {
		execbuf.flags = 0;
		gem_execbuf(fd, &execbuf);
	}
	intel_detect_and_clear_missed_interrupts(fd);

	igt_fork(child, nengine) {
		struct timespec start, now;

		execbuf.flags &= ~ENGINE_FLAGS;
		execbuf.flags |= engines[child];

		count = 0;
		clock_gettime(CLOCK_MONOTONIC, &start);
		do {
			for (int loop = 0; loop < 1024; loop++)
				gem_execbuf(fd, &execbuf);
			count += 1024;
			clock_gettime(CLOCK_MONOTONIC, &now);
		} while (elapsed(&start, &now) < timeout);
		time = elapsed(&start, &now) / count;
		igt_info("%s: %ld cycles, %.3fus\n", names[child], count, 1e6*time);
	}

	igt_waitchildren();
	igt_assert_eq(intel_detect_and_clear_missed_interrupts(fd), 0);

}

static void series(int fd, uint32_t handle, int timeout)
{
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 obj;
	struct timespec start, now, sync;
	unsigned engines[16];
	unsigned nengine;
	unsigned engine;
	unsigned long count;
	double time, max = 0, min = HUGE_VAL, sum = 0;
	const char *name;

	nengine = 0;
	for_each_engine(fd, engine) {
		if (ignore_engine(fd, engine))
			continue;

		time = nop_on_ring(fd, handle, engine, 1, &count) / count;
		if (time > max) {
			name = e__->name;
			max = time;
		}
		if (time < min)
			min = time;
		sum += time;
		engines[nengine++] = engine;
	}
	igt_require(nengine);
	igt_info("Maximum execution latency on %s, %.3fus, min %.3fus, total %.3fus per cycle, average %.3fus\n",
		 name, max*1e6, min*1e6, sum*1e6, sum/nengine*1e6);

	memset(&obj, 0, sizeof(obj));
	obj.handle = handle;

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = to_user_pointer(&obj);
	execbuf.buffer_count = 1;
	execbuf.flags |= LOCAL_I915_EXEC_HANDLE_LUT;
	execbuf.flags |= LOCAL_I915_EXEC_NO_RELOC;
	if (__gem_execbuf(fd, &execbuf)) {
		execbuf.flags = 0;
		gem_execbuf(fd, &execbuf);
	}
	intel_detect_and_clear_missed_interrupts(fd);

	count = 0;
	clock_gettime(CLOCK_MONOTONIC, &start);
	do {
		for (int loop = 0; loop < 1024; loop++) {
			for (int n = 0; n < nengine; n++) {
				execbuf.flags &= ~ENGINE_FLAGS;
				execbuf.flags |= engines[n];
				gem_execbuf(fd, &execbuf);
			}
		}
		count += nengine * 1024;
		clock_gettime(CLOCK_MONOTONIC, &now);
	} while (elapsed(&start, &now) < timeout); /* Hang detection ~120s */
	gem_sync(fd, handle);
	clock_gettime(CLOCK_MONOTONIC, &sync);
	igt_debug("sync time: %.3fus\n", elapsed(&now, &sync)*1e6);
	igt_assert_eq(intel_detect_and_clear_missed_interrupts(fd), 0);

	time = elapsed(&start, &now) / count;
	igt_info("All (%d engines): %'lu cycles, average %.3fus per cycle [expected %.3fus]\n",
		 nengine, count, 1e6*time, 1e6*((max-min)/nengine+min));

	/* The rate limiting step should be how fast the slowest engine can
	 * execute its queue of requests, as when we wait upon a full ring all
	 * dispatch is frozen. So in general we cannot go faster than the
	 * slowest engine (but as all engines are in lockstep, they should all
	 * be executing in parallel and so the average should be max/nengines),
	 * but we should equally not go any slower.
	 *
	 * However, that depends upon being able to submit fast enough, and
	 * that in turns depends upon debugging turned off and no bottlenecks
	 * within the driver. We cannot assert that we hit ideal conditions
	 * across all engines, so we only look for an outrageous error
	 * condition.
	 */
	igt_assert_f(time < 2*sum,
		     "Average time (%.3fus) exceeds expectation for parallel execution (min %.3fus, max %.3fus; limit set at %.3fus)\n",
		     1e6*time, 1e6*min, 1e6*max, 1e6*sum*2);
}

static void xchg(void *array, unsigned i, unsigned j)
{
	unsigned *u = array;
	unsigned tmp = u[i];
	u[i] = u[j];
	u[j] = tmp;
}

static int __gem_context_create(int fd, uint32_t *ctx_id)
{
	struct drm_i915_gem_context_create arg;
	int ret = 0;

	memset(&arg, 0, sizeof(arg));
	if (drmIoctl(fd, DRM_IOCTL_I915_GEM_CONTEXT_CREATE, &arg))
		ret = -errno;

	*ctx_id = arg.ctx_id;
	return ret;
}
static void sequential(int fd, uint32_t handle, unsigned flags, int timeout)
{
	const int ncpus = flags & FORKED ? sysconf(_SC_NPROCESSORS_ONLN) : 1;
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 obj[2];
	unsigned engines[16];
	unsigned nengine;
	double *results;
	double time, sum;
	unsigned n;

	results = mmap(NULL, 4096, PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);
	igt_assert(results != MAP_FAILED);

	nengine = 0;
	sum = 0;
	for_each_engine(fd, n) {
		unsigned long count;

		if (ignore_engine(fd, n))
			continue;

		time = nop_on_ring(fd, handle, n, 1, &count) / count;
		sum += time;
		igt_debug("%s: %.3fus\n", e__->name, 1e6*time);

		engines[nengine++] = n;
	}
	igt_require(nengine);
	igt_info("Total (individual) execution latency %.3fus per cycle\n",
		 1e6*sum);

	memset(obj, 0, sizeof(obj));
	obj[0].handle = gem_create(fd, 4096);
	obj[0].flags = EXEC_OBJECT_WRITE;
	obj[1].handle = handle;

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = to_user_pointer(obj);
	execbuf.buffer_count = 2;
	execbuf.flags |= LOCAL_I915_EXEC_HANDLE_LUT;
	execbuf.flags |= LOCAL_I915_EXEC_NO_RELOC;
	igt_require(__gem_execbuf(fd, &execbuf) == 0);

	if (flags & CONTEXT) {
		uint32_t id;

		igt_require(__gem_context_create(fd, &id) == 0);
		execbuf.rsvd1 = id;
	}

	for (n = 0; n < nengine; n++) {
		execbuf.flags &= ~ENGINE_FLAGS;
		execbuf.flags |= engines[n];
		igt_require(__gem_execbuf(fd, &execbuf) == 0);
	}

	intel_detect_and_clear_missed_interrupts(fd);

	igt_fork(child, ncpus) {
		struct timespec start, now;
		unsigned long count;

		obj[0].handle = gem_create(fd, 4096);
		gem_execbuf(fd, &execbuf);

		if (flags & CONTEXT)
			execbuf.rsvd1 = gem_context_create(fd);

		hars_petruska_f54_1_random_perturb(child);

		count = 0;
		clock_gettime(CLOCK_MONOTONIC, &start);
		do {
			igt_permute_array(engines, nengine, xchg);
			if (flags & CHAINED) {
				for (n = 0; n < nengine; n++) {
					execbuf.flags &= ~ENGINE_FLAGS;
					execbuf.flags |= engines[n];
					for (int loop = 0; loop < 1024; loop++)
						gem_execbuf(fd, &execbuf);
				}
			} else {
				for (int loop = 0; loop < 1024; loop++) {
					for (n = 0; n < nengine; n++) {
						execbuf.flags &= ~ENGINE_FLAGS;
						execbuf.flags |= engines[n];
						gem_execbuf(fd, &execbuf);
					}
				}
			}
			count += 1024;
			clock_gettime(CLOCK_MONOTONIC, &now);
		} while (elapsed(&start, &now) < timeout); /* Hang detection ~120s */
		results[child] = elapsed(&start, &now) / count;

		if (flags & CONTEXT)
			gem_context_destroy(fd, execbuf.rsvd1);

		gem_close(fd, obj[0].handle);
	}
	igt_waitchildren();
	igt_assert_eq(intel_detect_and_clear_missed_interrupts(fd), 0);

	results[ncpus] = 0;
	for (n = 0; n < ncpus; n++)
		results[ncpus] += results[n];
	results[ncpus] /= ncpus;

	igt_info("Sequential (%d engines, %d processes): average %.3fus per cycle [expected %.3fus]\n",
		 nengine, ncpus, 1e6*results[ncpus], 1e6*sum*ncpus);

	if (flags & CONTEXT)
		gem_context_destroy(fd, execbuf.rsvd1);

	gem_close(fd, obj[0].handle);
	munmap(results, 4096);
}

static void print_welcome(int fd)
{
	bool active;
	int dir;

	dir = igt_sysfs_open_parameters(fd);
	if (dir < 0)
		return;

	active = igt_sysfs_get_boolean(dir, "enable_guc_submission");
	if (active) {
		igt_info("Using GuC submission\n");
		goto out;
	}

	active = igt_sysfs_get_boolean(dir, "enable_execlists");
	if (active) {
		igt_info("Using Execlists submission\n");
		goto out;
	}

	active = igt_sysfs_get_boolean(dir, "semaphores");
	igt_info("Using Legacy submission%s\n",
		 active ? ", with semaphores" : "");

out:
	close(dir);
}

igt_main
{
	const struct intel_execution_engine *e;
	uint32_t handle = 0;
	int device = -1;

	igt_fixture {
		const uint32_t bbe = MI_BATCH_BUFFER_END;

		device = drm_open_driver(DRIVER_INTEL);
		print_welcome(device);

		handle = gem_create(device, 4096);
		gem_write(device, handle, 0, &bbe, sizeof(bbe));

		igt_fork_hang_detector(device);
	}

	igt_subtest("basic-series")
		series(device, handle, 5);

	igt_subtest("basic-parallel")
		parallel(device, handle, 5);

	igt_subtest("basic-sequential")
		sequential(device, handle, 0, 5);

	for (e = intel_execution_engines; e->name; e++)
		igt_subtest_f("%s", e->name)
			single(device, handle, e->exec_id | e->flags, e->name);

	igt_subtest("series")
		series(device, handle, 150);

	igt_subtest("parallel")
		parallel(device, handle, 150);

	igt_subtest("sequential")
		sequential(device, handle, 0, 150);

	igt_subtest("forked-sequential")
		sequential(device, handle, FORKED, 150);

	igt_subtest("chained-sequential")
		sequential(device, handle, FORKED | CHAINED, 150);

	igt_subtest("context-sequential")
		sequential(device, handle, FORKED | CONTEXT, 150);

	igt_fixture {
		igt_stop_hang_detector();
		gem_close(device, handle);
		close(device);
	}
}
