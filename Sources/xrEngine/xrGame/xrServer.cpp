// xrServer.cpp: implementation of the xrServer class.
//
//////////////////////////////////////////////////////////////////////

#include "pch_script.h"
#include "xrServer.h"
#include "xrMessages.h"
#include "xrServer_Objects_ALife_All.h"
#include "level.h"
#include "game_cl_base.h"
#include "ai_space.h"
#include "../IGame_Persistent.h"

#include "../XR_IOConsole.h"

#pragma warning(push)
#pragma warning(disable:4995)
#include <malloc.h>
#pragma warning(pop)

xrClientData::xrClientData	()
{
	ps					= Level().Server->game->createPlayerState();
	ps->clear			();
	ps->m_online_time	= Level().timeServer();

	Clear				();
}

void	xrClientData::Clear()
{
	owner									= NULL;
	net_Ready								= FALSE;
	net_Accepted							= FALSE;
	net_PassUpdates							= TRUE;
	m_admin_rights.m_has_admin_rights		= FALSE;
};

xrClientData::~xrClientData()
{
	xr_delete(ps);
}


xrServer::xrServer()
{
	m_aDelayedPackets.clear();
}

xrServer::~xrServer()
{
	while (net_Players.size())
	{
		client_Destroy(net_Players[0]);
	}
	m_aDelayedPackets.clear();
}

//--------------------------------------------------------------------

CSE_Abstract*	xrServer::ID_to_entity		(u16 ID)
{
	// #pragma todo("??? to all : ID_to_entity - must be replaced to 'game->entity_from_eid()'")	
	if (0xffff==ID)				return 0;
	xrS_entities::iterator	I	= entities.find	(ID);
	if (entities.end()!=I)		return I->second;
	else						return 0;
}

//--------------------------------------------------------------------
IClient*	xrServer::client_Create		()
{
	return xr_new<xrClientData> ();
}

void		xrServer::client_Destroy	(IClient* C)
{
	csPlayers.Enter	();

	for (u32 I=0; I<net_Players.size(); I++)
	{
		if (net_Players[I] == C)
		{
			CSE_Abstract* pOwner	= ((xrClientData*)C)->owner;
			CSE_Spectator* pS		=	smart_cast<CSE_Spectator*>(pOwner);
			if (pS)
			{
				NET_Packet			P;
				P.w_begin			(M_EVENT);
				P.w_u32				(Level().timeServer());
				P.w_u16				(GE_DESTROY);
				P.w_u16				(pS->ID);
				SendBroadcast		(BroadcastCID,P);
			};

			{
				DelayedPacket pp;
				pp.SenderID = C->ID;

				xr_deque<DelayedPacket>::iterator it;
				do{
					it						=std::find(m_aDelayedPackets.begin(),m_aDelayedPackets.end(),pp);
					if(it!=m_aDelayedPackets.end())
					{
						m_aDelayedPackets.erase	(it);
						Msg("removing packet from delayed event storage");
					}else
						break;
				}while(true);
			}

			xr_delete			(C);
			net_Players.erase	(net_Players.begin()+I);
			break;
		};
	}

	csPlayers.Leave();
}

void xrServer::Update	()
{
	NET_Packet		Packet;
	csPlayers.Enter	();

	VERIFY						(verify_entities());

	ProceedDelayedPackets();
	game->ProcessDelayedEvent();
	game->Update	();

	u32 svT				= Device.TimerAsync();
	while (!(q_respawn.empty() || (svT<q_respawn.begin()->timestamp)))
	{
		svs_respawn	R		= *q_respawn.begin();
		q_respawn.erase		(q_respawn.begin());

		CSE_Abstract* E	= ID_to_entity(R.phantom);
		E->Spawn_Write		(Packet,FALSE);
		u16					ID;
		Packet.r_begin		(ID);
		R_ASSERT(M_SPAWN==ID);
		ClientID clientID; 
		clientID.set(0xffff);
		Process_spawn		(Packet,clientID);
	}


	SendUpdatesToAll();


	if (game->sv_force_sync)	Perform_game_export();

	VERIFY						(verify_entities());
	csPlayers.Leave					();
}

