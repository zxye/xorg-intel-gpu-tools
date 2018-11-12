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
	FILE *intra_image;

	bo = drm_intel_bo_alloc(data->bufmgr, "", INPUT_SIZE, 4096);

    /* @@TODO, read src surface from file "SourceFrameI.yu12".
       But even without it, we can still triger the rcs0 resetting
       with this vme kernel.*/

	memset(buf, 0, sizeof(*buf));

	buf->bo = bo;
	buf->stride = STRIDE;
	buf->tiling = I915_TILING_NONE;
	buf->size = INPUT_SIZE;


	intra_image = fopen("SourceFrameI.yu12", "rb");
	igt_assert(intra_image);
	igt_assert(fread(data->linear, 1, sizeof(data->linear), intra_image) == sizeof(data->linear));
	/*
	memset(data->linear,0,sizeof(data->linear));
	data->linear[0] = 0xc6;
	data->linear[1] = 0xb9;
	data->linear[2] = 0xab;
	data->linear[3] = 0xa4;
	*/
	for (int i=0; i<HEIGHT; i++)
	{
		for (int j=0; j<WIDTH; j++)
		{
			printf("%x ",data->linear[i*HEIGHT+j]);
		}
		printf("\n");
	}
	gem_write(data->drm_fd, buf->bo->handle, 0, data->linear, sizeof(data->linear)); 

	fclose(intra_image);
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

struct _drm_intel_context {
	unsigned int ctx_id;
	struct _drm_intel_bufmgr *bufmgr;
};
struct _drm_intel_bufmgr {
	/**
	 * Allocate a buffer object.
	 *
	 * Buffer objects are not necessarily initially mapped into CPU virtual
	 * address space or graphics device aperture.  They must be mapped
	 * using bo_map() or drm_intel_gem_bo_map_gtt() to be used by the CPU.
	 */
	drm_intel_bo *(*bo_alloc) (drm_intel_bufmgr *bufmgr, const char *name,
				   unsigned long size, unsigned int alignment);

	/**
	 * Allocate a buffer object, hinting that it will be used as a
	 * render target.
	 *
	 * This is otherwise the same as bo_alloc.
	 */
	drm_intel_bo *(*bo_alloc_for_render) (drm_intel_bufmgr *bufmgr,
					      const char *name,
					      unsigned long size,
					      unsigned int alignment);

	/**
	 * Allocate a buffer object from an existing user accessible
	 * address malloc'd with the provided size.
	 * Alignment is used when mapping to the gtt.
	 * Flags may be I915_VMAP_READ_ONLY or I915_USERPTR_UNSYNCHRONIZED
	 */
	drm_intel_bo *(*bo_alloc_userptr)(drm_intel_bufmgr *bufmgr,
					  const char *name, void *addr,
					  uint32_t tiling_mode, uint32_t stride,
					  unsigned long size,
					  unsigned long flags);

	/**
	 * Allocate a tiled buffer object.
	 *
	 * Alignment for tiled objects is set automatically; the 'flags'
	 * argument provides a hint about how the object will be used initially.
	 *
	 * Valid tiling formats are:
	 *  I915_TILING_NONE
	 *  I915_TILING_X
	 *  I915_TILING_Y
	 *
	 * Note the tiling format may be rejected; callers should check the
	 * 'tiling_mode' field on return, as well as the pitch value, which
	 * may have been rounded up to accommodate for tiling restrictions.
	 */
	drm_intel_bo *(*bo_alloc_tiled) (drm_intel_bufmgr *bufmgr,
					 const char *name,
					 int x, int y, int cpp,
					 uint32_t *tiling_mode,
					 unsigned long *pitch,
					 unsigned long flags);

	/** Takes a reference on a buffer object */
	void (*bo_reference) (drm_intel_bo *bo);

	/**
	 * Releases a reference on a buffer object, freeing the data if
	 * no references remain.
	 */
	void (*bo_unreference) (drm_intel_bo *bo);

