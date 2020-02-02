#pragma once

#include <pmap.h>
#include <uniqueIdAllocator.h>
#include <referenceCount.h>

#include "baseentity.h"
#include "bsploader.h"
#include "client.h"

#include <steam\steamnetworkingsockets.h>

class EXPCL_PANDABSP Server : private ISteamNetworkingSocketsCallbacks, public ReferenceCount
{
public:
	Server();

	PT( BaseEntity ) make_entity_by_name( const std::string &name );
	void remove_entity( BaseEntity *ent );

	bool startup();

	void broadcast_datagram( Datagram &dg, bool only_playing = false );

	void snapshot();
	void send_full_snapshot_to_client( Client *cl );
	void build_snapshot( Datagram &dg, bool full_snapshot );

	void pre_frame_tick();
	void post_frame_tick();

	virtual void OnSteamNetConnectionStatusChanged( SteamNetConnectionStatusChangedCallback_t *clbk ) override;

	typedef pmap<HSteamNetConnection, PT( Client )> clientmap_t;

	void send_datagram( Datagram &dg, HSteamNetConnection conn );

	void process_cmd( Client *cl, const std::string &cmd );
	void update_client_state( Client *cl, int state );

private:
	void read_incoming_messages();

	int remove_client( Client *cl );

	void handle_datagram( Datagram &dg, Client *cl );

	void handle_client_hello( SteamNetConnectionStatusChangedCallback_t *info );

private:
	UniqueIdAllocator _entnum_alloc;
	UniqueIdAllocator _client_id_alloc;
	typedef pmap<entid_t, PT( BaseEntity )> entmap_t;
	entmap_t _entlist;
	BSPLoader _bsploader;

	clientmap_t _clients_by_address;

	HSteamListenSocket _listen_socket;
	HSteamNetPollGroup _poll_group;

	bool _is_started;
};

extern EXPCL_PANDABSP Server *g_server;