void xrServer::SendUpdatesToAll()
{
	for (u32 client=0; client<net_Players.size(); ++client)
	{
		xrClientData*	Client			= (xrClientData*) net_Players	[client];
		if (!Client->net_Ready)			continue;

		NET_Packet						Packet;
		Packet.w_begin					(M_UPDATE);
		game->net_Export_Update			(Packet,Client->ID,Client->ID);
		game->net_Export_GameTime		(Packet);
		SendTo							(Client->ID,Packet);
	}

	if (game->sv_force_sync)	Perform_game_export();

	VERIFY						(verify_entities());

}

xr_vector<shared_str>	_tmp_log;
void console_log_cb(LPCSTR text)
{
	_tmp_log.push_back	(text);
}

void xrServer::OnDelayedMessage	(NET_Packet& P, ClientID sender)
{
	if (g_pGameLevel && Level().IsDemoSave()) 
		Level().Demo_StoreServerData(P.B.data, P.B.count);
	u16						type;
	P.r_begin				(type);

	csPlayers.Enter			();

	VERIFY							(verify_entities());
	xrClientData* CL				= ID_to_client(sender);
	R_ASSERT2						(CL, make_string("packet type [%d]",type).c_str());

	switch (type)
	{
		case M_CLIENT_REQUEST_CONNECTION_DATA:
		{
			OnCL_Connected				(CL);
		}break;
		case M_REMOTE_CONTROL_CMD:
		{
			if(CL->m_admin_rights.m_has_admin_rights)
			{
				string1024			buff;
				P.r_stringZ			(buff);
				SetLogCB			(console_log_cb);
				_tmp_log.clear		();
				Console->Execute	(buff);
				SetLogCB			(NULL);

				NET_Packet			P_answ;			
				for(u32 i=0; i<_tmp_log.size(); ++i)
				{
					P_answ.w_begin		(M_REMOTE_CONTROL_CMD);
					P_answ.w_stringZ	(_tmp_log[i]);
					SendTo				(CL->ID,P_answ);
				}
			}else
			{
				NET_Packet			P_answ;			
				P_answ.w_begin		(M_REMOTE_CONTROL_CMD);
				P_answ.w_stringZ	("you dont have admin rights");
				SendTo				(CL->ID,P_answ);
			}
		}break;
	}
	VERIFY							(verify_entities());

	csPlayers.Leave					();
}

