/**
 * @file WinSCard.c
 *
 *	Smart Card API Hub for DTV apps.
 *
 */

#ifndef SAME_INI_BASENAME_AS_DLL
#define SAME_INI_BASENAME_AS_DLL 0
#endif

#include "global.h"
#include "scard_api.h"
#include "scard_dll.h"
#include "scard_stuff.h"

#define BASENAME "SCardHub"

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif

#include <shlwapi.h>
#include <shlobj.h>
#include <stdarg.h>
#include <locale.h>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

void dprintf(const char *fmt, ...) {
	char tmp[1024];
	va_list ap;

#ifdef _UNICODE
	char locale_save[64];
	strcpy(locale_save, setlocale(LC_ALL, NULL));
	setlocale(LC_ALL, ".OCP");
#endif

	va_start(ap, fmt);
	vsnprintf(tmp, _ARRAYSIZE(tmp), fmt, ap);
	va_end(ap);

#ifdef _UNICODE
	setlocale(LC_ALL, locale_save);
#endif

	OutputDebugStringA(tmp);
}

static const LPCTSTR kCaption = _T("SCardHub(WinSCard.dll)");

static const BYTE kBCAS_ATR[] = {
	0x3B, 0xF0, 0x12, 0x00, 0xFF, 0x91, 0x81, 0xB1,
	0x7C, 0x45, 0x1F, 0x03, 0x99
};

static BOOL s_place_at_end;
static BOOL s_hexdump;

/* 本物SCardAPIへのアクセスマクロ */
#define F(name) g_sys_api.name

/****************************************************************************/

static int parse_dll_line(HINSTANCE hinstDLL, TCHAR *line, TCHAR *end) {
	int n = 0;

	while (line < end && *line) {
		TCHAR path[MAX_PATH];
		scard_dll_t *dll;

		{
			TCHAR *p = _tcschr(line, _T(';'));
			if (p)
				*p = _T('\0');
		}

		if (PathIsRelative(line)) {
			GetModuleFileName(hinstDLL, path, _ARRAYSIZE(path));
			PathRemoveFileSpec(path);
			PathAppend(path, line);
		}
		else
			_tcscpy(path, line);

		line += _tcslen(line) + 1;

		dll = create_scard_dll();
		if (!dll)
			return -1;

		if (!dll->init(dll, path)) {
			TCHAR msg[MAX_PATH * 2];
#ifdef _MSC_VER
			_sntprintf(msg, _ARRAYSIZE(msg), _T("カードリーダDLLのロードに失敗しました。\n\n%s"), path);
#else
			_sntprintf(msg, _ARRAYSIZE(msg), _T("Failed to load the card reader DLL.\n\n%s"), path);
#endif
			MessageBox(GetDesktopWindow(), msg, kCaption, MB_ICONWARNING);
			dll->release(dll);
			continue;
		}

		if (!register_dll(dll))
			return -1;

		n += 1;
	}

	return n;
}

static BOOL parse_ini(HINSTANCE hinstDLL, const TCHAR *ini_path) {
	TCHAR linebuf[0x1000];
	TCHAR *end;
	int i;
	int rc;

	s_place_at_end = GetPrivateProfileInt(_T("option"), _T("place_at_end"), 0, ini_path) != 0;
	s_hexdump = GetPrivateProfileInt(_T("option"), _T("hexdump"), 0, ini_path) != 0;

	end = linebuf + GetPrivateProfileString(_T("option"), _T("reader_dlls"),
											_T(""), linebuf, _ARRAYSIZE(linebuf), ini_path);

	rc = parse_dll_line(hinstDLL, linebuf, end);
	if (rc < 0)
		return FALSE;

	for (i = 0; i < 100; i++) {
		TCHAR tag[8];
		_stprintf_s(tag, _ARRAYSIZE(tag), _T("dll%02d"), i);
		end = linebuf + GetPrivateProfileString(_T("option"), tag,
												_T(""), linebuf, _ARRAYSIZE(linebuf), ini_path);

		rc = parse_dll_line(hinstDLL, linebuf, end);
		if (rc < 0)
			return FALSE;
	}

	return TRUE;
}

