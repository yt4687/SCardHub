#pragma once

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif

#define g_rgSCardT0Pci  _REMOVE_g_rgSCardT0Pci
#define g_rgSCardT1Pci  _REMOVE_g_rgSCardT1Pci
#define g_rgSCardRawPci _REMOVE_g_rgSCardRawPci

#include <winscard.h>

#undef g_rgSCardT0Pci
#undef g_rgSCardT1Pci
#undef g_rgSCardRawPci

extern SCARD_IO_REQUEST g_rgSCardT0Pci;
extern SCARD_IO_REQUEST g_rgSCardT1Pci;
extern SCARD_IO_REQUEST g_rgSCardRawPci;

#ifdef _MSC_VER
#pragma warning(pop)
#endif
