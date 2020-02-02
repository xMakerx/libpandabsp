#include "net/server.h"
#include "net/messages.h"
#include "game/basegame.h"

#include <configVariableInt.h>
#include <datagramIterator.h>
#include <clockObject.h>

static ConfigVariableInt server_port( "server_port", 27015 );
static ConfigVariableDouble sv_heartbeat_tolerance( "sv_heartbeat_tolerance", 20.0 );

Server *g_server = nullptr;

Server::Server() :
	_entnum_alloc( ENTID_MIN, ENTID_MAX ),
	_client_id_alloc( 0, 0xFFFF ),
	_listen_socket( k_HSteamListenSocket_Invalid ),
	_poll_group( k_HSteamNetPollGroup_Invalid ),
	_is_started( false )
{
	g_server = this;
}

void Server::pre_frame_tick()
{
	if ( _is_started )
	{
		read_incoming_messages();
		SteamNetworkingSockets()->RunCallbacks( this );
	}	
}

void Server::post_frame_tick()
{
	if ( _is_started )
	{
		snapshot();
	}
}

int Server::remove_client( Client *cl )
{
	vector_entnum &ents = cl->get_owned_ents();
	for ( size_t i = 0; i < ents.size(); i++ )
	{
		BaseEntity *ent = _entlist[ents[i]];
		remove_entity( ent );
	}
	_client_id_alloc.free( cl->get_client_id() );
	return (int)_clients_by_address.erase( cl->get_connection() );
}

void Server::remove_entity( BaseEntity *ent )
{
	std::cout << "removing entity " << ent << std::endl;
	ent->despawn();
	_entnum_alloc.free( ent->get_entnum() );

	Datagram dg = BeginMessage( NETMSG_DELETE_ENTITY );
	dg.add_uint32( ent->get_entnum() );
	broadcast_datagram( dg, true );

	_entlist.erase( _entlist.find( ent->get_entnum() ) );
}

void Server::handle_client_hello( SteamNetConnectionStatusChangedCallback_t *info )
{
	nassertv( _clients_by_address.find( info->m_hConn ) == _clients_by_address.end() );

	printf( "Got connection from %s\n", info->m_info.m_szConnectionDescription );

	// Try to accept the connection
	if ( SteamNetworkingSockets()->AcceptConnection( info->m_hConn ) != k_EResultOK )
	{
		SteamNetworkingSockets()->CloseConnection( info->m_hConn, 0, nullptr, false );
		printf( "Couldn't accept connection.\n" );
		return;
	}

	// Assign the poll group
	if ( !SteamNetworkingSockets()->SetConnectionPollGroup( info->m_hConn, _poll_group ) )
	{
		SteamNetworkingSockets()->CloseConnection( info->m_hConn, 0, nullptr, false );
		printf( "Failed to set poll group on connection.\n" );
		return;
	}

	PT( Client ) cl = new Client( info->m_info.m_addrRemote, info->m_hConn, _client_id_alloc.allocate() );
	_clients_by_address[info->m_hConn] = cl;

	PT( BaseEntity ) plyr = make_entity_by_name( "baseentity" );
	cl->grant_ownership( plyr->get_entnum() );
	plyr->set_owner_client_id( cl->get_client_id() );

	NetDatagram dg = BeginMessage( NETMSG_HELLO_RESP );
	dg.add_uint16( cl->get_client_id() );
	dg.add_string( g_game->_map );
	send_datagram( dg, cl->get_connection() );

	update_client_state( cl, CLIENTSTATE_NONE );
}

void Server::update_client_state( Client *cl, int state )
{
	cl->set_client_state( state );

	if ( state == CLIENTSTATE_PLAYING )
	{
		// Client has finished loading and started playing.
		// Send all entities.
		send_full_snapshot_to_client( cl );
	}
}

void Server::process_cmd( Client *cl, const std::string &cmd )
{
}

void Server::handle_datagram( Datagram &dg, Client *cl )
{
	DatagramIterator dgi( dg );

	int msgtype = dgi.get_uint16();

	switch ( msgtype )
	{
	case NETMSG_CLIENT_STATE:
	{
		int state = dgi.get_uint8();
		update_client_state( cl, state );
		break;
	}
	case NETMSG_CMD:
	{
		std::string cmd = dgi.get_string();
		process_cmd( cl, cmd );
		break;
	}
	default:
		break;
	}
}

void Server::read_incoming_messages()
{
	ISteamNetworkingMessage *incoming_msg = nullptr;
	int num_msgs = SteamNetworkingSockets()->ReceiveMessagesOnPollGroup( _poll_group, &incoming_msg, 1 );
	if ( num_msgs == 0 )
		return;
	if ( num_msgs < 0 )
	{
		std::cout << "Error checking for messages" << std::endl;
		return;
	}

	nassertv( num_msgs == 1 && incoming_msg );
	auto iclient = _clients_by_address.find( incoming_msg->GetConnection() );
	nassertv( iclient != _clients_by_address.end() );
	Client *cl = iclient->second;

	Datagram dg;
	dg.append_data( incoming_msg->m_pData, incoming_msg->m_cbSize );
	handle_datagram( dg, cl );

	incoming_msg->Release();
}