static BOOL find_ini(HINSTANCE hinstDLL, TCHAR ini_path[MAX_PATH]) {
	TCHAR msgbuf[MAX_PATH * 4];
	TCHAR *msg = msgbuf;
	TCHAR *end = msgbuf + _ARRAYSIZE(msgbuf);

#if SAME_INI_BASENAME_AS_DLL
	TCHAR confnamebuf[MAX_PATH], *confname;
	GetModuleFileName(hinstDLL, confnamebuf, _ARRAYSIZE(confnamebuf));
	PathRemoveExtension(confnamebuf);
	PathAddExtension(confnamebuf, _T(".ini"));
	confname = PathFindFileName(confnamebuf);
#else
	const TCHAR confname[] = _T(BASENAME) _T(".ini");
#endif

#ifdef _MSC_VER
	_tcscpy(msg, _T("iniファイルを以下の何れかに配置してください:\n\n"));
#else
	_tcscpy(msg, _T("Please place the ini file to one of the following:\n\n"));
#endif
	msg += _tcslen(msg);

	/* dllと同じフォルダ */
	GetModuleFileName(hinstDLL, ini_path, MAX_PATH);
	PathRemoveFileSpec(ini_path);
	PathAppend(ini_path, confname);
	if (PathFileExists(ini_path))
		return TRUE;

	msg += _sntprintf(msg, end - msg, _T("%s\n\n"), ini_path);

	/* AppDataフォルダ (ユーザーフォルダ) */
	SHGetFolderPath(NULL, CSIDL_APPDATA, NULL, 0, ini_path);
	PathAppend(ini_path, confname);
	if (PathFileExists(ini_path))
		return TRUE;

	msg += _sntprintf(msg, end - msg, _T("%s\n\n"), ini_path);

	/* Windowsフォルダ (C:\Windows) */
	GetWindowsDirectory(ini_path, MAX_PATH);
	PathAppend(ini_path, confname);
	if (PathFileExists(ini_path))
		return TRUE;

	msg += _sntprintf(msg, end - msg, _T("%s\n"), ini_path);

	MessageBox(GetDesktopWindow(), msgbuf, kCaption, MB_ICONERROR);

	return FALSE;
}

static BOOL update_reader_list(void) {
	SCARDCONTEXT hContext;
	LPSTR szRealReaders;
	LONG result;

	/* 既存のreader_tを退避 */
	shunt_readers();

	/* 実カードリーダリスト取得 */
	result = F(SCardEstablishContext)(SCARD_SCOPE_USER, NULL, NULL, &hContext);
	if (result == SCARD_S_SUCCESS) {
		DWORD dwAutoAllocate = SCARD_AUTOALLOCATE;
		result = F(SCardListReadersA)(hContext, NULL, (LPSTR)&szRealReaders, &dwAutoAllocate);
		if (result != SCARD_S_SUCCESS)
			szRealReaders = NULL;
	}
	else
		szRealReaders = NULL;

	if (szRealReaders) {
		if (s_place_at_end) {
			if (!register_real_readers(szRealReaders))
				return FALSE;

			/* dllを後に配置 */
			if (!register_dll_readers(szRealReaders))
				return FALSE;
		}
		else {
			/* dllを先に配置 */
			if (!register_dll_readers(szRealReaders))
				return FALSE;

			if (!register_real_readers(szRealReaders))
				return FALSE;
		}
	}
	else {
		if (!register_dll_readers(szRealReaders))
			return FALSE;
	}

	F(SCardReleaseContext)(hContext);

	return TRUE;
}

/****************************************************************************/
static HINSTANCE g_hinstDLL;

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
	(void)lpvReserved;
#ifdef _DEBUG
	CHAR dll_path[MAX_PATH];
	GetModuleFileNameA(hinstDLL, dll_path, MAX_PATH);
#endif
	g_hinstDLL = hinstDLL;

	if (fdwReason == DLL_PROCESS_ATTACH) {
		TCHAR ini_path[MAX_PATH];

		DEBUGLOG(("-------- DLL_PROCESS_ATTACH { %s\n", dll_path));

#if 0	/* http://support.microsoft.com/kb/555563/en-us */
		DisableThreadLibraryCalls(hinstDLL);
#endif

		if (!scard_stuff_construct())
			return FALSE;

		if (!find_ini(hinstDLL, ini_path))
			return FALSE;

		if (!parse_ini(hinstDLL, ini_path))
			return FALSE;

		DEBUGLOG(("-------- DLL_PROCESS_ATTACH } %s\n", dll_path));
	}
	else if (fdwReason == DLL_PROCESS_DETACH) {
		DEBUGLOG(("-------- DLL_PROCESS_DETACH { %s\n", dll_path));

		scard_stuff_destruct();

		DEBUGLOG(("-------- DLL_PROCESS_DETACH } %s\n", dll_path));
	}

	return TRUE;
}

