// SPDX-License-Identifier: GPL-2.0
/*
 * Compatibility shims for loading the prebuilt vendor kernel modules.
 *
 * The stock vendor kernel was built with Clang CFI and without per-task stack
 * canaries, so every module in vendor_dlkm imports
 *
 *   __cfi_slowpath                       (CFI runtime helper)
 *   __ubsan_handle_cfi_check_fail_abort  (CFI failure handler)
 *   __stack_chk_guard                    (global stack canary)
 *
 * Without these three symbols none of the 138 vendor modules can be loaded.
 * When this kernel is itself built with CONFIG_CFI_CLANG the first two come
 * from kernel/cfi.c, so they are only provided here in the non-CFI case.
 */
#include <linux/export.h>
#include <linux/types.h>

#ifndef CONFIG_CFI_CLANG

void __cfi_slowpath(uint64_t id, void *ptr, void *diag)
{
}

void __ubsan_handle_cfi_check_fail_abort(void *data, void *ptr)
{
}

EXPORT_SYMBOL_GPL(__cfi_slowpath);
EXPORT_SYMBOL_GPL(__ubsan_handle_cfi_check_fail_abort);

#endif /* !CONFIG_CFI_CLANG */

/*
 * With per-task canaries the arch code does not export the global canary that
 * non-per-task modules (i.e. all of the vendor modules) reference.
 */
#ifdef CONFIG_STACKPROTECTOR_PER_TASK
unsigned long __stack_chk_guard;
EXPORT_SYMBOL(__stack_chk_guard);
#endif
