#include "stdafx.h"
#pragma hdrstop

void	__stdcall	xrMemFill32_x86		(LPVOID dest, u32 value,  u32 count)
{
	std::fill_n	(static_cast<u32*>(dest), count, value);
}
