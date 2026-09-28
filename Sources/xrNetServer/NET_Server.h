#pragma once

#include "net_shared.h"

struct SClientConnectData
{
	ClientID		clientID;
	string64		name;

	SClientConnectData()
	{
		name[0] = 0;
	}
};

class XRNETSERVER_API
IClient
{
public:
	struct Flags
	{
		u32		bLocal		: 1;
		u32		bConnected	: 1;
	};

						IClient();
	virtual				~IClient()	{}

	ClientID			ID;
	shared_str			name;
	Flags				flags;
};

IC bool operator== (IClient const* pClient, ClientID const& ID) { return pClient->ID == ID; }

class XRNETSERVER_API
IPureServer
{
public:
	enum EConnect
	{
		ErrConnect,
		ErrNoLevel,
		ErrMax,
		ErrNoError = ErrMax,
	};
protected:
	shared_str				connect_options;

	xrCriticalSection		csPlayers;
	xr_vector<IClient*>		net_Players;
	IClient*				SV_Client;

	IClient*				ID_to_client		(ClientID ID);
public:
							IPureServer			();
	virtual					~IPureServer		()	{}

	virtual EConnect		Connect				(LPCSTR session_name);

	virtual void			SendTo_LL			(ClientID ID, void* data, u32 size)	= 0;
	void					SendTo				(ClientID ID, NET_Packet& P);
	void					SendBroadcast		(ClientID exclude, NET_Packet& P);

	IC u32					client_Count		()			{ return net_Players.size(); }
	IC IClient*				client_Get			(u32 num)	{ return net_Players[num]; }

	IClient*				GetServerClient		()			{ return SV_Client; };

	const shared_str&		GetConnectOptions	() const {return connect_options;}
};
