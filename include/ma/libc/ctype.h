#ifndef MA_LIBC_CTYPE_H
#define MA_LIBC_CTYPE_H

#include <ma/libc/internal.h>

#if MA_USE_LIBFT
#include <ft/ctype.h>
#elif !MA_PLATFORM_BARE
#include <ctype.h>
#endif

#if MA_PLATFORM_BARE
int ma_isalnum(int c);
int ma_isalpha(int c);
int ma_iscntrl(int c);
int ma_isdigit(int c);
int ma_isgraph(int c);
int ma_islower(int c);
int ma_isprint(int c);
int ma_ispunct(int c);
int ma_isspace(int c);
int ma_isupper(int c);
int ma_isxdigit(int c);
int ma_isblank(int c);
int ma_toupper(int c);
int ma_tolower(int c);
#else

inline int ma_isalnum(int c) { return MA_LIBC_PREFIX(isalnum)(c); }
inline int ma_isalpha(int c) { return MA_LIBC_PREFIX(isalpha)(c); }
inline int ma_iscntrl(int c) { return MA_LIBC_PREFIX(iscntrl)(c); }
inline int ma_isdigit(int c) { return MA_LIBC_PREFIX(isdigit)(c); }
inline int ma_isgraph(int c) { return MA_LIBC_PREFIX(isgraph)(c); }
inline int ma_islower(int c) { return MA_LIBC_PREFIX(islower)(c); }
inline int ma_isprint(int c) { return MA_LIBC_PREFIX(isprint)(c); }
inline int ma_ispunct(int c) { return MA_LIBC_PREFIX(ispunct)(c); }
inline int ma_isspace(int c) { return MA_LIBC_PREFIX(isspace)(c); }
inline int ma_isupper(int c) { return MA_LIBC_PREFIX(isupper)(c); }
inline int ma_isxdigit(int c) { return MA_LIBC_PREFIX(isxdigit)(c); }
inline int ma_isblank(int c) { return MA_LIBC_PREFIX(isblank)(c); }
inline int ma_toupper(int c) { return MA_LIBC_PREFIX(toupper)(c); }
inline int ma_tolower(int c) { return MA_LIBC_PREFIX(tolower)(c); }

#endif

#endif
