#pragma once

#include <datagramIterator.h>
#include <netAddress.h>
#include <referenceCount.h>
#include <steam/steamnetworkingsockets.h>

#include "c_baseentity.h"

class EXPCL_PANDABSP C_Client : public ISteamNetworkingSocketsCallbacks, public ReferenceCount
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

private:
	void read_incoming_messages();
	void handle_datagram( const Datagram &dg );

	void connect_success( DatagramIterator &dgi );

	PT( C_BaseEntity ) make_client_entity( const std::string &network_name, entid_t entnum );

private:
	pvector<PT( C_BaseEntity )> _entlist;
	NetAddress _server_addr;

	HSteamNetConnection _connection;

	int _my_client_id;

	bool _connected;

	int _client_state;
};

extern EXPCL_PANDABSP C_Client *g_client;