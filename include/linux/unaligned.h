/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_UNALIGNED_H
#define __LINUX_UNALIGNED_H

/*
 * <linux/unaligned.h> was only introduced in Linux 6.12; before that the
 * helpers lived in <asm/unaligned.h>, which on arm64 resolves to
 * asm-generic/unaligned.h.  Newer imported code (lib/zstd 1.5.x, lib/lz4
 * 1.10) includes the generic name, so provide it here instead of patching
 * every upstream file.
 *
 * asm-generic/unaligned.h pulls in linux/unaligned/{access_ok,le_struct,
 * be_byteshift,generic}.h, which between them provide get_unaligned(),
 * put_unaligned() and the explicit get_unaligned_le16/32/64(),
 * get_unaligned_be16/32/64() and put_unaligned_*() variants.
 */
#include <asm/unaligned.h>

#endif /* __LINUX_UNALIGNED_H */
