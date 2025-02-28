#include "ma/internal.h"

#if MA_USE_THREAD == MA_NO_THREADS

#elif MA_USE_THREAD == MA_C11_THREADS

#else
int ma_init_mutex(ma_mtx *mtx) { return pthread_mutex_init(mtx, NULL); }
int ma_lock_mutex(ma_mtx *mtx) { return pthread_mutex_lock(mtx); }
int ma_unlock_mutex(ma_mtx *mtx) { return pthread_mutex_unlock(mtx); }
#endif