extern	float	g_fCatchObjectTime;
void xrServer::OnMessage	(NET_Packet& P, ClientID sender)
{
	if (g_pGameLevel && Level().IsDemoSave()) Level().Demo_StoreServerData(P.B.data, P.B.count);
	u16			type;
	P.r_begin	(type);

	csPlayers.Enter			();

	VERIFY							(verify_entities());
	xrClientData* CL				= ID_to_client(sender);

	switch (type)
	{
	case M_UPDATE:	
		{
			Process_update			(P,sender);
			VERIFY					(verify_entities());
		}break;
	case M_SPAWN:	
		{
			if (CL->flags.bLocal)
				Process_spawn		(P,sender);	

			VERIFY					(verify_entities());
		}break;
	case M_EVENT:	
		{
			Process_event			(P,sender);
			VERIFY					(verify_entities());
		}break;
	case M_EVENT_PACK:
		{
			NET_Packet	tmpP;
			while (!P.r_eof())
			{
				tmpP.B.count		= P.r_u8();
				P.r					(&tmpP.B.data, tmpP.B.count);

				OnMessage			(tmpP, sender);
			};			
		}break;
	case M_CL_UPDATE:
		{
			xrClientData* CL		= ID_to_client	(sender);
			if (!CL)				break;
			CL->net_Ready			= TRUE;

			if (!CL->net_PassUpdates)
				break;
			if (SV_Client)
				SendTo	(SV_Client->ID, P);
			VERIFY					(verify_entities());
		}break;
	case M_MOVE_PLAYERS_RESPOND:
		{
			xrClientData* CL		= ID_to_client	(sender);
			if (!CL)				break;
			CL->net_Ready			= TRUE;
			CL->net_PassUpdates		= TRUE;
		}break;
	//-------------------------------------------------------------------
	case M_CL_INPUT:
		{
			xrClientData* CL		= ID_to_client	(sender);
			if (CL)	CL->net_Ready	= TRUE;
			if (SV_Client) SendTo	(SV_Client->ID, P);
			VERIFY					(verify_entities());
		}break;
	case M_GAMEMESSAGE:
		{
			SendBroadcast			(BroadcastCID,P);
			VERIFY					(verify_entities());
		}break;
	case M_CLIENTREADY:
		{
			xrClientData* CL		= ID_to_client(sender);
			if ( CL )	
			{
				CL->net_Ready	= TRUE;
				CL->ps->DeathTime = Device.dwTimeGlobal;
				game->OnPlayerConnectFinished(sender);
				CL->ps->setName( CL->name.c_str() );
				
			};
			game->signal_Syncronize	();
			VERIFY					(verify_entities());
		}break;
	case M_SWITCH_DISTANCE:
		{
			game->switch_distance	(P,sender);
			VERIFY					(verify_entities());
		}break;
	case M_CHANGE_LEVEL:
		{
			if (game->change_level(P,sender))
			{
				SendBroadcast		(BroadcastCID,P);
			}
			VERIFY					(verify_entities());
		}break;
	case M_SAVE_GAME:
		{
			game->save_game			(P,sender);
			VERIFY					(verify_entities());
		}break;
	case M_LOAD_GAME:
		{
			game->load_game			(P,sender);
			SendBroadcast			(BroadcastCID,P);
			VERIFY					(verify_entities());
		}break;
	case M_RELOAD_GAME:
		{
			SendBroadcast			(BroadcastCID,P);
			VERIFY					(verify_entities());
		}break;
	case M_SAVE_PACKET:
		{
			Process_save			(P,sender);
			VERIFY					(verify_entities());
		}break;
	case M_CLIENT_REQUEST_CONNECTION_DATA:
		{
			AddDelayedPacket(P, sender);
		}break;
	case M_CHAT_MESSAGE:
		{
			xrClientData *l_pC			= ID_to_client(sender);
			OnChatMessage				(&P, l_pC);
		}break;
	case M_CHANGE_LEVEL_GAME:
		{
			ClientID CID; CID.set		(0xffffffff);
			SendBroadcast				(CID,P);
		}break;
	case M_CL_AUTH:
		{
			game->AddDelayedEvent		(P,GAME_EVENT_PLAYER_AUTH, 0, sender);
		}break;
	case M_STATISTIC_UPDATE:
		{
			if (SV_Client)
				SendBroadcast			(SV_Client->ID,P);
			else
				SendBroadcast			(BroadcastCID,P);
		}break;
	case M_STATISTIC_UPDATE_RESPOND:
		{
			if (SV_Client) SendTo	(SV_Client->ID, P);
		}break;
	case M_PLAYER_FIRE:
		{
			if (game)
				game->OnPlayerFire(sender, P);
		}break;
	case M_REMOTE_CONTROL_AUTH:
		{
			string512				reason;
			shared_str				user;
			shared_str				pass;
			P.r_stringZ				(user);
			if(0==stricmp(user.c_str(),"logoff"))
			{
				CL->m_admin_rights.m_has_admin_rights	= FALSE;
				strcpy				(reason,"logged off");
				Msg("# Remote administrator logged off.");
			}else
			{
				P.r_stringZ				(pass);
				bool res = CheckAdminRights(user, pass, reason);
				if(res){
					CL->m_admin_rights.m_has_admin_rights	= TRUE;
					CL->m_admin_rights.m_dwLoginTime		= Device.dwTimeGlobal;
					Msg("# User [%s] logged as remote administrator.", user.c_str());
				}else
					Msg("# User [%s] tried to login as remote administrator. Access denied.", user.c_str());

			}
			NET_Packet			P_answ;			
			P_answ.w_begin		(M_REMOTE_CONTROL_CMD);
			P_answ.w_stringZ	(reason);
			SendTo				(CL->ID,P_answ);
		}break;

	case M_REMOTE_CONTROL_CMD:
		{
			AddDelayedPacket(P, sender);
		}break;
	}

	VERIFY							(verify_entities());

	csPlayers.Leave					();
}