	/**
	 * Maps the buffer into userspace.
	 *
	 * This function will block waiting for any existing execution on the
	 * buffer to complete, first.  The resulting mapping is available at
	 * buf->virtual.
	 */
	int (*bo_map) (drm_intel_bo *bo, int write_enable);

	/**
	 * Reduces the refcount on the userspace mapping of the buffer
	 * object.
	 */
	int (*bo_unmap) (drm_intel_bo *bo);

	/**
	 * Write data into an object.
	 *
	 * This is an optional function, if missing,
	 * drm_intel_bo will map/memcpy/unmap.
	 */
	int (*bo_subdata) (drm_intel_bo *bo, unsigned long offset,
			   unsigned long size, const void *data);

	/**
	 * Read data from an object
	 *
	 * This is an optional function, if missing,
	 * drm_intel_bo will map/memcpy/unmap.
	 */
	int (*bo_get_subdata) (drm_intel_bo *bo, unsigned long offset,
			       unsigned long size, void *data);

	/**
	 * Waits for rendering to an object by the GPU to have completed.
	 *
	 * This is not required for any access to the BO by bo_map,
	 * bo_subdata, etc.  It is merely a way for the driver to implement
	 * glFinish.
	 */
	void (*bo_wait_rendering) (drm_intel_bo *bo);

	/**
	 * Tears down the buffer manager instance.
	 */
	void (*destroy) (drm_intel_bufmgr *bufmgr);

	/**
	 * Indicate if the buffer can be placed anywhere in the full ppgtt
	 * address range (2^48).
	 *
	 * Any resource used with flat/heapless (0x00000000-0xfffff000)
	 * General State Heap (GSH) or Intructions State Heap (ISH) must
	 * be in a 32-bit range. 48-bit range will only be used when explicitly
	 * requested.
	 *
	 * \param bo Buffer to set the use_48b_address_range flag.
	 * \param enable The flag value.
	 */
	void (*bo_use_48b_address_range) (drm_intel_bo *bo, uint32_t enable);

	/**
	 * Add relocation entry in reloc_buf, which will be updated with the
	 * target buffer's real offset on on command submission.
	 *
	 * Relocations remain in place for the lifetime of the buffer object.
	 *
	 * \param bo Buffer to write the relocation into.
	 * \param offset Byte offset within reloc_bo of the pointer to
	 *			target_bo.
	 * \param target_bo Buffer whose offset should be written into the
	 *                  relocation entry.
	 * \param target_offset Constant value to be added to target_bo's
	 *			offset in relocation entry.
	 * \param read_domains GEM read domains which the buffer will be
	 *			read into by the command that this relocation
	 *			is part of.
	 * \param write_domains GEM read domains which the buffer will be
	 *			dirtied in by the command that this
	 *			relocation is part of.
	 */
	int (*bo_emit_reloc) (drm_intel_bo *bo, uint32_t offset,
			      drm_intel_bo *target_bo, uint32_t target_offset,
			      uint32_t read_domains, uint32_t write_domain);
	int (*bo_emit_reloc_fence)(drm_intel_bo *bo, uint32_t offset,
				   drm_intel_bo *target_bo,
				   uint32_t target_offset,
				   uint32_t read_domains,
				   uint32_t write_domain);

	/** Executes the command buffer pointed to by bo. */
	int (*bo_exec) (drm_intel_bo *bo, int used,
			drm_clip_rect_t *cliprects, int num_cliprects,
			int DR4);

	/** Executes the command buffer pointed to by bo on the selected
	 * ring buffer
	 */
	int (*bo_mrb_exec) (drm_intel_bo *bo, int used,
			    drm_clip_rect_t *cliprects, int num_cliprects,
			    int DR4, unsigned flags);

	/**
	 * Pin a buffer to the aperture and fix the offset until unpinned
	 *
	 * \param buf Buffer to pin
	 * \param alignment Required alignment for aperture, in bytes
	 */
	int (*bo_pin) (drm_intel_bo *bo, uint32_t alignment);

