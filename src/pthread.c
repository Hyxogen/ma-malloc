#if MA_USE_PTHREAD
#include "ma/internal.h"
#include <pthread.h>

int ma_init_mutex(ma_mtx *mtx)
{
	return pthread_mutex_init(mtx, NULL);
}

int ma_lock_mutex(ma_mtx *mtx)
{
	return pthread_mutex_lock(mtx);

}

int ma_unlock_mtx(ma_mtx *mtx)
{
	return pthread_mutex_unlock(mtx);
}
#endif
