/**
 * @file scard_dll.c
 *
 *	SmartCard-API DLL interface
 *
 */
#include "scard_dll.h"
#include "global.h"

#define MINIMUM_ENTRIES

#define _E(type,name,args) type (CALLBACK *name) args;
typedef struct functionset_t {
#include "entries.h"
} functionset_t;
#undef _E

#define _E(type,name,args) #name,
static const char *functionset_t_name_tbl[] = {
#include "entries.h"
	NULL
};
#undef _E

#undef MINIMUM_ENTRIES

/****************************************************************************/
/*      scard_trans_t の実装                                                */
/****************************************************************************/
typedef struct private_data_tr_t {
	functionset_t *fs;
	SCARDHANDLE card;
	LPCTSTR dllpath;
	int refcount;
	char padding[4];
} private_data_tr_t;

static private_data_tr_t *private_data_tr(void *self) {
	scard_trans_t *entity = self;
	private_data_tr_t *p;

	assert(entity);

	p = entity->private_data;
	assert((void *)(p + 1) == self);

	return p;
}

static void *release_tr(void *self) {
	private_data_tr_t *_this = private_data_tr(self);

	_this->refcount -= 1;
	if (_this->refcount > 0)
		return self;

	if (_this->fs->SCardDisconnect) {
		DEBUGLOG(("%" PRIt "::SCardDisconnect()\n", _this->dllpath));
		_this->fs->SCardDisconnect(_this->card, SCARD_LEAVE_CARD);
	}

	DEBUGLOG(("[%s] hCard = %p\n", __FUNCTION__, _this->card));

	free(_this);
	return NULL;
}

static LONG transmit_tr(void *self,
						LPCSCARD_IO_REQUEST pioSendPci,
						LPCBYTE pbSendBuffer,
						DWORD cbSendLength,
						LPSCARD_IO_REQUEST pioRecvPci,
						LPBYTE pbRecvBuffer,
						LPDWORD pcbRecvLength) {
	private_data_tr_t *_this = private_data_tr(self);

	return _this->fs->SCardTransmit(_this->card,
									pioSendPci,
									pbSendBuffer,
									cbSendLength,
									pioRecvPci,
									pbRecvBuffer,
									pcbRecvLength);
}

static void addref_tr(void *self) {
	private_data_tr_t *_this = private_data_tr(self);

	_this->refcount += 1;
}

static scard_trans_t *trans_create(functionset_t *fs, SCARDHANDLE hCard, LPCTSTR dllpath) {
	private_data_tr_t *p;
	scard_trans_t *e;

	p = calloc(1, sizeof(*p) + sizeof(*e));

	assert(p);
	if (!p)
		return NULL;

	e = (scard_trans_t *)(p + 1);

	e->private_data = p;
#define SETMETHOD(name) e->name = name##_tr
	SETMETHOD(release);
	SETMETHOD(transmit);
	SETMETHOD(addref);
#undef SETMETHOD

	p->fs   = fs;
	p->card = hCard;
	p->dllpath = dllpath;

	DEBUGLOG(("[%s] hCard = %p\n", __FUNCTION__, hCard));

	return e;
}

/****************************************************************************/
/*      scard_dll_t の実装                                                  */
/****************************************************************************/
typedef struct private_data_dl_t {
	HINSTANCE instance;
	functionset_t fs;
	SCARDCONTEXT context;
	TCHAR *dllpath SCOPED_PTR;
} private_data_dl_t;

static private_data_dl_t *private_data_dl(void *self) {
	scard_dll_t *entity = self;
	private_data_dl_t *p;

	assert(entity);

	p = entity->private_data;
	assert((void *)(p + 1) == self);

	return p;
}

static BOOL init_dl(void *self, LPCTSTR path_to_dll) {
	private_data_dl_t *_this = private_data_dl(self);
	HINSTANCE hInstance;

	assert(!_this->instance);

	hInstance = LoadLibrary(path_to_dll);
	if (!hInstance) {
		dprintf("%" PRIt " LoadLibrary failed\n", path_to_dll);
		return FALSE;
	}

	DEBUGLOG(("%" PRIt " loaded\n", path_to_dll));

	{
		const char **const name = functionset_t_name_tbl;
		FARPROC *const proc = (FARPROC *)&_this->fs;
		int i = 0;
		while (name[i]) {
			proc[i] = GetProcAddress(hInstance, name[i]);
			i++;
		}

		assert(_this->fs.SCardListReadersA);
		assert(_this->fs.SCardTransmit);
		if (!_this->fs.SCardListReadersA || !_this->fs.SCardTransmit) {
			dprintf("%" PRIt " GetProcAddress failed\n", path_to_dll);
			FreeLibrary(hInstance);
			return FALSE;
		}
	}

	if (_this->fs.SCardEstablishContext) {
		LONG result;
		DEBUGLOG(("%" PRIt "::SCardEstablishContext()\n", path_to_dll));
		result = _this->fs.SCardEstablishContext(SCARD_SCOPE_USER, NULL, NULL, &_this->context);
		assert(result == SCARD_S_SUCCESS);
		if (result != SCARD_S_SUCCESS) {
			dprintf("%" PRIt "::SCardEstablishContext failed\n", path_to_dll);
			FreeLibrary(hInstance);
			return FALSE;
		}
	}

	_this->dllpath = _tcsdup(path_to_dll);
	assert(_this->dllpath);
	_this->instance = hInstance;

	return TRUE;
}