bool xrServer::CheckAdminRights(const shared_str& user, const shared_str& pass, string512 reason)
{
	bool res			= false;
	string_path			fn;
	FS.update_path		(fn,"$app_data_root$","radmins.ltx");
	if(FS.exist(fn))
	{
		CInifile			ini(fn);
		if(ini.line_exist("radmins",user.c_str()))
		{
			if (ini.r_string ("radmins",user.c_str()) == pass)
			{
				strcpy			(reason,"Access permitted.");
				res				= true;
			}else
			{
				strcpy			(reason,"Access denied. Wrong password.");
			}
		}else
			strcpy			(reason,"Access denied. No such user.");
	}else
		strcpy				(reason,"Access denied.");

	return				res;
}

void xrServer::SendTo_LL			(ClientID ID, void* data, u32 size)
{
	if (SV_Client && SV_Client->ID==ID)
		Level().OnMessage			(data,size);
}

//--------------------------------------------------------------------
CSE_Abstract*	xrServer::entity_Create		(LPCSTR name)
{
	return F_entity_Create(name);
}

void			xrServer::entity_Destroy	(CSE_Abstract *&P)
{
#ifdef DEBUG
	Msg							("xrServer::entity_Destroy : [%d][%s][%s]",P->ID,P->name(),P->name_replace());
#endif
	R_ASSERT					(P);
	entities.erase				(P->ID);
	m_tID_Generator.vfFreeID	(P->ID,Device.TimerAsync());

	if(P->owner && P->owner->owner==P)
		P->owner->owner		= NULL;

	P->owner = NULL;
	if (!ai().get_alife() || !P->m_bALifeControl)
	{
		F_entity_Destroy		(P);
	}
}

CSE_Abstract*	xrServer::GetEntity			(u32 Num)
{
	xrS_entities::iterator	I=entities.begin(),E=entities.end();
	for (u32 C=0; I!=E; ++I,++C)
	{
		if (C == Num) return I->second;
	};
	return NULL;
};


void		xrServer::OnChatMessage(NET_Packet* P, xrClientData* CL)
{
//	string256 ChatMsg;
//	u16 PlayerID = P->r_u16();
	s16 team = P->r_s16();
//	P->r_stringZ(ChatMsg);
	if (!CL->net_Ready) return;
	game_PlayerState* Cps = CL->ps;
	for (u32 client=0; client<net_Players.size(); ++client)
	{
		// Initialize process and check for available bandwidth
		xrClientData*	Client		= (xrClientData*) net_Players	[client];
		game_PlayerState* ps = Client->ps;
		if (!Client->net_Ready) continue;
		if (team != 0 && ps->team != team) continue;
		if (Cps->testFlag(GAME_PLAYER_FLAG_VERY_VERY_DEAD) && !ps->testFlag(GAME_PLAYER_FLAG_VERY_VERY_DEAD))
			continue;
		SendTo(Client->ID, *P);
	};
};

#ifdef DEBUG

static	BOOL	_ve_initialized			= FALSE;
static	BOOL	_ve_use					= TRUE;