/*
 *	Windows7において、
 *	DllMainからシステムのWinSCard.dllのエントリを呼ぶとフリーズしてしまう。
 *	原因は不明。
 *
 *	これを回避するため、当該エントリを要する初期化手続きは、
 *	アプリからSCardEstablishContextが初めて呼ばれるタイミングで行う。
 */
static void hubinit(void) {
	static BOOL passed_through = FALSE;

	if (passed_through)
		return;

	passed_through = TRUE;

	DEBUGLOG(("hubinit {\n"));

	/* リーダ名を決め打ちでSCardConnectしてくるアプリに備えておく */
	update_reader_list();

	/* Hub-APIを使う準備が整った */
	scard_hub_ready(g_hinstDLL);

	DEBUGLOG(("hubinit }\n"));
}

/****************************************************************************/
/*  WinSCard APIの実装                                                      */
/****************************************************************************/

SCARD_IO_REQUEST g_rgSCardT1Pci = {
	SCARD_PROTOCOL_T1,
	sizeof(SCARD_IO_REQUEST)
};

#ifndef _In_
#define _In_
#endif
#ifndef _In_opt_
#define _In_opt_
#endif
#ifndef _Inout_
#define _Inout_
#endif
#ifndef _Inout_opt_
#define _Inout_opt_
#endif
#ifndef _Out_
#define _Out_
#endif
#ifndef _Out_opt_
#define _Out_opt_
#endif

HANDLE WINAPI SCardAccessStartedEvent(void) {
	DEBUGLOG((__FUNCTION__ "()\n"));
	return F(SCardAccessStartedEvent)();
}

LONG WINAPI SCardCancel(
	_In_  SCARDCONTEXT hContext
) {
	SCARDCONTEXT hRealContext;

	DEBUGLOG((__FUNCTION__ "(%p)\n", hContext));

	if (validate_context(hContext, &hRealContext) == FALSE)
		return ERROR_INVALID_HANDLE;

	if (!hRealContext)
		return SCARD_S_SUCCESS;

	return F(SCardCancel)(hRealContext);
}

LONG WINAPI SCardConnectA(
	_In_   SCARDCONTEXT hContext,
	_In_   LPCSTR szReader,
	_In_   DWORD dwShareMode,
	_In_   DWORD dwPreferredProtocols,
	_Out_  LPSCARDHANDLE phCard,
	_Out_  LPDWORD pdwActiveProtocol
) {
	DEBUGLOG((__FUNCTION__ "(%p, \"%s\", ...)\n", hContext, szReader));

	if (validate_context(hContext, NULL) == FALSE)
		return ERROR_INVALID_HANDLE;

	if (!szReader || !phCard || !pdwActiveProtocol)
		return SCARD_E_INVALID_PARAMETER;

	return card_connectA(hContext, szReader, dwShareMode, dwPreferredProtocols, phCard, pdwActiveProtocol);
}

LONG WINAPI SCardConnectW(
	_In_   SCARDCONTEXT hContext,
	_In_   LPCWSTR szReader,
	_In_   DWORD dwShareMode,
	_In_   DWORD dwPreferredProtocols,
	_Out_  LPSCARDHANDLE phCard,
	_Out_  LPDWORD pdwActiveProtocol
) {
	DEBUGLOG((__FUNCTION__ "(%p, \"%S\", ...)\n", hContext, szReader));

	if (validate_context(hContext, NULL) == FALSE)
		return ERROR_INVALID_HANDLE;

	if (!szReader || !phCard || !pdwActiveProtocol)
		return SCARD_E_INVALID_PARAMETER;

	return card_connectW(hContext, szReader, dwShareMode, dwPreferredProtocols, phCard, pdwActiveProtocol);
}

