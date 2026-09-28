#pragma once

#include "net_utils.h"


#ifdef XR_NETSERVER_EXPORTS
	#define XRNETSERVER_API __declspec(dllexport)
#else
	#define XRNETSERVER_API __declspec(dllimport)
	#pragma comment(lib,	"xrNetServer"	)
#endif

XRNETSERVER_API extern ClientID BroadcastCID;

XRNETSERVER_API extern Flags32	psNET_Flags;

enum	{
	NETFLAG_MINIMIZEUPDATES		= (1<<0),
};

IC u32 TimeGlobal	(CTimer* timer)	{ return timer->GetElapsed_ms();	}
