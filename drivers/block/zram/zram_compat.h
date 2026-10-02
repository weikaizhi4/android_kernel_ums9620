/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Small compatibility shims so that the mainline (v7.x) zram driver can be
 * built on this 5.4 based tree.
 */
#ifndef _ZRAM_COMPAT_H_
#define _ZRAM_COMPAT_H_

#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/mm.h>
#include <linux/bio.h>

/* 6.15+ typed allocation helper (no custom gfp flag variant is used here) */
#ifndef kzalloc_obj
#define kzalloc_obj(p)	kzalloc(sizeof(p), GFP_KERNEL)
#endif

/* 5.11+ per-CPU mapping helpers; identical to kmap() on !HIGHMEM and on arm64 */
#ifndef kmap_local_page
#define kmap_local_page(page)	kmap(page)
#define kunmap_local(addr)	kunmap(addr)
#endif


/*
 * 6.x scope guards: the driver uses guard(rwsem_read|rwsem_write)(&lock).
 * Provide an equivalent built on the cleanup attribute.
 */
#ifndef guard
#define __zram_guard_id(prefix)	__PASTE(__PASTE(__UNIQUE_ID_, prefix), __COUNTER__)
#define guard(_name)		CLASS(_name, __zram_guard_id(guard))
#define CLASS(_name, var)							class_##_name##_t var								__attribute__((__cleanup__(class_##_name##_destructor))) = 		class_##_name##_constructor

typedef struct rw_semaphore *class_rwsem_read_t;
typedef struct rw_semaphore *class_rwsem_write_t;

static inline class_rwsem_read_t
class_rwsem_read_constructor(struct rw_semaphore *lock)
{
	down_read(lock);
	return lock;
}
static inline void class_rwsem_read_destructor(class_rwsem_read_t *p)
{
	up_read(*p);
}
static inline class_rwsem_write_t
class_rwsem_write_constructor(struct rw_semaphore *lock)
{
	down_write(lock);
	return lock;
}
static inline void class_rwsem_write_destructor(class_rwsem_write_t *p)
{
	up_write(*p);
}
#endif /* guard */

/* 6.x page helper */
#ifndef memset_page
static inline void zram_memset_page(struct page *page, unsigned int offset,
				    int val, size_t len)
{
	void *addr = kmap_atomic(page);

	memset((char *)addr + offset, val, len);
	kunmap_atomic(addr);
}
#define memset_page(page, offset, val, len)	\
	zram_memset_page((page), (offset), (val), (len))
#endif

/* 6.x page/BVEC helpers */
#ifndef memzero_page
static inline void zram_memzero_page(struct page *page, unsigned int offset,
				     size_t len)
{
	void *addr = kmap_atomic(page);

	memset((char *)addr + offset, 0, len);
	kunmap_atomic(addr);
}
#define memzero_page(page, offset, len)	\
	zram_memzero_page((page), (offset), (len))
#endif

#ifndef memcpy_to_bvec
static inline void zram_memcpy_to_bvec(struct bio_vec *bvec, const void *src)
{
	void *dst = kmap_local_page(bvec->bv_page);

	memcpy((char *)dst + bvec->bv_offset, src, bvec->bv_len);
	kunmap_local(dst);
}
static inline void zram_memcpy_from_bvec(char *dst, struct bio_vec *bvec)
{
	void *src = kmap_local_page(bvec->bv_page);

	memcpy(dst, (char *)src + bvec->bv_offset, bvec->bv_len);
	kunmap_local(src);
}
#define memcpy_to_bvec(bvec, src)	zram_memcpy_to_bvec((bvec), (src))
#define memcpy_from_bvec(dst, bvec)	zram_memcpy_from_bvec((dst), (bvec))
#endif

/* 6.x bio accounting and iteration helpers (5.4 uses the generic_* forms) */
#ifndef bio_start_io_acct
/* 5.4: generic_start_io_acct() returns void, so carry jiffies ourselves */
#define bio_start_io_acct(bio)						\
	({								\
		generic_start_io_acct((bio)->bi_disk->queue, bio_op(bio), \
				      bio_sectors(bio),			\
				      &(bio)->bi_disk->part0);		\
		jiffies;						\
	})
#define bio_end_io_acct(bio, start_time)				\
	generic_end_io_acct((bio)->bi_disk->queue, bio_op(bio),		\
			    &(bio)->bi_disk->part0, (start_time))
#endif
#ifndef bio_advance_iter_single
#define bio_advance_iter_single(bio, iter, bytes)	\
	bio_advance_iter((bio), (iter), (bytes))
#endif

/* 6.x lockdep shorthands (5.4 has the longer lock_acquire/lock_release) */
#undef mutex_acquire
#undef mutex_release
#ifndef mutex_acquire
#define mutex_acquire(map, subclass, trylock, ip)		lock_acquire((map), (subclass), (trylock), 0, 1, NULL, (ip))
#define mutex_release(map, ip)	lock_release((map), 1, (ip))
#endif

#include <linux/rwsem.h>

#endif /* _ZRAM_COMPAT_H_ */
