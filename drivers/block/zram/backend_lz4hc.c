// SPDX-License-Identifier: GPL-2.0-or-later

#define pr_fmt(fmt) "lz4hc: " fmt

#include <linux/kernel.h>
#include <linux/lz4.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include "backend_lz4hc.h"

struct lz4hc_ctx {
	/* working memory for LZ4_compress_HC() */
	void *mem;
};

static void lz4hc_release_params(struct zcomp_params *params)
{
	/*
	 * Dictionaries are not supported here, see lz4hc_setup_params().
	 */
	params->drv_data = NULL;
}

static int lz4hc_setup_params(struct zcomp_params *params)
{
	if (params->level == ZCOMP_PARAM_NOT_SET) {
		params->level = LZ4HC_DEFAULT_CLEVEL;
	} else if (params->level < 1 || params->level > LZ4HC_MAX_CLEVEL) {
		/*
		 * Use < 1 rather than < LZ4HC_MIN_CLEVEL here because
		 * LZ4HC_compress_generic() only clamps levels below 1
		 * (levels 1 and 2 are valid). LZ4HC_MIN_CLEVEL (3) is
		 * advisory and not enforced by the library.
		 */
		pr_err("invalid compression level %d\n", params->level);
		return -EINVAL;
	}

	/*
	 * Dictionaries require the LZ4HC streaming/prefix API, which is not
	 * used here. zram never sets a dictionary, so silently ignore it.
	 */
	return 0;
}

static void lz4hc_destroy(struct zcomp_ctx *ctx)
{
	struct lz4hc_ctx *zctx = ctx->context;

	if (!zctx)
		return;

	vfree(zctx->mem);
	kfree(zctx);
	ctx->context = NULL;
}

static int lz4hc_create(struct zcomp_params *params, struct zcomp_ctx *ctx)
{
	struct lz4hc_ctx *zctx;

	zctx = kzalloc_obj(*zctx);
	if (!zctx)
		return -ENOMEM;

	ctx->context = zctx;

	zctx->mem = vmalloc(LZ4HC_MEM_COMPRESS);
	if (!zctx->mem)
		goto error;

	return 0;

error:
	lz4hc_destroy(ctx);
	return -ENOMEM;
}

static int lz4hc_compress(struct zcomp_params *params, struct zcomp_ctx *ctx,
			  struct zcomp_req *req)
{
	struct lz4hc_ctx *zctx = ctx->context;
	int ret;

	ret = LZ4_compress_HC(req->src, req->dst, req->src_len, req->dst_len,
			      params->level, zctx->mem);
	if (!ret)
		return -EINVAL;

	req->dst_len = ret;
	return 0;
}

static int lz4hc_decompress(struct zcomp_params *params, struct zcomp_ctx *ctx,
			    struct zcomp_req *req)
{
	int ret;

	ret = LZ4_decompress_safe(req->src, req->dst, req->src_len,
				  req->dst_len);
	if (ret < 0)
		return -EINVAL;

	return 0;
}

const struct zcomp_ops backend_lz4hc = {
	.compress	= lz4hc_compress,
	.decompress	= lz4hc_decompress,
	.create_ctx	= lz4hc_create,
	.destroy_ctx	= lz4hc_destroy,
	.setup_params	= lz4hc_setup_params,
	.release_params	= lz4hc_release_params,
	.name		= "lz4hc",
};