LONG WINAPI SCardDisconnect(
	_In_  SCARDHANDLE hCard,
	_In_  DWORD dwDisposition
) {
	if (validate_card(hCard, NULL) == FALSE)
		return ERROR_INVALID_HANDLE;

	DEBUGLOG((__FUNCTION__ "(\"%s\", %d)\n", get_card_readerA(hCard), dwDisposition));

	return card_disconnect(hCard, dwDisposition);
}

LONG WINAPI SCardEstablishContext(
	_In_   DWORD dwScope,
	_In_   LPCVOID pvReserved1,
	_In_   LPCVOID pvReserved2,
	_Out_  LPSCARDCONTEXT phContext
) {
	SCARDCONTEXT hRealContext;
	LONG result;

	DEBUGLOG((__FUNCTION__ "(...)\n"));

	hubinit();

	if (!phContext)
		return SCARD_E_INVALID_PARAMETER;

	result = F(SCardEstablishContext)(dwScope, pvReserved1, pvReserved2, &hRealContext);
	if (result == SCARD_E_NO_SERVICE) {
		DEBUGLOG(("SCARD_E_NO_SERVICE, fallback to fakecontext\n"));
		hRealContext = 0;
	}
	else if (result != SCARD_S_SUCCESS)
		return result;

	*phContext = establish_context(hRealContext);

	return *phContext ? SCARD_S_SUCCESS : SCARD_E_NO_MEMORY;
}

LONG WINAPI SCardFreeMemory(
	_In_  SCARDCONTEXT hContext,
	_In_  LPCVOID pvMem
) {
	SCARDCONTEXT hRealContext;

	DEBUGLOG((__FUNCTION__ "(%p, %p)\n", hContext, pvMem));

	if (validate_context(hContext, &hRealContext) == FALSE)
		return ERROR_INVALID_HANDLE;

	if (scfree(hContext, (void *)pvMem))
		return SCARD_S_SUCCESS;

	/* AUTOALLOCATEは全てscmallocなので、通常ここは通らない */
	return F(SCardFreeMemory)(hRealContext, pvMem);
}

LONG WINAPI SCardGetStatusChangeA(
	_In_     SCARDCONTEXT hContext,
	_In_     DWORD dwTimeout,
	_Inout_  LPSCARD_READERSTATEA rgReaderStates,
	_In_     DWORD cReaders
) {
	SCARDCONTEXT hRealContext;
	LONG result = SCARD_S_SUCCESS;
	DWORD i, j;
	LPSCARD_READERSTATEA realReaderStates;

	DEBUGLOG((__FUNCTION__ "(%p, %d, %p, %d)\n", hContext, dwTimeout, rgReaderStates, cReaders));

	if (validate_context(hContext, &hRealContext) == FALSE)
		return ERROR_INVALID_HANDLE;

	if (!rgReaderStates)
		return SCARD_E_INVALID_PARAMETER;

	realReaderStates = malloc(sizeof(*realReaderStates) * cReaders);
	assert(realReaderStates);

	if (!realReaderStates)
		return SCARD_E_NO_MEMORY;

	for (i = 0, j = 0; i < cReaders; i++) {
		LPSCARD_READERSTATEA p = &rgReaderStates[i];
		DEBUGLOG(("[%d/%d]%s\n", i + 1, cReaders, p->szReader));
		if (!is_fakereaderA(p->szReader))
			realReaderStates[j++] = *p;
	}
	if (j) {
		if (!hRealContext) {
			free(realReaderStates);
			return SCARD_E_UNKNOWN_READER;
		}
		result = F(SCardGetStatusChangeA)(hRealContext, dwTimeout, realReaderStates, j);
		DEBUGLOG((">%x\n", result));
	}

	for (i = 0, j = 0; i < cReaders; i++) {
		LPSCARD_READERSTATEA p = &rgReaderStates[i];
		if (!is_fakereaderA(p->szReader)) {
			*p = realReaderStates[j++];
			continue;
		}

		p->dwEventState = SCARD_STATE_PRESENT;
		p->cbAtr = sizeof(kBCAS_ATR);
		memcpy(p->rgbAtr, kBCAS_ATR, sizeof(kBCAS_ATR));
		if (p->dwCurrentState == SCARD_STATE_UNAWARE)
			continue;
		if ((p->dwCurrentState & SCARD_STATE_PRESENT) == 0) {
			Sleep(0);
			p->dwEventState |= SCARD_STATE_CHANGED;
			continue;
		}
		if (result != SCARD_E_TIMEOUT) {
			Sleep(dwTimeout); /* INFINITEは来ないハズ */
			result = SCARD_E_TIMEOUT;
		}
	}
	free(realReaderStates);

	return result;
}

