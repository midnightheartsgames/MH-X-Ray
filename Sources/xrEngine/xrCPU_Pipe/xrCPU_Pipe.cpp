#include "stdafx.h"
#pragma hdrstop

#pragma comment(lib,"xrEngine")

BOOL APIENTRY DllMain( HANDLE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
					 )
{
    return TRUE;
}

extern xrSkin1W			xrSkin1W_x86;
extern xrSkin2W			xrSkin2W_x86;

extern "C" {
	__declspec(dllexport) void	__cdecl	xrBind_PSGP	(xrDispatchTable* T)
	{
		T->skin1W	= xrSkin1W_x86;
		T->skin2W	= xrSkin2W_x86;
	}
};
