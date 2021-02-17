/**
 * @file scard_stuff.c
 *
 *	SmartCardAPIの実装に必要な諸々
 *
 */
#include "global.h"
#include "scard_stuff.h"
#include "scard_dll.h"
#include "linklist.h"

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif

#include <locale.h>
#include <shlwapi.h>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

struct sys_api_t g_sys_api;

static struct linknode	s_memchain;
static struct linknode	s_dllchain;
static struct linknode	s_hookchain;
static struct linknode	s_reader; /* SCardConnet可能なリーダのリスト */
static struct linknode	s_reader_pool; /* リーダリスト再構築用 */
static struct linknode	s_context;
static struct linknode	s_handle_free;
static struct handle_entity_t **s_handle_buf;
static unsigned s_handle_buf_count;
#if 1
static CRITICAL_SECTION s_section;
#else
#define InitializeCriticalSection(x) ((void)0)
#define EnterCriticalSection(x) ((void)0)
#define LeaveCriticalSection(x) ((void)0)
#define DeleteCriticalSection(x) ((void)0)
#endif

typedef struct memchain_t {
	struct memchain_t *next;
	struct memchain_t *prev;
	/*uint8_t payload[0];*/
} memchain_t;

typedef struct context_t {
	struct context_t *next;
	struct context_t *prev;

	struct linknode	handlechain;
	struct linknode	cardchain;
	struct linknode	memchain;

	SCARDCONTEXT hContext; /* 自身を表すハンドル */
	SCARDCONTEXT hRealContext;
} context_t;

typedef struct dllchain_t {
	struct dllchain_t *next;
	struct dllchain_t *prev;

	scard_dll_t       *dll SCOPED_PTR;
	struct linknode    hookchain;
	transmit_hook_t    transmit_hook;
} dllchain_t;

typedef struct reader_t {
	struct reader_t *next;
	struct reader_t *prev;

	/* 表向きのリーダ名 */
	LPSTR  readerA SCOPED_PTR;
	LPWSTR readerW SCOPED_PTR;

	/* dllから得た実際のリーダ名 */
	LPSTR  realname SCOPED_PTR;

	struct scard_dll_t *dll; /* 実カードリーダならNULL */
	struct scard_trans_t *transmitter SCOPED_PTR;

} reader_t;

typedef struct card_t {
	struct card_t *next;
	struct card_t *prev;

	SCARDCONTEXT hContext;
	SCARDHANDLE hCard; /* 自身のハンドル */
	SCARDHANDLE hRealCard; /* dllなら0 */
	struct scard_dll_t *dll; /* 実カードならNULL */
	struct reader_t *reader;
	struct scard_trans_t *transmitter; /* 実カードならNULL */
} card_t;

static void unregister_dll(dllchain_t *p);
static void release_reader(reader_t *p);
static void release_context_impl(context_t *ctx);
static void release_card_impl(card_t *p);
static reader_t *find_readerA(struct linknode *list, LPCSTR szReader);

static void release_node(void *p) {
	node_cut(p);
	free(p);
}

#define RELEASE_CHAIN(type, base, func) { \
	type *const _base = (type *)(base);   \
	while (!is_list_empty(_base)) {       \
		type *_p = _base->prev;           \
		func(_p);                         \
	}                                     \
}

/****************************************************************************/

static BOOL load_system_winscard_dll(void) {
	static const char *name_tbl[] = {
#define _E(type,name,args) #name,
#include "entries.h"
#undef _E
		NULL
	};

	TCHAR path[MAX_PATH];
	FARPROC *funcs = (FARPROC *)&g_sys_api;
	int i = 0;

	GetSystemDirectory(path, _ARRAYSIZE(path));
	PathAppend(path, _T("WinSCard.dll"));

	g_sys_api.instance = LoadLibrary(path);
	if (!g_sys_api.instance) {
		dprintf("%" PRIt " LoadLibrary failed\n", path);
		return FALSE;
	}

	DEBUGLOG(("%" PRIt " loaded\n", path));

	while (name_tbl[i]) {
		funcs[i] = GetProcAddress(g_sys_api.instance, name_tbl[i]);
		assert(funcs[i]);
		if (!funcs[i]) {
			dprintf("%" PRIt "::%s GetProcAddress failed\n", path, name_tbl[i]);
			FreeLibrary(g_sys_api.instance);
			g_sys_api.instance = 0;
			return FALSE;
		}
		i++;
	}

	return TRUE;
}