LONG WINAPI SCardGetStatusChangeW(
	_In_     SCARDCONTEXT hContext,
	_In_     DWORD dwTimeout,
	_Inout_  LPSCARD_READERSTATEW rgReaderStates,
	_In_     DWORD cReaders
) {
	SCARDCONTEXT hRealContext;
	LONG result = SCARD_S_SUCCESS;
	DWORD i, j;
	LPSCARD_READERSTATEW realReaderStates;

	DEBUGLOG((__FUNCTION__ "(%p, %d, %p, %d)\n", hContext, dwTimeout, rgReaderStates, cReaders));

	if (validate_context(hContext, &hRealContext) == FALSE)
		return ERROR_INVALID_HANDLE;

	if (!rgReaderStates)
		return SCARD_E_INVALID_PARAMETER;

	realReaderStates = malloc(sizeof(*realReaderStates) * cReaders);
	assert(realReaderStates);

	if (!realReaderStates)
		return SCARD_E_NO_MEMORY;

	for (i = 0, j = 0; i < cReaders; i++) {
		LPSCARD_READERSTATEW p = &rgReaderStates[i];
		DEBUGLOG(("[%d/%d]%S\n", i + 1, cReaders, p->szReader));
		if (!is_fakereaderW(p->szReader))
			realReaderStates[j++] = *p;
	}
	if (j) {
		if (!hRealContext) {
			free(realReaderStates);
			return SCARD_E_UNKNOWN_READER;
		}
		result = F(SCardGetStatusChangeW)(hRealContext, dwTimeout, realReaderStates, j);
		DEBUGLOG((">%x\n", result));
	}

	for (i = 0, j = 0; i < cReaders; i++) {
		LPSCARD_READERSTATEW p = &rgReaderStates[i];
		if (!is_fakereaderW(p->szReader)) {
			*p = realReaderStates[j++];
			continue;
		}

		p->dwEventState = SCARD_STATE_PRESENT;
		p->cbAtr = sizeof(kBCAS_ATR);
		memcpy(p->rgbAtr, kBCAS_ATR, sizeof(kBCAS_ATR));
		if (p->dwCurrentState == SCARD_STATE_UNAWARE)
			continue;
		if ((p->dwCurrentState & SCARD_STATE_PRESENT) == 0) {
			Sleep(0);
			p->dwEventState |= SCARD_STATE_CHANGED;
			continue;
		}
		if (result != SCARD_E_TIMEOUT) {
			Sleep(dwTimeout); /* INFINITEは来ないハズ */
			result = SCARD_E_TIMEOUT;
		}
	}
	free(realReaderStates);

	return result;
}

LONG WINAPI SCardIsValidContext(
	_In_  SCARDCONTEXT hContext
) {
	SCARDCONTEXT hRealContext;

	DEBUGLOG((__FUNCTION__ "(%p)\n", hContext));

	if (validate_context(hContext, &hRealContext) == FALSE)
		return ERROR_INVALID_HANDLE;

	if (!hRealContext)
		return SCARD_S_SUCCESS;

	return F(SCardIsValidContext)(hRealContext);
}

LONG WINAPI SCardListReadersA(
	_In_      SCARDCONTEXT hContext,
	_In_opt_  LPCSTR mszGroups,
	_Out_     LPSTR mszReaders,
	_Inout_   LPDWORD pcchReaders
) {
	LPSTR szReaders = NULL;
	DWORD cchReaders;
	LONG result;

	(void)mszGroups;

	DEBUGLOG((__FUNCTION__ "(%p, ...)\n", hContext));

	if (validate_context(hContext, NULL) == FALSE)
		return ERROR_INVALID_HANDLE;

	if (!pcchReaders)
		return SCARD_E_INVALID_PARAMETER;

	if (!update_reader_list())
		return SCARD_E_NO_MEMORY;

	if (!list_readersA(&cchReaders, (mszReaders == NULL) ? NULL : &szReaders))
		return SCARD_E_NO_MEMORY;

	if (mszReaders == NULL) { /* query chars */
		*pcchReaders = cchReaders;
		result = SCARD_S_SUCCESS;
		goto bailout;
	}

	if (*pcchReaders == SCARD_AUTOALLOCATE) {
		LPSTR dst = scmalloc(hContext, sizeof(*dst) * cchReaders);
		if (!dst) {
			result = SCARD_E_NO_MEMORY;
			goto bailout;
		}
		*(LPSTR *)mszReaders = dst;
		mszReaders = dst;
		*pcchReaders = cchReaders;
	}
	else if (*pcchReaders < cchReaders) {
		*pcchReaders = cchReaders;
		result = SCARD_E_INSUFFICIENT_BUFFER;
		goto bailout;
	}

	memcpy(mszReaders, szReaders, sizeof(CHAR) * cchReaders);
	*pcchReaders = cchReaders;

	result = SCARD_S_SUCCESS;

bailout:

	free(szReaders);
	return result;
}

