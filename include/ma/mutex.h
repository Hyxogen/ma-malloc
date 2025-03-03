#ifndef MA_MUTEX_H
#define MA_MUTEX_H

#define MA_NO_MUTEX 0
#define MA_C11_MUTEX 1
#define MA_PTHREAD_MUTEX 2
#define MA_SPINLOCK_MUTEX 3

#ifndef MA_USE_MUTEX
#define MA_USE_MUTEX MA_C11_MUTEX
#endif

#if MA_USE_MUTEX == MA_NO_MUTEX
typedef int ma_mtx;

[[nodiscard]]
inline int ma_init_mutex(ma_mtx *mtx)
{
	(void)mtx;
	return 0;
}
[[nodiscard]]
inline int ma_lock_mutex(ma_mtx *mtx)
{
	(void)mtx;
	return 0;
}
[[nodiscard]]
inline int ma_unlock_mutex(ma_mtx *mtx)
{
	(void)mtx;
	return 0;
}

#elif MA_USE_MUTEX == MA_C11_MUTEX
#include <threads.h>
typedef mtx_t ma_mtx;

[[nodiscard]]
inline int ma_init_mutex(ma_mtx *mtx)
{
	return mtx_init(mtx, mtx_plain) == thrd_error;
}
[[nodiscard]]
inline int ma_lock_mutex(ma_mtx *mtx)
{
	return mtx_lock(mtx) == thrd_error;
}
[[nodiscard]]
inline int ma_unlock_mutex(ma_mtx *mtx)
{
	return mtx_unlock(mtx) == thrd_error;
}

#elif MA_USE_MUTEX == MA_PTHREAD_MUTEX
#include <pthread.h>
typedef pthread_mutex_t ma_mtx;

[[nodiscard]]
inline int ma_init_mutex(ma_mtx *mtx)
{
	return pthread_mutex_init(mtx, NULL);
}

[[nodiscard]]
inline int ma_lock_mutex(ma_mtx *mtx)
{
	return pthread_mutex_lock(mtx);
}

[[nodiscard]]
inline int ma_unlock_mutex(ma_mtx *mtx)
{
	return pthread_mutex_unlock(mtx);
}

#elif MA_USE_MUTEX == MA_SPINLOCK_MUTEX
#include <stdatomic.h>
struct spinlock_s {
	atomic_flag _locked;
};

typedef struct spinlock_s ma_mtx;

[[nodiscard]]
static inline int ma_init_mutex(ma_mtx *mtx)
{
	atomic_flag_clear(&mtx->_locked);
	return 0;
}

[[nodiscard]]
static inline int ma_lock_mutex(ma_mtx *mtx)
{
	while (atomic_flag_test_and_set_explicit(&mtx->_locked, memory_order_acquire)) {
		continue;
	}
	return 0;
}

[[nodiscard]]
static inline int ma_unlock_mutex(ma_mtx *mtx)
{
	atomic_flag_clear_explicit(&mtx->_locked, memory_order_release);
	return 0;
}

#else
#error "Unknown MA_USE_MUTEX value"
#endif

#endif