/**
 * @brief scard_stuff_construct
 *
 *
 */
BOOL scard_stuff_construct(void) {
	node_init(&s_memchain);
	node_init(&s_context);
	node_init(&s_reader);
	node_init(&s_reader_pool);
	node_init(&s_dllchain);
	node_init(&s_hookchain);
	node_init(&s_handle_free);
	s_handle_buf = NULL;
	s_handle_buf_count = 0;
	InitializeCriticalSection(&s_section);

	return load_system_winscard_dll();
}

/**
 * @brief scard_stuff_destruct
 *
 *
 */
void scard_stuff_destruct(void) {
	EnterCriticalSection(&s_section);

	RELEASE_CHAIN(context_t , &s_context    , release_context_impl);
	RELEASE_CHAIN(reader_t  , &s_reader_pool, release_reader);
	RELEASE_CHAIN(reader_t  , &s_reader     , release_reader);
	RELEASE_CHAIN(dllchain_t, &s_dllchain   , unregister_dll);
	RELEASE_CHAIN(memchain_t, &s_memchain   , release_node);

	free(s_handle_buf);
	s_handle_buf = NULL;
	s_handle_buf_count = 0;

	LeaveCriticalSection(&s_section);

	DeleteCriticalSection(&s_section);

	FreeLibrary(g_sys_api.instance);
}

/****************************************************************************/
enum htype_t {
	htype_SCARDCONTEXT,
	htype_SCARDHANDLE,
};

struct handle_entity_t {
	/* context_t::handlechain か s_handle_free のどちらかに必ずリンクする */
	struct handle_entity_t *next;
	struct handle_entity_t *prev;

	void *ptr;
	enum htype_t type;
	unsigned index;
	unsigned serial;

	char pad[4];
};

static void extend_handle_buf(int n) {
	struct handle_entity_t *p, *term;
	struct handle_entity_t **pp;
	unsigned index = s_handle_buf_count;

	s_handle_buf_count += n;
	s_handle_buf = pp = realloc(s_handle_buf, sizeof(*pp) * s_handle_buf_count);
	assert(pp);

	p = scmalloc(0, sizeof(*p) * n);
	assert(p);
	term = p + n;

	while (p < term) {
		pp[index] = p;

		p->ptr = NULL;
		p->index = index;
		p->serial = 0;
		list_push(&s_handle_free, p);

		index += 1;
		p += 1;
	}
}

static struct handle_entity_t *new_handle_entity(void) {
	struct handle_entity_t *p;

	EnterCriticalSection(&s_section);

	p = list_shift(&s_handle_free);

	if (!p) {
		extend_handle_buf(8);
		p = list_shift(&s_handle_free);
	}

	LeaveCriticalSection(&s_section);

	return p;
}

#ifdef _M_X64
#define SERIAL_BITS 32
#else
#define SERIAL_BITS 16
#endif
#define SERIAL_MASK (((intptr_t)1 << SERIAL_BITS) - 1)

static struct handle_entity_t *handle_to_entity(intptr_t h) {
	struct handle_entity_t *p;
	unsigned index;
	unsigned serial;

	index = (unsigned)(h >> SERIAL_BITS);
	if (index >= s_handle_buf_count)
		return NULL;

	p = s_handle_buf[index];
	serial = h & SERIAL_MASK;
	if (p->serial != serial)
		return NULL;

	return p;
}

static intptr_t open_handle(void *ptr, enum htype_t type, struct linknode *owner) {
	struct handle_entity_t *p = new_handle_entity();
	intptr_t h;

	list_push(owner, p);

	p->ptr = ptr;
	p->type = type;
	p->serial += 1;

	h = (intptr_t)p->index << SERIAL_BITS;
	h |= p->serial & SERIAL_MASK;

	return h;
}

static void close_handle(intptr_t h) {
	struct handle_entity_t *p = handle_to_entity(h);
	if (!p)
		return;

	EnterCriticalSection(&s_section);

	node_cut(p);
	list_push(&s_handle_free, p);
	p->ptr = NULL;

	LeaveCriticalSection(&s_section);
}