LONG WINAPI SCardListReadersW(
	_In_      SCARDCONTEXT hContext,
	_In_opt_  LPCWSTR mszGroups,
	_Out_     LPWSTR mszReaders,
	_Inout_   LPDWORD pcchReaders
) {
	LPWSTR szReaders = NULL;
	DWORD cchReaders;
	LONG result;

	(void)mszGroups;

	DEBUGLOG((__FUNCTION__ "(%p, ...)\n", hContext));

	if (validate_context(hContext, NULL) == FALSE)
		return ERROR_INVALID_HANDLE;

	if (!pcchReaders)
		return SCARD_E_INVALID_PARAMETER;

	if (!update_reader_list())
		return SCARD_E_NO_MEMORY;

	if (!list_readersW(&cchReaders, (mszReaders == NULL) ? NULL : &szReaders))
		return SCARD_E_NO_MEMORY;

	if (mszReaders == NULL) { /* query chars */
		*pcchReaders = cchReaders;
		result = SCARD_S_SUCCESS;
		goto bailout;
	}

	if (*pcchReaders == SCARD_AUTOALLOCATE) {
		LPWSTR dst = scmalloc(hContext, sizeof(*dst) * cchReaders);
		if (!dst) {
			result = SCARD_E_NO_MEMORY;
			goto bailout;
		}
		*(LPWSTR *)mszReaders = dst;
		mszReaders = dst;
		*pcchReaders = cchReaders;
	}
	else if (*pcchReaders < cchReaders) {
		*pcchReaders = cchReaders;
		result = SCARD_E_INSUFFICIENT_BUFFER;
		goto bailout;
	}

	memcpy(mszReaders, szReaders, sizeof(WCHAR) * cchReaders);
	*pcchReaders = cchReaders;

	result = SCARD_S_SUCCESS;

bailout:
	free(szReaders);
	return result;
}

LONG WINAPI SCardReconnect(
	_In_       SCARDHANDLE hCard,
	_In_       DWORD dwShareMode,
	_In_       DWORD dwPreferredProtocols,
	_In_       DWORD dwInitialization,
	_Out_opt_  LPDWORD pdwActiveProtocol
) {
	SCARDHANDLE hRealCard;

	DEBUGLOG((__FUNCTION__ "(\"%s\", ...)\n", get_card_readerA(hCard)));

	if (validate_card(hCard, &hRealCard) == FALSE)
		return ERROR_INVALID_HANDLE;

	if (!hRealCard) {
		if (pdwActiveProtocol)
			*pdwActiveProtocol = SCARD_PROTOCOL_T1;
		return SCARD_S_SUCCESS;
	}

	return F(SCardReconnect)(hRealCard, dwShareMode, dwPreferredProtocols, dwInitialization, pdwActiveProtocol);
}

LONG WINAPI SCardReleaseContext(
	_In_  SCARDCONTEXT hContext
) {
	DEBUGLOG((__FUNCTION__ "(%p)\n", hContext));

	if (validate_context(hContext, NULL) == FALSE)
		return ERROR_INVALID_HANDLE;

	return release_context(hContext);
}

void WINAPI SCardReleaseStartedEvent(void) {
	DEBUGLOG((__FUNCTION__ "()\n"));
	F(SCardReleaseStartedEvent)();
}

