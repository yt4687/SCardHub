/**
 * @file scard_stuff.h
 *
 *	SmartCardAPIの実装に必要な諸々
 *
 */
#pragma once

#include "scard_api.h"

#ifndef SCOPED_PTR
#define SCOPED_PTR /* ポインタの所有権(解放責任)示す目印 */
#endif

struct scard_dll_t;
struct scard_trans_t;

/****************************************************************************/
extern BOOL scard_stuff_construct(void);
extern void scard_stuff_destruct(void);

/****************************************************************************/
extern BOOL register_dll(struct scard_dll_t *dll);
extern void scard_hub_ready(HINSTANCE hHubInstance);
extern BOOL tryhook(LPCBYTE pbSendBuffer, DWORD cbSendLength, LPBYTE pbRecvBuffer, LPDWORD pcbRecvLength, struct scard_dll_t *exclude);

/****************************************************************************/
extern void *scmalloc(SCARDCONTEXT hContext, size_t size);
extern BOOL scfree(SCARDCONTEXT hContext, void *ptr);

/****************************************************************************/
extern SCARDCONTEXT establish_context(SCARDCONTEXT hRealContext);
extern BOOL validate_context(SCARDCONTEXT hContext, LPSCARDCONTEXT phRealContext);
extern LONG release_context(SCARDCONTEXT hContext);

/****************************************************************************/
extern BOOL register_dll_readers(LPCSTR szRealReaders);
extern BOOL register_real_readers(LPCSTR szRealReaders);
extern void shunt_readers(void);
extern BOOL list_readersA(DWORD *pcchReaders, LPSTR *pszReaders);
extern BOOL list_readersW(DWORD *pcchReaders, LPWSTR *pszReaders);

/****************************************************************************/
extern BOOL is_fakereaderA(LPCSTR szReader);
extern BOOL is_fakereaderW(LPCWSTR szReader);

/****************************************************************************/
extern LONG card_connectA(SCARDCONTEXT hContext, LPCSTR  szReader, DWORD dwShareMode, DWORD dwPreferredProtocols, LPSCARDHANDLE phCard, LPDWORD pdwActiveProtocol);
extern LONG card_connectW(SCARDCONTEXT hContext, LPCWSTR szReader, DWORD dwShareMode, DWORD dwPreferredProtocols, LPSCARDHANDLE phCard, LPDWORD pdwActiveProtocol);
extern LONG card_disconnect(SCARDHANDLE hCard, DWORD dwDisposition);

extern SCARDCONTEXT get_card_context(SCARDHANDLE hCard);
extern LPCSTR       get_card_readerA(SCARDHANDLE hCard);
extern LPCWSTR      get_card_readerW(SCARDHANDLE hCard);
extern struct scard_dll_t   *get_card_dll(SCARDHANDLE hCard);
extern struct scard_trans_t *get_card_trans(SCARDHANDLE hCard);

extern BOOL validate_card(SCARDHANDLE hCard, LPSCARDHANDLE phRealCard);


/****************************************************************************/

struct sys_api_t {
#define _E(type,name,args) type (CALLBACK *name) args;
#include "entries.h"
#undef _E
	HINSTANCE instance;
};

extern struct sys_api_t g_sys_api;
