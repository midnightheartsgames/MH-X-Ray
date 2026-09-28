#include "stdafx.h"
#include "net_client.h"

INetQueue::INetQueue()
#ifdef PROFILE_CRITICAL_SECTIONS
	:cs(MUTEX_PROFILE_ID(INetQueue))
#endif
{
	unused.reserve	(128);
	for (int i=0; i<16; i++)
		unused.push_back	(xr_new<NET_Packet>());
}

INetQueue::~INetQueue()
{
	cs.Enter		();
	u32				it;
	for				(it=0; it<unused.size(); it++)	xr_delete(unused[it]);
	for				(it=0; it<ready.size(); it++)	xr_delete(ready[it]);
	cs.Leave		();
}

static u32 LastTimeCreate = 0;

void INetQueue::CreateCommit(NET_Packet* P)
{
	cs.Enter		();
	ready.push_back	(P);
	cs.Leave		();
}

NET_Packet*		INetQueue::CreateGet()
{
	NET_Packet*	P			= 0;
	cs.Enter		();

	if (unused.empty())
	{
		P					= xr_new<NET_Packet> ();
		LastTimeCreate		= GetTickCount();
	} else
	{
		P					= unused.back();
		unused.pop_back		();
	}
	cs.Leave		();
	return	P;
}

NET_Packet*		INetQueue::Retreive	()
{
	NET_Packet*	P			= 0;
	cs.Enter		();
	if (!ready.empty())		P = ready.front();
	else
	{
		u32 tmp_time = GetTickCount()-60000;
		u32 size = unused.size();
		if ((LastTimeCreate < tmp_time) &&  (size > 32))
		{
			xr_delete(unused.back());
			unused.pop_back();
		}
	}
	cs.Leave		();
	return	P;
}

void			INetQueue::Release	()
{
	cs.Enter		();
	VERIFY			(!ready.empty());
	u32 tmp_time = GetTickCount()-60000;
	u32 size = unused.size();
	if ((LastTimeCreate < tmp_time) &&  (size > 32))
	{
		xr_delete(ready.front());
	}
	else
		unused.push_back(ready.front());
	ready.pop_front	();
	cs.Leave		();
}

XRNETSERVER_API Flags32	psNET_Flags			= {0};

static const u32		update_interval_ms				= 1000/30;
static const u32		minimized_update_interval_ms	= 1000;

IPureClient::IPureClient	(CTimer* timer)
{
	device_timer			= timer;
	net_Time_LastUpdate		= 0;
	net_TimeDelta_User		= 0;
}

void	IPureClient::OnMessage(void* data, u32 size)
{
	NET_Packet* P = net_Queue.CreateGet();

	P->construct			(data, size);
	P->timeReceive			= timeServer_Async();

	u16						tmp_type;
	P->r_begin				(tmp_type);
	net_Queue.CreateCommit	(P);
}

BOOL	IPureClient::net_HasBandwidth	()
{
	u32		dwTime		= TimeGlobal(device_timer);
	u32		dwInterval	= psNET_Flags.test(NETFLAG_MINIMIZEUPDATES) ? minimized_update_interval_ms : update_interval_ms;

	if ((dwTime-net_Time_LastUpdate) <= dwInterval)
		return				FALSE;

	net_Time_LastUpdate		= dwTime;
	return					TRUE;
}