static void *handle_to_ptr(intptr_t h, enum htype_t type) {
	struct handle_entity_t *p = handle_to_entity(h);
	if (!p)
		return NULL;
	return p->type == type ? p->ptr : NULL;
}

#undef SERIAL_BITS
#undef SERIAL_MASK

/****************************************************************************/

/* メンバ変数hookchainのアドレスをdllchain_t自身のアドレスへ変換する */
static __inline dllchain_t *hookchain_to_ptr(struct linknode *p) {
	char *const chainnode = (char *)p;
	char *const adjusted = chainnode - offsetof(dllchain_t, hookchain);
	return (dllchain_t *)adjusted;
}

/**
 * @brief register_dll
 *
 *
 */
BOOL register_dll(scard_dll_t *dll) {
	dllchain_t *p = calloc(1, sizeof(*p));
	assert(p);
	if (!p)
		return FALSE;

	p->dll = dll;

	list_push(&s_dllchain, p);

	p->transmit_hook = dll->get_transmit_hook(dll);
	if (p->transmit_hook)
		list_push(&s_hookchain, &p->hookchain);

	return TRUE;
}

static void unregister_dll(dllchain_t *p) {
	scard_dll_t *dll = p->dll;
	dll->release(dll);
	release_node(p);
}

/**
 * @brief scard_hub_ready
 *
 *
 */
void scard_hub_ready(HINSTANCE hHubInstance) {
	dllchain_t *const base = (dllchain_t *)&s_dllchain;
	dllchain_t *p = base->next;

	while (p != base) {
		p->dll->scard_hub_ready(p->dll, hHubInstance);
		p = p->next;
	}
}

/**
 * @brief tryhook
 *
 *
 */
BOOL tryhook(LPCBYTE pbSendBuffer, DWORD cbSendLength, LPBYTE pbRecvBuffer, LPDWORD pcbRecvLength, scard_dll_t *exclude) {
	struct linknode *const base = &s_hookchain;
	struct linknode *p = base->next;

	while (p != base) {
		dllchain_t *x = hookchain_to_ptr(p);

		if (x->dll == exclude) {
			p = p->next;
			continue;
		}

		if (x->transmit_hook(pbSendBuffer, cbSendLength, pbRecvBuffer, pcbRecvLength)) {
			DEBUGLOG(("[transmit_hook] %" PRIt "\n", x->dll->dllpath(x->dll)));
			return TRUE;
		}

		p = p->next;
	}

	return FALSE;
}

/****************************************************************************/

/**
 * @brief scmalloc
 *
 *
 */
void *scmalloc(SCARDCONTEXT hContext, size_t size) {
	memchain_t *p;
	struct linknode *base;

	p = malloc(size + sizeof(*p));
	assert(p);
	if (!p) {
#if 0
		DEBUGLOG(("scmalloc %p 0x%x failed\n", hContext, size));
#endif
		return NULL;
	}

#if 0
	DEBUGLOG(("scmalloc %p 0x%x %p\n", hContext, size, p + 1));
#endif

	if (hContext) {
		context_t *ctx = handle_to_ptr((intptr_t)hContext, htype_SCARDCONTEXT);
		assert(ctx);
		base = &ctx->memchain;
	}
	else
		base = &s_memchain;

	EnterCriticalSection(&s_section);
	list_push(base, p);
	LeaveCriticalSection(&s_section);

	return p + 1;
}

/**
 * @brief scfree
 *
 *
 */
BOOL scfree(SCARDCONTEXT hContext, void *ptr) {
	memchain_t *base;
	BOOL succeeded = FALSE;

	if (hContext) {
		context_t *ctx = handle_to_ptr((intptr_t)hContext, htype_SCARDCONTEXT);
		assert(ctx);
		base = (memchain_t *)&ctx->memchain;
	}
	else
		base = (memchain_t *)&s_memchain;

	EnterCriticalSection(&s_section);
	{
		memchain_t *const p = (memchain_t *)ptr - 1;
		memchain_t *x = base->next;

		while (x != base) {
			if (x == p) {
				release_node(p);
				succeeded = TRUE;
				break;
			}
			x = x->next;
		}
	}
	LeaveCriticalSection(&s_section);

#if 0
	if (succeeded)
		DEBUGLOG(("scfree(%p, %p)\n", hContext, ptr));
	else
		DEBUGLOG(("scfree(%p, %p) unknown ptr\n", hContext, ptr));
#endif

	return succeeded;
}

