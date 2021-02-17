#pragma once

#define _CRT_SECURE_NO_WARNINGS

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <inttypes.h>
#include <assert.h>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

extern void dprintf(const char *fmt, ...);

#ifdef _DEBUG
#define DEBUGLOG(args) dprintf args
#define DEBUGTRACE() dprintf("%s:%d:%s\n", __FILE__, __LINE__, __FUNCTION__)
#else
#define DEBUGLOG(args) ((void)0)
#define DEBUGTRACE() ((void)0)
#endif

#ifdef _UNICODE
#define PRIt "S"
#else
#define PRIt "s"
#endif

#define MAX_READER_CH 0x100

/* SCardListReadersに必要なサイズを間違いなく満たすあろう文字数
	 (文字数クエリに対応していないDLLが存在するため) */
#define MAX_READERS_LIST_CH (MAX_READER_CH * 32)

#ifndef SCOPED_PTR
#define SCOPED_PTR /* ポインタの所有権(解放責任)示す目印 */
#endif

#define static_assert(exp) {typedef char t[(exp) ? 1 : -1]; (void)((t*)"");}
