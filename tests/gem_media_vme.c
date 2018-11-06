/*
 * Copyright © 2013 Intel Corporation
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
 *    Damien Lespiau <damien.lespiau@intel.com>
 *    Xiang, Haihao <haihao.xiang@intel.com>
 */

/*
 * This file is a basic test for the media_fill() function, a very simple
 * workload for the Media pipeline.
 */

#include "igt.h"
#include <stdbool.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "drm.h"
#include "intel_bufmgr.h"

IGT_TEST_DESCRIPTION("Basic test for the media_fill() function, a very simple"
		     " workload for the Media pipeline.");

#define WIDTH 64
#define STRIDE (WIDTH)
#define HEIGHT 64
#define INPUT_SIZE (WIDTH*HEIGHT*sizeof(char)*1.5)

#define OUTPUT_SIZE	(56*sizeof(int))

typedef struct {
	int drm_fd;
	uint32_t devid;
	drm_intel_bufmgr *bufmgr;
	uint8_t linear[WIDTH * HEIGHT];
} data_t;

static void scratch_buf_init_src(data_t *data, struct igt_buf *buf)
{
	drm_intel_bo *bo;

	bo = drm_intel_bo_alloc(data->bufmgr, "", INPUT_SIZE, 4096);

    /* @@TODO, read src surface from file "SourceFrameI.yu12".
       But even without it, we can still triger the rcs0 resetting
       with this vme kernel.*/

	memset(buf, 0, sizeof(*buf));

	buf->bo = bo;
	buf->stride = STRIDE;
	buf->tiling = I915_TILING_NONE;
	buf->size = INPUT_SIZE;
}

static void scratch_buf_init_dst(data_t *data, struct igt_buf *buf)
{
	drm_intel_bo *bo;

	bo = drm_intel_bo_alloc(data->bufmgr, "", OUTPUT_SIZE, 4096);

	memset(buf, 0, sizeof(*buf));

	buf->bo = bo;
	buf->stride = 1;
	buf->tiling = I915_TILING_NONE;
	buf->size = OUTPUT_SIZE;
}

igt_simple_main
{
	data_t data = {0, };
	struct intel_batchbuffer *batch = NULL;
	struct igt_buf src, dst;
	igt_vme_func_t media_vme = NULL;

	data.drm_fd = drm_open_driver_render(DRIVER_INTEL);
	igt_require_gem(data.drm_fd);

	data.devid = intel_get_drm_devid(data.drm_fd);

	data.bufmgr = drm_intel_bufmgr_gem_init(data.drm_fd, 4096);
	igt_assert(data.bufmgr);

	media_vme = igt_get_media_vme_func(data.devid);

	igt_require_f(media_vme,
		"no media-vme function\n");

	batch = intel_batchbuffer_alloc(data.bufmgr, data.devid);
	igt_assert(batch);

	scratch_buf_init_src(&data, &src);
	scratch_buf_init_dst(&data, &dst);

	media_vme(batch, &src, WIDTH, HEIGHT, &dst);
}