/****************************************************************************/

static BOOL is_reader_registered(LPCSTR szRealReaders, LPCSTR testee) {
	/* 実カードリーダはs_readerの状態に関係なく登録済み扱い */
	if (szRealReaders) {
		LPCSTR p = szRealReaders;
		while (*p) {
			if (strcmp(p, testee) == 0)
				return TRUE;
			while (*p)
				p++;
			p++;
		}
	}

	return find_readerA(&s_reader, testee) != NULL;
}

static wchar_t *mbs2wcsdup(const char *s) {
	wchar_t *dst;
	size_t chars;
	char locale_save[64];

	strcpy(locale_save, setlocale(LC_ALL, NULL));
	setlocale(LC_ALL, ".OCP");

	chars = mbstowcs(NULL, s, 0) + 1;
	dst = malloc(sizeof(*dst) * chars);
	assert(dst);
	if (dst)
		mbstowcs(dst, s, chars);

	setlocale(LC_ALL, locale_save);

	return dst;
}

static BOOL register_reader(LPCSTR szReader, struct scard_dll_t *dll, LPCSTR szRealReaders) {
	reader_t *p;
	LPSTR alias = NULL;

	/* リーダ名が重複するときは別名(alias)をつける */
	/* 実カードリーダ(!dll)はチェック対象外 */
	if (dll && is_reader_registered(szRealReaders, szReader)) {
		int suffixnum = 2;

		alias = malloc(strlen(szReader) + 8);
		assert(alias);

		if (!alias)
			return FALSE;

		do {
			sprintf(alias, "%s #%d", szReader, suffixnum++);
		}
		while (is_reader_registered(szRealReaders, alias));
	}

	/* 退避してあるリーダの中から指名のリーダを探す */
	p = find_readerA(&s_reader_pool, alias ? alias : szReader);

	/* 有れば再利用 */
	if (p) {
		EnterCriticalSection(&s_section);
		node_cut(p);
		list_push(&s_reader, p);
		LeaveCriticalSection(&s_section);
		return TRUE;
	}

	/* 新規に生成 */
	p = malloc(sizeof(*p));
	assert(p);
	if (!p) {
		free(alias);
		return FALSE;
	}

	p->dll = dll;
	p->transmitter = NULL;
	p->realname = _strdup(szReader);
	assert(p->realname);

	p->readerA = alias ? alias : p->realname;

	p->readerW = mbs2wcsdup(p->readerA);
	assert(p->readerW);

	if (alias)
		DEBUGLOG(("[%s] real=<%s> alias=<%s>\n", __FUNCTION__, p->realname, alias));
	else
		DEBUGLOG(("[%s] <%s>\n", __FUNCTION__, p->realname));

	EnterCriticalSection(&s_section);
	list_push(&s_reader, p);
	LeaveCriticalSection(&s_section);

	return TRUE;
}

static void release_reader(reader_t *p) {
	scard_trans_t *tr;

	tr = p->transmitter;
	if (tr)
		tr->release(tr);
	free(p->readerA);
	free(p->readerW);

	if (p->realname != p->readerA)
		free(p->realname);

	release_node(p);
}

static BOOL register_readers(LPCSTR szReaders, struct scard_dll_t *dll, LPCSTR szRealReaders) {
	LPCSTR name = szReaders;

	while (*name) {
		if (!register_reader(name, dll, szRealReaders))
			return FALSE;

		while (*name)
			name++;
		name++;
	}

	return TRUE;
}

/**
 * @brief register_dll_readers
 *
 *
 */
