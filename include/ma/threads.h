#ifndef MA_THREADS_H
#define MA_THREADS_H

#define MA_NO_THREADS 0
#define MA_C11_THREADS 1
#define MA_PTHREAD_THREADS 2

#ifndef MA_USE_THREADS
#define MA_USE_THREADS MA_C11_THREADS
#endif

#if MA_USE_THREADS == MA_NO_THREADS
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

#elif MA_USE_THREADS == MA_C11_THREADS
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

#elif MA_USE_THREADS == MA_PTHREAD_THREADS
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
#else
#error "Unknown MA_USE_THREADS value"
#endif

#endif
