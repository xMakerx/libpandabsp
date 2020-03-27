#pragma once

#include <datagramIterator.h>
#include <netAddress.h>
#include <referenceCount.h>
#include <steam/steamnetworkingsockets.h>

#include "c_baseentity.h"
#include "client_commandmgr.h"

class CUserCmd;

NotifyCategoryDeclNoExport( c_client )

class EXPORT_CLIENT_DLL C_Client : public ISteamNetworkingSocketsCallbacks, public ReferenceCount
{
public:
	C_Client();

	void receive_snapshot( DatagramIterator &dgi );

	C_BaseEntity *find_entity_by_id( entid_t entnum ) const;
	void remove_entity( entid_t entnum );

	void set_client_state( int state );

	void send_datagram( Datagram &dg );

	void connect( const NetAddress &addr );

	void tick();

	virtual void OnSteamNetConnectionStatusChanged( SteamNetConnectionStatusChangedCallback_t *clbk ) override;

	INLINE int get_my_client_id() const
	{
		return _my_client_id;
	}

	INLINE float get_server_tickrate() const
	{
		return _server_tickrate;
	}

	void cmd_tick();

	void send_tick();

private:
	void read_incoming_messages();
	void handle_datagram( const Datagram &dg );
	void handle_server_tick( DatagramIterator &dgi );
	void connect_success( DatagramIterator &dgi );

	PT( C_BaseEntity ) make_client_entity( const std::string &network_name, entid_t entnum );

public:
	pvector<PT( C_BaseEntity )> _entlist;
	NetAddress _server_addr;

	HSteamNetConnection _connection;

	int _my_client_id;

	bool _connected;

	int _client_state;

	float _server_tickrate;

	float _last_server_tick_time;
	int _oldtickcount;
	int _servertickcount;
	float _tick_remainder;

	int _server_tick;
	float _server_frametime;

	CClientCMDManager _cmd_mgr;
};

extern EXPORT_CLIENT_DLL C_Client *g_client;