BOOL register_dll_readers(LPCSTR szRealReaders) {
	dllchain_t *const base = (dllchain_t *)&s_dllchain;
	dllchain_t *p;

	p = base->next;
	while (p != base) {
		CHAR szReaders[MAX_READERS_LIST_CH];
		DWORD cchReaders;
		LONG result;

		cchReaders = _ARRAYSIZE(szReaders);
		result = p->dll->listReaders(p->dll, szReaders, &cchReaders);
		if (result == SCARD_E_NO_READERS_AVAILABLE) {
			p = p->next;
			continue;
		}

		assert(result == SCARD_S_SUCCESS);
		if (result != SCARD_S_SUCCESS)
			return FALSE;

		if (!register_readers(szReaders, p->dll, szRealReaders))
			return FALSE;

		p = p->next;
	}

	return TRUE;
}

/**
 * @brief register_real_readers
 *
 *
 */
BOOL register_real_readers(LPCSTR szRealReaders) {
	return register_readers(szRealReaders, NULL, NULL);
}

/**
 * @brief shunt_readers
 *
 *
 */
void shunt_readers(void) {
	reader_t *const base = (reader_t *)&s_reader;
	reader_t *const pool = (reader_t *)&s_reader_pool;

	/* baseの全要素をpoolへ移動 */
	EnterCriticalSection(&s_section);
	list_move(pool, base);
	LeaveCriticalSection(&s_section);
}

/**
 * @brief list_readersA
 *
 *
 */
BOOL list_readersA(DWORD *pcchReaders, LPSTR *pszReaders) {
	const reader_t *const base = (reader_t *)&s_reader;
	const reader_t *p;
	DWORD cchReaders = 1; /* 1 = 文字列群終端 */
	LPSTR szReaders, dst;

	EnterCriticalSection(&s_section);

	assert(pcchReaders);

	p = base->next;
	while (p != base) {
		cchReaders += (DWORD)strlen(p->readerA) + 1;
		p = p->next;
	}
	*pcchReaders = cchReaders;

	if (pszReaders) {
		dst = szReaders = malloc(sizeof(CHAR) * cchReaders);
		*pszReaders = szReaders;

		assert(dst);
		if (!dst) {
			LeaveCriticalSection(&s_section);
			return FALSE;
		}

		p = base->next;
		while (p != base) {
			size_t l = strlen(p->readerA) + 1;
			DEBUGLOG(("[%s] <%s> %Iu\n", __FUNCTION__, p->readerA, l));
			strcpy(dst, p->readerA);
			dst += l;
			p = p->next;
		}
		*dst = '\0';
	}

	LeaveCriticalSection(&s_section);

	return TRUE;
}

/**
 * @brief list_readersW
 *
 *
 */
BOOL list_readersW(DWORD *pcchReaders, LPWSTR *pszReaders) {
	const reader_t *const base = (reader_t *)&s_reader;
	const reader_t *p;
	DWORD cchReaders = 1; /* 1 = 文字列群終端 */
	LPWSTR szReaders, dst;

	EnterCriticalSection(&s_section);

	assert(pcchReaders);

	p = base->next;
	while (p != base) {
		cchReaders += (DWORD)wcslen(p->readerW) + 1;
		p = p->next;
	}
	*pcchReaders = cchReaders;

	if (pszReaders) {
		dst = szReaders = malloc(sizeof(WCHAR) * cchReaders);
		*pszReaders = szReaders;

		assert(dst);
		if (!dst) {
			LeaveCriticalSection(&s_section);
			return FALSE;
		}

		p = base->next;
		while (p != base) {
			size_t l = wcslen(p->readerW) + 1;
			DEBUGLOG(("[%s] <%s> %Iu\n", __FUNCTION__, p->readerA, l));
			wcscpy(dst, p->readerW);
			dst += l;
			p = p->next;
		}
		*dst = L'\0';
	}

	LeaveCriticalSection(&s_section);

	return TRUE;
}

static reader_t *find_readerA(struct linknode *list, LPCSTR szReader) {
	const reader_t *const base = (reader_t *)list;
	reader_t *p;

	EnterCriticalSection(&s_section);
	{
		p = base->next;
		while (p != base) {
			if (strcmp(p->readerA, szReader) == 0)
				break;
			p = p->next;
		}
	}
	LeaveCriticalSection(&s_section);

	return (p == base) ? NULL : p;
}

