#include "stdafx.h"
#include "net_server.h"

XRNETSERVER_API ClientID BroadcastCID(0xffffffff);

IClient::IClient()
{
	flags.bLocal		= FALSE;
	flags.bConnected	= FALSE;
}

IClient*	IPureServer::ID_to_client		(ClientID ID)
{
	if ( 0 == ID.value() )			return NULL;
	csPlayers.Enter	();

	for ( u32 client = 0; client < net_Players.size(); ++client )
	{
		if ( net_Players[client]->ID == ID )
		{
			csPlayers.Leave();
			return net_Players[client];
		}
	}
	csPlayers.Leave();
	return NULL;
}

IPureServer::IPureServer	()
#ifdef PROFILE_CRITICAL_SECTIONS
	:	csPlayers(MUTEX_PROFILE_ID(IPureServer::csPlayers))
#endif
{
	SV_Client				= NULL;
}

IPureServer::EConnect IPureServer::Connect(LPCSTR options)
{
	connect_options			= options;
	return					ErrNoError;
}

void	IPureServer::SendTo		(ClientID ID, NET_Packet& P)
{
	SendTo_LL( ID, P.B.data, P.B.count );
}

void	IPureServer::SendBroadcast(ClientID exclude, NET_Packet& P)
{
	csPlayers.Enter();

	for( u32 i=0; i<net_Players.size(); i++ )
	{
		IClient* player = net_Players[i];

		if( player->ID == exclude )     continue;
		if( !player->flags.bConnected ) continue;

		SendTo_LL( player->ID, P.B.data, P.B.count );
	}

	csPlayers.Leave	();
}