bool xrServer::verify_entities				() const
{
	if (!_ve_initialized)	{
		_ve_initialized					= TRUE;
		if (strstr(Core.Params,"-~ve"))	_ve_use=FALSE;
	}
	if (!_ve_use)						return true;

	xrS_entities::const_iterator		I = entities.begin();
	xrS_entities::const_iterator		E = entities.end();
	for ( ; I != E; ++I) {
		VERIFY2							((*I).first != 0xffff,"SERVER : Invalid entity id as a map key - 0xffff");
		VERIFY2							((*I).second,"SERVER : Null entity object in the map");
		VERIFY3							((*I).first == (*I).second->ID,"SERVER : ID mismatch - map key doesn't correspond to the real entity ID",(*I).second->name_replace());
		verify_entity					((*I).second);
	}
	return								(true);
}

void xrServer::verify_entity				(const CSE_Abstract *entity) const
{
	VERIFY(entity->m_wVersion!=0);
	if (entity->ID_Parent != 0xffff) {
		xrS_entities::const_iterator	J = entities.find(entity->ID_Parent);
		VERIFY3							(J != entities.end(),"SERVER : Cannot find parent in the map",entity->name_replace());
		VERIFY3							((*J).second,"SERVER : Null entity object in the map",entity->name_replace());
		VERIFY3							((*J).first == (*J).second->ID,"SERVER : ID mismatch - map key doesn't correspond to the real entity ID",(*J).second->name_replace());
		VERIFY3							(std::find((*J).second->children.begin(),(*J).second->children.end(),entity->ID) != (*J).second->children.end(),"SERVER : Parent/Children relationship mismatch - Object has parent, but corresponding parent doesn't have children",(*J).second->name_replace());
	}

	xr_vector<u16>::const_iterator		I = entity->children.begin();
	xr_vector<u16>::const_iterator		E = entity->children.end();
	for ( ; I != E; ++I) {
		VERIFY3							(*I != 0xffff,"SERVER : Invalid entity children id - 0xffff",entity->name_replace());
		xrS_entities::const_iterator	J = entities.find(*I);
		VERIFY3							(J != entities.end(),"SERVER : Cannot find children in the map",entity->name_replace());
		VERIFY3							((*J).second,"SERVER : Null entity object in the map",entity->name_replace());
		VERIFY3							((*J).first == (*J).second->ID,"SERVER : ID mismatch - map key doesn't correspond to the real entity ID",(*J).second->name_replace());
		VERIFY3							((*J).second->ID_Parent == entity->ID,"SERVER : Parent/Children relationship mismatch - Object has children, but children doesn't have parent",(*J).second->name_replace());
	}
}

#endif // DEBUG

shared_str xrServer::level_name				(const shared_str &server_options) const
{
	return								(game->level_name(server_options));
}

void xrServer::create_direct_client()
{
	SClientConnectData cl_data;
	cl_data.clientID.set(1);
	strcpy_s( cl_data.name, "single_player" );

	new_client( &cl_data );
}


void xrServer::ProceedDelayedPackets()
{
	DelayedPackestCS.Enter();
	while (!m_aDelayedPackets.empty())
	{
		DelayedPacket& DPacket	= *m_aDelayedPackets.begin();
		OnDelayedMessage(DPacket.Packet, DPacket.SenderID);
//		OnMessage(DPacket.Packet, DPacket.SenderID);
		m_aDelayedPackets.pop_front();
	}
	DelayedPackestCS.Leave();
};

void xrServer::AddDelayedPacket	(NET_Packet& Packet, ClientID Sender)
{
	DelayedPackestCS.Enter();

	m_aDelayedPackets.push_back(DelayedPacket());
	DelayedPacket* NewPacket = &(m_aDelayedPackets.back());
	NewPacket->SenderID = Sender;
	CopyMemory	(&(NewPacket->Packet),&Packet,sizeof(NET_Packet));	

	DelayedPackestCS.Leave();
}