static reader_t *find_readerW(struct linknode *list, LPCWSTR szReader) {
	const reader_t *const base = (reader_t *)list;
	reader_t *p;

	EnterCriticalSection(&s_section);
	{
		p = base->next;
		while (p != base) {
			if (wcscmp(p->readerW, szReader) == 0)
				break;
			p = p->next;
		}
	}
	LeaveCriticalSection(&s_section);

	return (p == base) ? NULL : p;
}

/****************************************************************************/

/**
 * @brief is_fakereaderA
 *
 *
 */
BOOL is_fakereaderA(LPCSTR szReader) {
	reader_t *p = find_readerA(&s_reader, szReader);
	return (p && p->dll);
}

/**
 * @brief is_fakereaderW
 *
 *
 */
BOOL is_fakereaderW(LPCWSTR szReader) {
	reader_t *p = find_readerW(&s_reader, szReader);
	return (p && p->dll);
}

/****************************************************************************/

/**
 * @brief establish_context
 *
 *
 */
SCARDCONTEXT establish_context(SCARDCONTEXT hRealContext) {
	context_t *p = scmalloc(0, sizeof(*p));

	if (!p)
		return 0;

	node_init(&p->handlechain);
	node_init(&p->cardchain);
	node_init(&p->memchain);

	p->hRealContext = hRealContext;
	p->hContext = (SCARDCONTEXT)open_handle(p, htype_SCARDCONTEXT, &p->handlechain);

	EnterCriticalSection(&s_section);

	list_push(&s_context, p);

	LeaveCriticalSection(&s_section);

	return p->hContext;
}

static void release_context_impl(context_t *ctx) {
	EnterCriticalSection(&s_section);

	RELEASE_CHAIN(card_t, &ctx->cardchain, release_card_impl);
	RELEASE_CHAIN(memchain_t, &ctx->memchain, release_node);
	close_handle(ctx->hContext);

	assert(is_list_empty(&ctx->handlechain));

	node_cut(ctx);
	scfree(0, ctx);

	LeaveCriticalSection(&s_section);
}

/**
 * @brief validate_context
 *
 *
 */
BOOL validate_context(SCARDCONTEXT hContext, LPSCARDCONTEXT phRealContext) {
	context_t *p = handle_to_ptr((intptr_t)hContext, htype_SCARDCONTEXT);
	if (!p)
		return FALSE;

	if (phRealContext)
		*phRealContext = p->hRealContext;

	return TRUE;
}

/**
 * @brief release_context
 *
 *
 */
LONG release_context(SCARDCONTEXT hContext) {
	LONG result;
	context_t *p = handle_to_ptr((intptr_t)hContext, htype_SCARDCONTEXT);

	assert(p);

	if (p->hRealContext) {
		result = g_sys_api.SCardReleaseContext(p->hRealContext);
		if (result != SCARD_S_SUCCESS)
			return result;
	}

	release_context_impl(p);

	return SCARD_S_SUCCESS;
}

/****************************************************************************/

static LONG card_connect(SCARDCONTEXT hContext, reader_t *reader, DWORD dwShareMode, DWORD dwPreferredProtocols, LPSCARDHANDLE phCard, LPDWORD pdwActiveProtocol) {
	LONG result;
	context_t *ctx;
	card_t *p;

	assert(reader);

	ctx = handle_to_ptr((intptr_t)hContext, htype_SCARDCONTEXT);
	assert(ctx);

	p = scmalloc(hContext, sizeof(*p));
	if (!p)
		return SCARD_E_NO_MEMORY;

	memset(p, 0, sizeof(*p));

	p->hContext = hContext;
	p->dll = reader->dll;
	p->reader = reader;

	if (reader->dll) {
		if (!reader->transmitter)
			reader->transmitter = reader->dll->connect(reader->dll, reader->realname);

		p->transmitter = reader->transmitter;
		if (!p->transmitter) {
			scfree(hContext, p);
			return SCARD_E_NO_MEMORY;
		}
		p->transmitter->addref(p->transmitter);

		*pdwActiveProtocol = SCARD_PROTOCOL_T1;
		result = SCARD_S_SUCCESS;
	}
	else
		result = g_sys_api.SCardConnectA(ctx->hRealContext, reader->realname, dwShareMode, dwPreferredProtocols, &p->hRealCard, pdwActiveProtocol);

	if (result == SCARD_S_SUCCESS) {
		*phCard = p->hCard = (SCARDHANDLE)open_handle(p, htype_SCARDHANDLE, &ctx->handlechain);
		list_push(&ctx->cardchain, p);
	}

	return result;
}

