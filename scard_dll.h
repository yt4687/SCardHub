/**
 * @file scard_dll.h
 *
 *	SmartCard-API DLL interface
 *
 */
#pragma once

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif

#include <wtypes.h>
#include <tchar.h>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

typedef struct scard_dll_t scard_dll_t;
typedef struct scard_trans_t scard_trans_t;

typedef BOOL (*transmit_hook_t)(
	LPCBYTE pbSendBuffer,
	DWORD cbSendLength,
	LPBYTE pbRecvBuffer,
	LPDWORD pcbRecvLength);

/* SmartCard-APIを持つDLLへのアクセサオブジェクト */
struct scard_dll_t {
	void *private_data;
	BOOL (*init)(void *self, LPCTSTR path_to_dll);
	void (*release)(void *self);
	LONG(*listReaders)(void *self, LPSTR mszReaders, LPDWORD pcchReaders);
	scard_trans_t *(*connect)(void *self, LPCSTR szReader);
	LPCTSTR(*dllpath)(void *self);
	/* 以下、SCardHub専用DLL用 */
	transmit_hook_t (*get_transmit_hook)(void *self);
	BOOL (*scard_hub_ready)(void *self, HINSTANCE hHubInstance);
};

/* SCardTransmit関数オブジェクト */
struct scard_trans_t {
	void *private_data;
	void *(*release)(void *self);
	LONG(*transmit)(void *self,
					LPCSCARD_IO_REQUEST pioSendPci,
					LPCBYTE pbSendBuffer,
					DWORD cbSendLength,
					LPSCARD_IO_REQUEST pioRecvPci,
					LPBYTE pbRecvBuffer,
					LPDWORD pcbRecvLength);
	void (*addref)(void *self);
};

extern scard_dll_t *create_scard_dll(void);
