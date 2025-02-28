#ifndef LIBC_CTYPE_H
#define LIBC_CTYPE_H

#include <libc/internal.h>

#if MA_USE_LIBFT
#include <ft/ctype.h>
#else
#include <ctype.h>
#endif

inline int ma_isalnum(int c) { return LIBC_PREFIX(isalnum)(c); }
inline int ma_isalpha(int c) { return LIBC_PREFIX(isalpha)(c); }
inline int ma_iscntrl(int c) { return LIBC_PREFIX(iscntrl)(c); }
inline int ma_isdigit(int c) { return LIBC_PREFIX(isdigit)(c); }
inline int ma_isgraph(int c) { return LIBC_PREFIX(isgraph)(c); }
inline int ma_islower(int c) { return LIBC_PREFIX(islower)(c); }
inline int ma_isprint(int c) { return LIBC_PREFIX(isprint)(c); }
inline int ma_ispunct(int c) { return LIBC_PREFIX(ispunct)(c); }
inline int ma_isspace(int c) { return LIBC_PREFIX(isspace)(c); }
inline int ma_isupper(int c) { return LIBC_PREFIX(isupper)(c); }
inline int ma_isxdigit(int c) { return LIBC_PREFIX(isxdigit)(c); }
inline int ma_isascii(int c) { return LIBC_PREFIX(isascii)(c); }
inline int ma_isblank(int c) { return LIBC_PREFIX(isblank)(c); }
inline int ma_toupper(int c) { return LIBC_PREFIX(toupper)(c); }
inline int ma_tolower(int c) { return LIBC_PREFIX(tolower)(c); }

#endif
