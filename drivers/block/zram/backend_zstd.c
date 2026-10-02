// SPDX-License-Identifier: GPL-2.0-or-later

#define pr_fmt(fmt) "zstd: " fmt

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/zstd.h>

#include "backend_zstd.h"

/*
 * Fallback bounds for ZSTD compression levels. 5.4's zstd.h provides
 * ZSTD_maxCLevel() but no ZSTD_minCLevel(); levels start at 1.
 */
#ifndef ZSTD_MIN_CLEVEL
#define ZSTD_MIN_CLEVEL		1
#endif
#ifndef ZSTD_DEFAULT_CLEVEL
#define ZSTD_DEFAULT_CLEVEL	3
#endif

struct zstd_ctx {
	ZSTD_CCtx *cctx;
	ZSTD_DCtx *dctx;

	/*
	 * 5.4 has no ZSTD_createCCtx()/ZSTD_createDCtx(), contexts must be
	 * initialized ("embedded") into caller provided, physically
	 * contiguous workspaces, see ZSTD_initCCtx()/ZSTD_initDCtx().
	 */
	void *cctx_mem;
	void *dctx_mem;
};

static void zstd_release_params(struct zcomp_params *params)
{
	/*
	 * Dictionaries are not supported here, see zstd_setup_params().
	 */
	params->drv_data = NULL;
}

static int zstd_setup_params(struct zcomp_params *params)
{
	if (params->level == ZCOMP_PARAM_NOT_SET) {
		params->level = ZSTD_DEFAULT_CLEVEL;
	} else if (params->level < ZSTD_MIN_CLEVEL ||
		   params->level > ZSTD_maxCLevel()) {
		pr_err("invalid compression level %d\n", params->level);
		return -EINVAL;
	}

	/*
	 * Dictionaries require the zstd custom memory / CDict / DDict API,
	 * which is not used here. zram never sets a dictionary, so silently
	 * ignore it.
	 */
	return 0;
}

static void zstd_destroy(struct zcomp_ctx *ctx)
{
	struct zstd_ctx *zctx = ctx->context;

	if (!zctx)
		return;

	/*
	 * Both contexts are embedded into their workspaces, so dropping the
	 * workspaces is enough, no explicit zstd context release is needed.
	 */
	vfree(zctx->cctx_mem);
	vfree(zctx->dctx_mem);
	kfree(zctx);
	ctx->context = NULL;
}

static int zstd_create(struct zcomp_params *params, struct zcomp_ctx *ctx)
{
	struct zstd_ctx *zctx;
	ZSTD_compressionParameters cparams;
	size_t sz;

	zctx = kzalloc_obj(*zctx);
	if (!zctx)
		return -ENOMEM;

	ctx->context = zctx;

	cparams = ZSTD_getCParams(params->level, PAGE_SIZE, 0);

	sz = ZSTD_CCtxWorkspaceBound(cparams);
	zctx->cctx_mem = vzalloc(sz);
	if (!zctx->cctx_mem)
		goto error;

	zctx->cctx = ZSTD_initCCtx(zctx->cctx_mem, sz);
	if (!zctx->cctx)
		goto error;

	sz = ZSTD_DCtxWorkspaceBound();
	zctx->dctx_mem = vzalloc(sz);
	if (!zctx->dctx_mem)
		goto error;

	zctx->dctx = ZSTD_initDCtx(zctx->dctx_mem, sz);
	if (!zctx->dctx)
		goto error;

	return 0;

error:
	zstd_destroy(ctx);
	return -ENOMEM;
}

static int zstd_compress(struct zcomp_params *params, struct zcomp_ctx *ctx,
			 struct zcomp_req *req)
{
	struct zstd_ctx *zctx = ctx->context;
	ZSTD_parameters prm;
	size_t ret;

	prm = ZSTD_getParams(params->level, PAGE_SIZE, 0);

	ret = ZSTD_compressCCtx(zctx->cctx, req->dst, req->dst_len,
				req->src, req->src_len, prm);
	if (ZSTD_isError(ret))
		return -EINVAL;

	req->dst_len = ret;
	return 0;
}

static int zstd_decompress(struct zcomp_params *params, struct zcomp_ctx *ctx,
			   struct zcomp_req *req)
{
	struct zstd_ctx *zctx = ctx->context;
	size_t ret;

	ret = ZSTD_decompressDCtx(zctx->dctx, req->dst, req->dst_len,
				  req->src, req->src_len);
	if (ZSTD_isError(ret))
		return -EINVAL;

	return 0;
}

const struct zcomp_ops backend_zstd = {
	.compress	= zstd_compress,
	.decompress	= zstd_decompress,
	.create_ctx	= zstd_create,
	.destroy_ctx	= zstd_destroy,
	.setup_params	= zstd_setup_params,
	.release_params	= zstd_release_params,
	.name		= "zstd",
};