	/**
	 * Unpin a buffer from the aperture, allowing it to be removed
	 *
	 * \param buf Buffer to unpin
	 */
	int (*bo_unpin) (drm_intel_bo *bo);

	/**
	 * Ask that the buffer be placed in tiling mode
	 *
	 * \param buf Buffer to set tiling mode for
	 * \param tiling_mode desired, and returned tiling mode
	 */
	int (*bo_set_tiling) (drm_intel_bo *bo, uint32_t * tiling_mode,
			      uint32_t stride);

	/**
	 * Get the current tiling (and resulting swizzling) mode for the bo.
	 *
	 * \param buf Buffer to get tiling mode for
	 * \param tiling_mode returned tiling mode
	 * \param swizzle_mode returned swizzling mode
	 */
	int (*bo_get_tiling) (drm_intel_bo *bo, uint32_t * tiling_mode,
			      uint32_t * swizzle_mode);

	/**
	 * Set the offset at which this buffer will be softpinned
	 * \param bo Buffer to set the softpin offset for
	 * \param offset Softpin offset
	 */
	int (*bo_set_softpin_offset) (drm_intel_bo *bo, uint64_t offset);

	/**
	 * Create a visible name for a buffer which can be used by other apps
	 *
	 * \param buf Buffer to create a name for
	 * \param name Returned name
	 */
	int (*bo_flink) (drm_intel_bo *bo, uint32_t * name);

	/**
	 * Returns 1 if mapping the buffer for write could cause the process
	 * to block, due to the object being active in the GPU.
	 */
	int (*bo_busy) (drm_intel_bo *bo);

	/**
	 * Specify the volatility of the buffer.
	 * \param bo Buffer to create a name for
	 * \param madv The purgeable status
	 *
	 * Use I915_MADV_DONTNEED to mark the buffer as purgeable, and it will be
	 * reclaimed under memory pressure. If you subsequently require the buffer,
	 * then you must pass I915_MADV_WILLNEED to mark the buffer as required.
	 *
	 * Returns 1 if the buffer was retained, or 0 if it was discarded whilst
	 * marked as I915_MADV_DONTNEED.
	 */
	int (*bo_madvise) (drm_intel_bo *bo, int madv);

	int (*check_aperture_space) (drm_intel_bo ** bo_array, int count);

	/**
	 * Disable buffer reuse for buffers which will be shared in some way,
	 * as with scanout buffers. When the buffer reference count goes to
	 * zero, it will be freed and not placed in the reuse list.
	 *
	 * \param bo Buffer to disable reuse for
	 */
	int (*bo_disable_reuse) (drm_intel_bo *bo);

	/**
	 * Query whether a buffer is reusable.
	 *
	 * \param bo Buffer to query
	 */
	int (*bo_is_reusable) (drm_intel_bo *bo);

	/**
	 *
	 * Return the pipe associated with a crtc_id so that vblank
	 * synchronization can use the correct data in the request.
	 * This is only supported for KMS and gem at this point, when
	 * unsupported, this function returns -1 and leaves the decision
	 * of what to do in that case to the caller
	 *
	 * \param bufmgr the associated buffer manager
	 * \param crtc_id the crtc identifier
	 */
	int (*get_pipe_from_crtc_id) (drm_intel_bufmgr *bufmgr, int crtc_id);

	/** Returns true if target_bo is in the relocation tree rooted at bo. */
	int (*bo_references) (drm_intel_bo *bo, drm_intel_bo *target_bo);

	/**< Enables verbose debugging printouts */
	int debug;
};

struct drm_i915_gem_context_param_sseu {
	/*
	 * Engine class & instance to be configured or queried.
	 */
	__u16 class;
	__u16 instance;

	/*
	 * Unused for now. Must be cleared to zero.
	 */
	__u32 rsvd1;

	/*
	 * Mask of slices to enable for the context. Valid values are a subset
	 * of the bitmask value returned for I915_PARAM_SLICE_MASK.
	 */
	__u64 slice_mask;

