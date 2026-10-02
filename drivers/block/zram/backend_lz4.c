// SPDX-License-Identifier: GPL-2.0-or-later

#define pr_fmt(fmt) "lz4: " fmt

#include <linux/kernel.h>
#include <linux/lz4.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include "backend_lz4.h"

struct lz4_ctx {
	/* working memory for LZ4_compress_fast() */
	void *mem;
};

static void lz4_release_params(struct zcomp_params *params)
{
	/*
	 * Dictionaries are not supported here, see lz4_setup_params().
	 */
	params->drv_data = NULL;
}

static int lz4_setup_params(struct zcomp_params *params)
{
	if (params->level == ZCOMP_PARAM_NOT_SET) {
		params->level = LZ4_ACCELERATION_DEFAULT;
	} else if (params->level < LZ4_ACCELERATION_DEFAULT) {
		pr_err("invalid compression level %d\n", params->level);
		return -EINVAL;
	}

	/*
	 * Dictionaries require the LZ4 streaming/prefix API, which is not
	 * used here. zram never sets a dictionary, so silently ignore it.
	 */
	return 0;
}

static void lz4_destroy(struct zcomp_ctx *ctx)
{
	struct lz4_ctx *zctx = ctx->context;

	if (!zctx)
		return;

	vfree(zctx->mem);
	kfree(zctx);
	ctx->context = NULL;
}

static int lz4_create(struct zcomp_params *params, struct zcomp_ctx *ctx)
{
	struct lz4_ctx *zctx;

	zctx = kzalloc_obj(*zctx);
	if (!zctx)
		return -ENOMEM;

	ctx->context = zctx;

	zctx->mem = vmalloc(LZ4_MEM_COMPRESS);
	if (!zctx->mem)
		goto error;

	return 0;

error:
	lz4_destroy(ctx);
	return -ENOMEM;
}

static int lz4_compress(struct zcomp_params *params, struct zcomp_ctx *ctx,
			struct zcomp_req *req)
{
	struct lz4_ctx *zctx = ctx->context;
	int ret;

	ret = LZ4_compress_fast(req->src, req->dst, req->src_len,
				req->dst_len, params->level, zctx->mem);
	if (!ret)
		return -EINVAL;

	req->dst_len = ret;
	return 0;
}

static int lz4_decompress(struct zcomp_params *params, struct zcomp_ctx *ctx,
			  struct zcomp_req *req)
{
	int ret;

	ret = LZ4_decompress_safe(req->src, req->dst, req->src_len,
				  req->dst_len);
	if (ret < 0)
		return -EINVAL;

	return 0;
}

const struct zcomp_ops backend_lz4 = {
	.compress	= lz4_compress,
	.decompress	= lz4_decompress,
	.create_ctx	= lz4_create,
	.destroy_ctx	= lz4_destroy,
	.setup_params	= lz4_setup_params,
	.release_params	= lz4_release_params,
	.name		= "lz4",
};
