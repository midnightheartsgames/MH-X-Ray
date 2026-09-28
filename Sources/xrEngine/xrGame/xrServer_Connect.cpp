#include "stdafx.h"
#include "xrServer.h"
#include "game_sv_single.h"
#include "xrMessages.h"
#include "game_cl_single.h"
#include "MainMenu.h"

#pragma warning(push)
#pragma warning(disable:4995)
#include <malloc.h>
#pragma warning(pop)

xrServer::EConnect xrServer::Connect(shared_str &session_name)
{
#ifdef DEBUG
	Msg						("* sv_Connect: %s",	*session_name);
#endif

	// Parse options and create game
	if (0==strchr(*session_name,'/'))
		return				ErrConnect;

	string1024				options;
	R_ASSERT2(xr_strlen(session_name) <= sizeof(options), "session_name too BIIIGGG!!!");
	strcpy					(options,strchr(*session_name,'/')+1);
	
	// Parse game type
	string1024				type;
	R_ASSERT2(xr_strlen(options) <= sizeof(type), "session_name too BIIIGGG!!!");
	strcpy					(type,options);
	if (strchr(type,'/'))	*strchr(type,'/') = 0;
	game					= NULL;

	CLASS_ID clsid			= game_GameState::getCLASS_ID(type,true);
	game					= smart_cast<game_sv_GameState*> (NEW_INSTANCE(clsid));

	// Options
	if (0==game)			return ErrConnect;
	csPlayers.Enter			();
//	game->type				= type_id;
#ifdef DEBUG
	Msg("* Created server_game %s",game->type_name());
#endif

	game->Create			(session_name);
	csPlayers.Leave			();
	
	
	return IPureServer::Connect(*session_name);
}


IClient* xrServer::new_client( SClientConnectData* cl_data )
{
	IClient* CL		= client_Create();
	CL->ID			= cl_data->clientID;

	csPlayers.Enter			();
	net_Players.push_back	( CL );
	csPlayers.Leave			();

	string64 new_name;
	strcpy_s( new_name, cl_data->name );
	CL->name._set( new_name );
	
	if ( !HasProtected() && game->NewPlayerName_Exists( CL, new_name ) )
	{
		game->NewPlayerName_Generate( CL, new_name );
		game->NewPlayerName_Replace( CL, new_name );
	}
	CL->name._set( new_name );

	NET_Packet		P;
	P.B.count		= 0;
	P.r_pos			= 0;
	
	game->AddDelayedEvent( P, GAME_EVENT_CREATE_CLIENT, 0, CL->ID );
	if ( client_Count() == 1 )
	{
		Update();
	}
	return CL;
}

void xrServer::AttachNewClient			(IClient* CL)
{
	SV_Client			= CL;
	CL->flags.bLocal	= 1;

	if (!NeedToCheckClient_BuildVersion(CL))
		Check_BuildVersion_Success(CL);
}