	/*
	 * Mask of subslices to enable for the context. Valid values are a
	 * subset of the bitmask value return by I915_PARAM_SUBSLICE_MASK.
	 */
	__u64 subslice_mask;

	/*
	 * Minimum/Maximum number of EUs to enable per subslice for the
	 * context. min_eus_per_subslice must be inferior or equal to
	 * max_eus_per_subslice.
	 */
	__u16 min_eus_per_subslice;
	__u16 max_eus_per_subslice;

	/*
	 * Unused for now. Must be cleared to zero.
	 */
	__u32 rsvd2;
};

#define I915_CONTEXT_PARAM_SSEU		0x8

static int
drm_get_context_param_sseu(int fd, drm_intel_context *ctx,
                struct drm_i915_gem_context_param_sseu *sseu)
{
    struct drm_i915_gem_context_param context_param;
    int ret;

    memset(&context_param, 0, sizeof(context_param));
    context_param.ctx_id = ctx->ctx_id;
    context_param.param = I915_CONTEXT_PARAM_SSEU;
    context_param.value = (uint64_t) sseu;
    context_param.size = sizeof(struct drm_i915_gem_context_param_sseu);

    ret = drmIoctl(fd,
            DRM_IOCTL_I915_GEM_CONTEXT_GETPARAM,
            &context_param);

    return ret;
}

static int
drm_set_context_param_sseu(int fd, drm_intel_context *ctx,
                struct drm_i915_gem_context_param_sseu sseu)
{
    struct drm_i915_gem_context_param context_param;
    int ret;

    memset(&context_param, 0, sizeof(context_param));
    context_param.ctx_id = ctx->ctx_id;
    context_param.param = I915_CONTEXT_PARAM_SSEU;
    context_param.value = (uint64_t) &sseu;
    context_param.size = sizeof(struct drm_i915_gem_context_param_sseu);

    ret = drmIoctl(fd,
               DRM_IOCTL_I915_GEM_CONTEXT_SETPARAM,
               &context_param);

    return ret;
}

static uint32_t mos_hweight8(uint8_t w)
{
    uint32_t i, weight = 0;

    for (i=0; i<8; i++)
    {
        weight += !!((w) & (1UL << i));
    }
    return weight;
}

static uint8_t switch_off_n_bits(uint8_t in_mask, int n)
{
    int i,count;
    uint8_t bi,out_mask;

    assert (n>0 && n<=8);

    out_mask = in_mask;
    count = n;
    for(i=0; i<8; i++)
    {
        bi = 1UL<<i;
        if (bi & in_mask)
        {
            out_mask &= ~bi;
            count--;
        }
        if (count==0)
        {
            break;
        }
    }
    return out_mask;
}

static void shut_non_vme_subslices(int drm_fd, drm_intel_context *ctx)
{
    struct drm_i915_gem_context_param_sseu sseu;
    memset(&sseu,0, sizeof(sseu));
    sseu.class = 0;
    sseu.instance = 0;

    drm_get_context_param_sseu(drm_fd, ctx, &sseu);
    sseu.subslice_mask = switch_off_n_bits(sseu.subslice_mask, mos_hweight8(sseu.subslice_mask)/2); /* shutdown half subslices*/
    drm_set_context_param_sseu(drm_fd, ctx, sseu);
}

igt_simple_main
{
	data_t data = {0, };
	struct intel_batchbuffer *batch = NULL;
	struct igt_buf src, dst;
	igt_vme_func_t media_vme = NULL;
	uint8_t linear[OUTPUT_SIZE];

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

	batch->ctx = drm_intel_gem_context_create(data.bufmgr);
	igt_assert(batch->ctx);
    shut_non_vme_subslices(data.drm_fd, batch->ctx);

	media_vme(batch, &src, WIDTH, HEIGHT, &dst);
	gem_read(data.drm_fd, dst.bo->handle, 0,
		linear, sizeof(linear));
	for (int i=0; i< sizeof(linear); i++)
	{
		printf("%x ",linear[i]);
	}
}