static void release_card_impl(card_t *p) {
	reader_t *r = p->reader;

	if (r->transmitter) {
		if (r->transmitter->release(r->transmitter) == NULL)
			r->transmitter = NULL;
	}

	EnterCriticalSection(&s_section);
	node_cut(p);
	LeaveCriticalSection(&s_section);

	close_handle((intptr_t)p->hCard);
	scfree(p->hContext, p);
}

/**
 * @brief card_connectA
 *
 *
 */
LONG card_connectA(SCARDCONTEXT hContext, LPCSTR szReader, DWORD dwShareMode, DWORD dwPreferredProtocols, LPSCARDHANDLE phCard, LPDWORD pdwActiveProtocol) {
	reader_t *reader = find_readerA(&s_reader, szReader);

	if (!reader)
		return SCARD_E_UNKNOWN_READER;

	return card_connect(hContext, reader, dwShareMode, dwPreferredProtocols, phCard, pdwActiveProtocol);
}

/**
 * @brief card_connectW
 *
 *
 */
LONG card_connectW(SCARDCONTEXT hContext, LPCWSTR szReader, DWORD dwShareMode, DWORD dwPreferredProtocols, LPSCARDHANDLE phCard, LPDWORD pdwActiveProtocol) {
	reader_t *reader = find_readerW(&s_reader, szReader);

	if (!reader)
		return SCARD_E_UNKNOWN_READER;

	return card_connect(hContext, reader, dwShareMode, dwPreferredProtocols, phCard, pdwActiveProtocol);
}

/**
 * @brief card_disconnect
 *
 *
 */
LONG card_disconnect(SCARDHANDLE hCard, DWORD dwDisposition) {
	LONG result;
	card_t *p;

	p = handle_to_ptr((intptr_t)hCard, htype_SCARDHANDLE);
	if (!p)
		return ERROR_INVALID_HANDLE;

	if (p->hRealCard)
		result = g_sys_api.SCardDisconnect(p->hRealCard, dwDisposition);
	else
		result = SCARD_S_SUCCESS;

	if (result == SCARD_S_SUCCESS || result == ERROR_INVALID_HANDLE)
		release_card_impl(p);

	return result;
}

/**
 * @brief get_card_context
 *
 *
 */
SCARDCONTEXT get_card_context(SCARDHANDLE hCard) {
	card_t *p = handle_to_ptr((intptr_t)hCard, htype_SCARDHANDLE);
	assert(p);
	return p->hContext;
}

/**
 * @brief get_card_readerA
 *
 *
 */
LPCSTR get_card_readerA(SCARDHANDLE hCard) {
	card_t *p = handle_to_ptr((intptr_t)hCard, htype_SCARDHANDLE);
	return p ? p->reader->readerA : "?";
}

/**
 * @brief get_card_readerW
 *
 *
 */
LPCWSTR get_card_readerW(SCARDHANDLE hCard) {
	card_t *p = handle_to_ptr((intptr_t)hCard, htype_SCARDHANDLE);
	return p ? p->reader->readerW : L"?";
}

/**
 * @brief get_card_dll
 *
 *
 */
struct scard_dll_t *get_card_dll(SCARDHANDLE hCard) {
	card_t *p = handle_to_ptr((intptr_t)hCard, htype_SCARDHANDLE);
	assert(p);
	return p->dll;
}

/**
 * @brief get_card_trans
 *
 *
 */
struct scard_trans_t *get_card_trans(SCARDHANDLE hCard) {
	card_t *p = handle_to_ptr((intptr_t)hCard, htype_SCARDHANDLE);
	assert(p);
	return p->transmitter;
}

/**
 * @brief validate_card
 *
 *
 */
BOOL validate_card(SCARDHANDLE hCard, LPSCARDHANDLE phRealCard) {
	card_t *p = handle_to_ptr((intptr_t)hCard, htype_SCARDHANDLE);
	if (!p)
		return FALSE;

	if (phRealCard)
		*phRealCard = p->hRealCard;

	return TRUE;
}