LONG WINAPI SCardStatusA(
	_In_         SCARDHANDLE hCard,
	_Out_        LPSTR szkReaderName,
	_Inout_opt_  LPDWORD pcchReaderLen,
	_Out_opt_    LPDWORD pdwState,
	_Out_opt_    LPDWORD pdwProtocol,
	_Out_        LPBYTE pbAtr,
	_Inout_opt_  LPDWORD pcbAtrLen
) {
	SCARDHANDLE hRealCard;
	SCARDCONTEXT hContext;
	LPCSTR szReader;
	void *mem1 = NULL, *mem2 = NULL;

	DEBUGLOG((__FUNCTION__ "(\"%s\", ...)\n", get_card_readerA(hCard)));

	if (validate_card(hCard, &hRealCard) == FALSE)
		return ERROR_INVALID_HANDLE;

	if (hRealCard)
		return F(SCardStatusA)(hRealCard, szkReaderName, pcchReaderLen, pdwState, pdwProtocol, pbAtr, pcbAtrLen);

	hContext = get_card_context(hCard);
	szReader = get_card_readerA(hCard);

	if (pcchReaderLen) {
		const DWORD kReaderLen = (DWORD)strlen(szReader) + 1;
		const DWORD chars = kReaderLen + 1;
		LPSTR dst = szkReaderName;
		if (szkReaderName) {
			if (*pcchReaderLen == SCARD_AUTOALLOCATE) {
				mem1 = scmalloc(hContext, sizeof(*dst) * chars);
				if (!mem1)
					return SCARD_E_NO_MEMORY;
				dst = *(LPSTR *)szkReaderName = mem1;
			}
			else if (*pcchReaderLen < chars) {
				*pcchReaderLen = chars;
				return SCARD_E_INSUFFICIENT_BUFFER;
			}
			strcpy(dst, szReader);
			dst[kReaderLen] = L'\0';
		}
		*pcchReaderLen = chars;
	}

	if (pdwState)
		*pdwState = SCARD_STATE_CHANGED | SCARD_STATE_UNKNOWN;

	if (pdwProtocol)
		*pdwProtocol = SCARD_PROTOCOL_T1;

	if (pcbAtrLen) {
		LPBYTE dst = pbAtr;
		if (pbAtr) {
			if (*pcbAtrLen == SCARD_AUTOALLOCATE) {
				mem2 = scmalloc(hContext, sizeof(kBCAS_ATR));
				if (!mem2) {
					scfree(hContext, mem1);
					return SCARD_E_NO_MEMORY;
				}
				dst = *(LPBYTE *)pbAtr = mem2;
			}
			else if (*pcbAtrLen < sizeof(kBCAS_ATR)) {
				*pcbAtrLen = sizeof(kBCAS_ATR);
				scfree(hContext, mem1);
				return SCARD_E_INSUFFICIENT_BUFFER;
			}
			memcpy(dst, kBCAS_ATR, sizeof(kBCAS_ATR));
		}
		*pcbAtrLen = sizeof(kBCAS_ATR);
	}

	return SCARD_S_SUCCESS;
}