void Server::OnSteamNetConnectionStatusChanged( SteamNetConnectionStatusChangedCallback_t *info )
{
	switch ( info->m_info.m_eState )
	{
	case k_ESteamNetworkingConnectionState_ClosedByPeer:
	case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
		if ( info->m_eOldState == k_ESteamNetworkingConnectionState_Connected )
		{
			auto iclient = _clients_by_address.find( info->m_hConn );
			nassertv( iclient != _clients_by_address.end() );

			// Select appropriate log messages
			const char *pszDebugLogAction;
			if ( info->m_info.m_eState == k_ESteamNetworkingConnectionState_ProblemDetectedLocally )
			{
				pszDebugLogAction = "lost (Problem detected locally)";
			}
			else
			{
				// Note that here we could check the reason code to see if
				// it was a "usual" connection or an "unusual" one.
				pszDebugLogAction = "lost (Closed by peer)";
			}

			// Spew something to our own log.  Note that because we put their nick
			// as the connection description, it will show up, along with their
			// transport-specific data (e.g. their IP address)
			printf( "Connection %s %s, reason %d: %s\n",
				info->m_info.m_szConnectionDescription,
				pszDebugLogAction,
				info->m_info.m_eEndReason,
				info->m_info.m_szEndDebug
			);

			remove_client( iclient->second );
		}
		break;
	case k_ESteamNetworkingConnectionState_Connecting:
		handle_client_hello( info );
		break;
	default:
		break;
	}
}

bool Server::startup()
{
	SteamNetworkingErrMsg err;
	if ( !GameNetworkingSockets_Init( nullptr, err ) )
	{
		std::cout << "ERROR initializing networking library: " << err << std::endl;
		return false;
	}

	SteamNetworkingIPAddr server_local_addr;
	server_local_addr.Clear();
	server_local_addr.m_port = server_port;

	_listen_socket = SteamNetworkingSockets()->CreateListenSocketIP( server_local_addr, 0, nullptr );
	if ( _listen_socket == k_HSteamListenSocket_Invalid )
	{
		std::cout << "Failed to listen on port " << server_port << std::endl;
		return false;
	}

	_poll_group = SteamNetworkingSockets()->CreatePollGroup();
	if ( _poll_group == k_HSteamNetPollGroup_Invalid )
	{
		std::cout << "Failed to listen on port " << server_port << std::endl;
		return false;
	}

	_is_started = true;

	return true;
}

PT( BaseEntity ) Server::make_entity_by_name( const std::string &name )
{
	BaseEntity *singleton = BaseEntity::_entity_to_class[name];
	PT( BaseEntity ) ent = singleton->make_new();
	ent->init( _entnum_alloc.allocate() );
	ent->precache();
	ent->spawn();

	_entlist.insert( entmap_t::value_type( ent->get_entnum(), ent ) );
	std::cout << _entlist.size() << std::endl;

	std::cout << "Made new " << name << ", entnum" << ent->get_entnum() << std::endl;

	return ent;
}

void Server::send_datagram( Datagram &dg, HSteamNetConnection conn )
{
	SteamNetworkingSockets()->SendMessageToConnection( conn, dg.get_data(),
							   dg.get_length(),
							   k_nSteamNetworkingSend_Reliable, nullptr );
}

void Server::build_snapshot( Datagram &dg, bool full_snapshot )
{
	dg.add_uint16( NETMSG_SNAPSHOT );
	dg.add_uint32( _entlist.size() );

	for ( auto itr = _entlist.begin();
	      itr != _entlist.end(); ++itr )
	{
		BaseEntity *ent = itr->second;
		ServerClass *cls = ent->get_server_class();
		SendTable &st = cls->get_send_table();

		dg.add_uint32( ent->get_entnum() );
		dg.add_string( cls->get_network_name() );

		int num_props;
		if ( full_snapshot || ent->is_entity_fully_changed() )
			num_props = st.get_num_props();
		else
			num_props = ent->get_num_changed_offsets();

		dg.add_uint16( num_props );

		int total_props = st.get_num_props();
		for ( int j = 0; j < total_props; j++ )
		{
			SendProp &prop = st.get_send_props()[j];
			if ( full_snapshot || ent->is_property_changed( &prop ) )
			{
				
				dg.add_string( prop.get_prop_name() );
				void *data = (unsigned char *)ent + prop.get_offset();
				prop.get_proxy()( &prop, data, dg );
			}
		}

		if ( !full_snapshot )
			ent->reset_changed_offsets();
	}
}

void Server::broadcast_datagram( Datagram &dg, bool only_playing )
{
	for ( auto itr = _clients_by_address.begin();
	      itr != _clients_by_address.end(); ++itr )
	{
		Client *cl = itr->second;

		if ( ( only_playing && cl->get_client_state() == CLIENTSTATE_PLAYING ) ||
		     !only_playing )
			send_datagram( dg, cl->get_connection() );
	}
}

void Server::send_full_snapshot_to_client( Client *cl )
{
	Datagram dg;
	build_snapshot( dg, true );
	send_datagram( dg, cl->get_connection() );
}

void Server::snapshot()
{
	Datagram dg;
	build_snapshot( dg, false );

	// Send snapshot to all clients
	broadcast_datagram( dg, true );
}