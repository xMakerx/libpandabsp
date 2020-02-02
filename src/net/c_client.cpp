#include "net/c_client.h"
#include "net/messages.h"
#include "game/basegame_shared.h"

#include <datagramIterator.h>
#include <clockObject.h>
#include <configVariableDouble.h>

C_Client *g_client = nullptr;

static ConfigVariableDouble cl_heartbeat_rate( "cl_heartbeat_rate", 1.0 );

C_Client::C_Client() :
	_connection( k_HSteamNetConnection_Invalid ),
	_connected( false ),
	_client_state( CLIENTSTATE_NONE )
{
	g_client = this;

	SteamNetworkingErrMsg msg;
	GameNetworkingSockets_Init( nullptr, msg );
}

void C_Client::OnSteamNetConnectionStatusChanged( SteamNetConnectionStatusChangedCallback_t *info )
{
	nassertv( info->m_hConn == _connection || _connection == k_HSteamNetConnection_Invalid );

	switch ( info->m_info.m_eState )
	{
	case k_ESteamNetworkingConnectionState_ClosedByPeer:
	case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
		std::cout << "lost connection" << std::endl;
		SteamNetworkingSockets()->CloseConnection( info->m_hConn, 0, nullptr, false );
		_connection = k_HSteamNetConnection_Invalid;
		_connected = false;
		break;
	case k_ESteamNetworkingConnectionState_Connected:
		std::cout << "Connect success" << std::endl;
		_connected = true;
		break;
	default:
		break;
	}
}

void C_Client::connect( const NetAddress &addr )
{
	_server_addr = addr;
	SteamNetworkingIPAddr saddr;
	saddr.Clear();
	saddr.ParseString( addr.get_ip_string().c_str() );
	saddr.m_port = addr.get_port();

	_connection = SteamNetworkingSockets()->ConnectByIPAddress( saddr, 0, nullptr );
	if ( _connection == k_HSteamNetConnection_Invalid )
	{
		std::cout << "Failed to connect to " << addr << std::endl;
		return;
	}
}

void C_Client::connect_success( DatagramIterator &dgi )
{
	_my_client_id = dgi.get_uint16();
	std::string mapname = dgi.get_string();
	g_game->load_bsp_level( "maps/" + mapname );
}

void C_Client::set_client_state( int state )
{
	_client_state = state;
	Datagram dg = BeginMessage( NETMSG_CLIENT_STATE );
	dg.add_uint8( _client_state );
	send_datagram( dg );
}

void C_Client::send_datagram( Datagram &dg )
{
	SteamNetworkingSockets()->SendMessageToConnection( _connection, dg.get_data(), dg.get_length(),
							   k_nSteamNetworkingSend_Reliable, nullptr );
}

void C_Client::handle_datagram( const Datagram &dg )
{
	DatagramIterator dgi( dg );
	int msgtype = dgi.get_uint16();

	switch ( msgtype )
	{
	case NETMSG_HELLO_RESP:
		connect_success( dgi );
		break;
	case NETMSG_SNAPSHOT:
		receive_snapshot( dgi );
		break;
	case NETMSG_SERVER_HEARTBEAT:
		// todo
		break;
	case NETMSG_DELETE_ENTITY:
	{
		entid_t entnum = dgi.get_uint32();
		remove_entity( entnum );
		break;
	}
	case NETMSG_CHANGE_LEVEL:
	{
		std::string mapname = dgi.get_string();
		bool is_transition = (bool)dgi.get_uint8();
		g_game->load_bsp_level( "maps/" + mapname, is_transition );
		break;
	}
	}
}

void C_Client::read_incoming_messages()
{
	ISteamNetworkingMessage *msg = nullptr;
	int num_msgs = SteamNetworkingSockets()->ReceiveMessagesOnConnection( _connection, &msg, 1 );
	if ( num_msgs <= 0 )
		return;

	Datagram dg;
	dg.append_data( msg->m_pData, msg->m_cbSize );
	std::cout << "DG is size " << dg.get_length() << std::endl;
	handle_datagram( dg );

	msg->Release();
}

void C_Client::tick()
{
	if ( _connected )
	{
		read_incoming_messages();
		SteamNetworkingSockets()->RunCallbacks( this );
	}
}

void C_Client::remove_entity( entid_t entnum )
{
	C_BaseEntity *ent = find_entity_by_id( entnum );
	if ( !ent )
		return;

	ent->despawn();
	_entlist.erase( std::find( _entlist.begin(), _entlist.end(), ent ) );
}

C_BaseEntity *C_Client::find_entity_by_id( entid_t entnum ) const
{
	std::cout << "Find entity by id " << entnum << std::endl;
	for ( size_t i = 0; i < _entlist.size(); i++ )
	{
		C_BaseEntity *ent = _entlist[i];
		std::cout << "ent " << ent << std::endl;
		std::cout << "entnum " << ent->get_entnum() << std::endl;
		if ( ent->get_entnum() == entnum )
			return ent;
	}

	return nullptr;
}

PT( C_BaseEntity ) C_Client::make_client_entity( const std::string &network_name, entid_t entnum )
{
	PT( C_BaseEntity ) ent = nullptr;

	for ( auto itr = C_BaseEntity::_networkname_to_class.begin();
	      itr != C_BaseEntity::_networkname_to_class.end(); ++itr )
	{
		std::string nname = itr->first;
		C_BaseEntity *singleton = itr->second;

		if ( nname == network_name )
		{
			std::cout << "Making client entity " << network_name << std::endl;
			ent = singleton->make_new();
			ent->init( entnum );
			_entlist.push_back( ent );
			return ent;
		}
	}

	std::cout << "Client-side view of " << network_name << " not found!" << std::endl;

	return ent;
}

void C_Client::receive_snapshot( DatagramIterator &dgi )
{
	std::cout << "Received snapshot" << std::endl;
	int num_ents = dgi.get_uint32();
	std::cout << "\t" << num_ents << " entities" << std::endl;
	for ( int ient = 0; ient < num_ents; ient++ )
	{
		entid_t entnum = dgi.get_uint32();
		std::string network_name = dgi.get_string();
		int num_props = dgi.get_uint16();

		// Make sure this entity exists, if not, create it.
		PT( C_BaseEntity ) ent = find_entity_by_id( entnum );
		if ( !ent )
			ent = make_client_entity( network_name, entnum );

		nassertv( ent != nullptr );

		std::cout << "entnum " << entnum << std::endl;
		std::cout << "netname " << network_name << std::endl;
		std::cout << "numprops " << num_props << std::endl;

		for ( int iprop = 0; iprop < num_props; iprop++ )
		{
			std::string prop_name = dgi.get_string();
			std::cout << "propname " << prop_name << std::endl;
			RecvProp *prop = ent->get_client_class()->
				get_recv_table().find_recv_prop( prop_name );
			nassertv( prop != nullptr );
			void *out = (unsigned char *)ent.p() + prop->get_offset();
			prop->get_proxy()( prop, ent, out, dgi );
		}
	}
}