LONG WINAPI SCardStatusW(
	_In_         SCARDHANDLE hCard,
	_Out_        LPWSTR szkReaderName,
	_Inout_opt_  LPDWORD pcchReaderLen,
	_Out_opt_    LPDWORD pdwState,
	_Out_opt_    LPDWORD pdwProtocol,
	_Out_        LPBYTE pbAtr,
	_Inout_opt_  LPDWORD pcbAtrLen
) {
	SCARDHANDLE hRealCard;
	SCARDCONTEXT hContext;
	LPCWSTR szReader;
	void *mem1 = NULL, *mem2 = NULL;

	DEBUGLOG((__FUNCTION__ "(\"%s\", ...)\n", get_card_readerA(hCard)));

	if (validate_card(hCard, &hRealCard) == FALSE)
		return ERROR_INVALID_HANDLE;

	if (hRealCard)
		return F(SCardStatusW)(hRealCard, szkReaderName, pcchReaderLen, pdwState, pdwProtocol, pbAtr, pcbAtrLen);

	hContext = get_card_context(hCard);
	szReader = get_card_readerW(hCard);

	if (pcchReaderLen) {
		const DWORD kReaderLen = (DWORD)wcslen(szReader) + 1;
		const DWORD chars = kReaderLen + 1;
		LPWSTR dst = szkReaderName;
		if (szkReaderName) {
			if (*pcchReaderLen == SCARD_AUTOALLOCATE) {
				mem1 = scmalloc(hContext, sizeof(*dst) * chars);
				if (!mem1)
					return SCARD_E_NO_MEMORY;
				dst = *(LPWSTR *)szkReaderName = mem1;
			}
			else if (*pcchReaderLen < chars) {
				*pcchReaderLen = chars;
				return SCARD_E_INSUFFICIENT_BUFFER;
			}
			wcscpy(dst, szReader);
			dst[kReaderLen] = L'\0';
		}
		*pcchReaderLen = chars;
	}

	if (pdwState)
		*pdwState = SCARD_STATE_CHANGED | SCARD_STATE_UNKNOWN;

	if (pdwProtocol)
		*pdwProtocol = SCARD_PROTOCOL_T1;

	if (pcbAtrLen) {
		LPBYTE dst = pbAtr;
		if (pbAtr) {
			if (*pcbAtrLen == SCARD_AUTOALLOCATE) {
				mem2 = scmalloc(hContext, sizeof(kBCAS_ATR));
				if (!mem2) {
					scfree(hContext, mem1);
					return SCARD_E_NO_MEMORY;
				}
				dst = *(LPBYTE *)pbAtr = mem2;
			}
			else if (*pcbAtrLen < sizeof(kBCAS_ATR)) {
				*pcbAtrLen = sizeof(kBCAS_ATR);
				scfree(hContext, mem1);
				return SCARD_E_INSUFFICIENT_BUFFER;
			}
			memcpy(dst, kBCAS_ATR, sizeof(kBCAS_ATR));
		}
		*pcbAtrLen = sizeof(kBCAS_ATR);
	}

	return SCARD_S_SUCCESS;
}

static void hexdump(LPCSTR szReader, int dir, const uint8_t *it, const uint8_t *end) {
	char buf[32 * 3 + 1], *p;
	const int N = sizeof(buf) / 3;

	if (!s_hexdump)
		return;

	while ((end - it) > N) {
		hexdump(szReader, dir, it, it + N);
		it += N;
	}

	if (it >= end)
		return;

	p = buf;
	while (it < end) {
		static const char hex[] = "0123456789abcdef";
		const int b = *it++;
		p[0] = hex[b >> 4];
		p[1] = hex[b & 15];
		p[2] = ' ';
		p += 3;
	}
	p[-1] = '\0';

	dprintf("%s %c %s\n", szReader, dir, buf);
}

LONG WINAPI SCardTransmit(
	_In_         SCARDHANDLE hCard,
	_In_         LPCSCARD_IO_REQUEST pioSendPci,
	_In_         LPCBYTE pbSendBuffer,
	_In_         DWORD cbSendLength,
	_Inout_opt_  LPSCARD_IO_REQUEST pioRecvPci,
	_Out_        LPBYTE pbRecvBuffer,
	_Inout_      LPDWORD pcbRecvLength
) {
	SCARDHANDLE hRealCard;
	LONG result = SCARD_S_SUCCESS;
	LPCSTR szReader = get_card_readerA(hCard);

	DEBUGLOG((__FUNCTION__ "(\"%s\", ...)\n", szReader));

	if (validate_card(hCard, &hRealCard) == FALSE)
		return ERROR_INVALID_HANDLE;

	hexdump(szReader, 's', pbSendBuffer, pbSendBuffer + cbSendLength);

	if (tryhook(pbSendBuffer, cbSendLength, pbRecvBuffer, pcbRecvLength, get_card_dll(hCard))) {
		hexdump(szReader, '*', pbRecvBuffer, pbRecvBuffer + *pcbRecvLength);
		return SCARD_S_SUCCESS;
	}

	if (hRealCard)
		result = F(SCardTransmit)(hRealCard, pioSendPci, pbSendBuffer, cbSendLength, pioRecvPci, pbRecvBuffer, pcbRecvLength);
	else {
		scard_trans_t *tr = get_card_trans(hCard);
		result = tr->transmit(tr, pioSendPci, pbSendBuffer, cbSendLength, pioRecvPci, pbRecvBuffer, pcbRecvLength);
	}

	if (result == SCARD_S_SUCCESS)
		hexdump(szReader, 'r', pbRecvBuffer, pbRecvBuffer + *pcbRecvLength);

	return result;
}
