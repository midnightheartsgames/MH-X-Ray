#pragma once

#include "net_shared.h"

class XRNETSERVER_API INetQueue
{
	xrCriticalSection		cs;
	xr_deque<NET_Packet*>	ready;
	xr_vector<NET_Packet*>	unused;
public:
	INetQueue();
	~INetQueue();

	NET_Packet*			CreateGet	();
	void				CreateCommit(NET_Packet*);

	NET_Packet*			Retreive();
	void				Release	();
};

class XRNETSERVER_API
IPureClient
{
protected:
	CTimer*					device_timer;
	INetQueue				net_Queue;
	u32						net_Time_LastUpdate;
	s32						net_TimeDelta_User;

public:
	IPureClient				(CTimer* tm);
	virtual ~IPureClient	()	{}

	IC virtual	NET_Packet*	net_msg_Retreive		()	{ return net_Queue.Retreive();	}
	IC void					net_msg_Release			()	{ net_Queue.Release();			}

	virtual void			OnMessage				(void* data, u32 size);
	BOOL					net_HasBandwidth		();

	IC u32					timeServer				()	{ return TimeGlobal(device_timer) + net_TimeDelta_User; }
	IC u32					timeServer_Async		()	{ return TimeGlobal(device_timer) + net_TimeDelta_User; }
	IC void					timeServer_UserDelta	(s32 d)						{ net_TimeDelta_User=d;	}
};
