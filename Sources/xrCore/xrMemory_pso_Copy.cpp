#include "stdafx.h"
#pragma hdrstop

void	__stdcall xrMemCopy_x86					(LPVOID dest, const void* src, u32 n)
{
	memcpy		(dest,src,n);
}