static void release_dl(void *self) {
	private_data_dl_t *_this = private_data_dl(self);

	if (_this->instance) {
		if (_this->fs.SCardReleaseContext) {
			DEBUGLOG(("%" PRIt "::SCardReleaseContext()\n", _this->dllpath));
			_this->fs.SCardReleaseContext(_this->context);
		}
		FreeLibrary(_this->instance);
		DEBUGLOG(("%" PRIt " unloaded\n", _this->dllpath));
	}

	free(_this->dllpath);

	free(_this);
}

static LONG listReaders_dl(void *self, LPSTR mszReaders, LPDWORD pcchReaders) {
	private_data_dl_t *_this = private_data_dl(self);
	CHAR buf[MAX_READERS_LIST_CH];
	LPCSTR p;
	DWORD cchVerified;
	LONG result;

	assert(_this->instance);

	/* SCardMemoryFreeを実装してないDLLがあるのでAUTOALLOCATEは使用禁止 */
	assert(*pcchReaders != SCARD_AUTOALLOCATE);
	if (*pcchReaders == SCARD_AUTOALLOCATE)
		return SCARD_E_INVALID_PARAMETER;

	if (!mszReaders) {
		/* 文字数クエリに対応していないDLLへの備え */
		*pcchReaders = _ARRAYSIZE(buf);
		mszReaders = buf;
	}

	/* ファイナルターミネータを付けてくれないDLLへの備え */
	memset(mszReaders, 0, sizeof(*mszReaders) * (*pcchReaders));

	DEBUGLOG(("%" PRIt "::SCardListReadersA()\n", _this->dllpath));
	result = _this->fs.SCardListReadersA(_this->context, NULL, mszReaders, pcchReaders);
	if (result != SCARD_S_SUCCESS)
		return result;

	p = mszReaders;
	/* "\0\0" が現れるまでpを進める */
	while (p[0] || p[1])
		++p;
	cchVerified = (DWORD)(p - mszReaders) + 2;

	if (*pcchReaders != cchVerified) {
		dprintf("** warning ** wrong cchReaders=%d, fix to %d. %" PRIt "\n", *pcchReaders, cchVerified, _this->dllpath);
		*pcchReaders = cchVerified;
	}

	return result;
}

static scard_trans_t *connect_dl(void *self, LPCSTR szReader) {
	private_data_dl_t *_this = private_data_dl(self);
	SCARDHANDLE hCard;

	if (_this->fs.SCardConnectA) {
		DWORD dwActiveProtocol;
		LONG result;
		assert(_this->instance);
		DEBUGLOG(("%" PRIt "::SCardConnectA()\n", _this->dllpath));
		result = _this->fs.SCardConnectA(_this->context,
										 szReader,
										 SCARD_SHARE_SHARED,
										 SCARD_PROTOCOL_T1,
										 &hCard,
										 &dwActiveProtocol);
		if (result != SCARD_S_SUCCESS)
			return NULL;
	}
	else
		hCard = (SCARDHANDLE)self; /* 特に意味は無い */

	return trans_create(&_this->fs, hCard, _this->dllpath);
}

static LPCTSTR dllpath_dl(void *self) {
	private_data_dl_t *_this = private_data_dl(self);

	return _this->dllpath;
}

static transmit_hook_t get_transmit_hook_dl(void *self) {
	private_data_dl_t *_this = private_data_dl(self);
	typedef transmit_hook_t(CALLBACK * proc)(void);
	proc get_transmit_hook;

	get_transmit_hook = (proc)GetProcAddress(_this->instance, "get_transmit_hook");
	if (!get_transmit_hook)
		return NULL;

	return get_transmit_hook();
}

static BOOL scard_hub_ready_dl(void *self, HINSTANCE hHubInstance) {
	private_data_dl_t *_this = private_data_dl(self);
	typedef void (CALLBACK * proc)(HINSTANCE);
	proc scard_hub_ready;

	assert(_this->instance);

	scard_hub_ready = (proc)GetProcAddress(_this->instance, "scard_hub_ready");

	if (!scard_hub_ready) {
		/* scard_hub_readyが無いなら旧式のset_winscard_apiを試みる */
		struct scard_func_set_t;
		typedef void (CALLBACK * proc2)(const struct scard_func_set_t *, HINSTANCE);
		proc2 set_winscard_api;

		set_winscard_api = (proc2)GetProcAddress(_this->instance, "set_winscard_api");

		if (!set_winscard_api)
			return FALSE;

		DEBUGLOG(("%" PRIt "::set_winscard_api() %c ** DEPRECATED **\n", _this->dllpath, '{'));
		set_winscard_api(NULL, hHubInstance); /* scard_func_set_tによるAPI渡しは廃止 */
		DEBUGLOG(("%" PRIt "::set_winscard_api() %c ** DEPRECATED **\n", _this->dllpath, '}'));

		return TRUE;
	}

	DEBUGLOG(("%" PRIt "::scard_hub_ready() %c\n", _this->dllpath, '{'));
	scard_hub_ready(hHubInstance);
	DEBUGLOG(("%" PRIt "::scard_hub_ready() %c\n", _this->dllpath, '}'));

	return TRUE;
}

scard_dll_t *create_scard_dll(void) {
	private_data_dl_t *p;
	scard_dll_t *e;

	p = calloc(1, sizeof(*p) + sizeof(*e));

	assert(p);
	if (!p)
		return NULL;

	e = (scard_dll_t *)(p + 1);

	e->private_data = p;

#define SETMETHOD(name) e->name = name##_dl
	SETMETHOD(init);
	SETMETHOD(release);
	SETMETHOD(listReaders);
	SETMETHOD(connect);
	SETMETHOD(dllpath);
	SETMETHOD(get_transmit_hook);
	SETMETHOD(scard_hub_ready);
#undef SETMETHOD

	return e;
}
