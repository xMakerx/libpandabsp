#pragma once

#include "entityshared.h"

#include <referenceCount.h>
#include <clockObject.h>
#include <pvector.h>

#include <steam/steamnetworkingsockets.h>

class EXPCL_PANDABSP Client : public ReferenceCount
{
public:
	INLINE Client( const SteamNetworkingIPAddr &addr, HSteamNetConnection socket, int id ) :
		_addr( addr ),
		_socket( socket ),
		_client_state( CLIENTSTATE_NONE ),
		_client_id( id )
	{
		heartbeat();
	}

	INLINE SteamNetworkingIPAddr get_addr() const
	{
		return _addr;
	}

	INLINE HSteamNetConnection get_connection() const
	{
		return _socket;
	}

	INLINE void heartbeat()
	{
		_last_heartbeat_time = ClockObject::get_global_clock()->get_frame_time();
	}

	INLINE double get_last_heartbeat_time() const
	{
		return _last_heartbeat_time;
	}

	INLINE void grant_ownership( entid_t ent )
	{
		if ( std::find( _owned_ents.begin(), _owned_ents.end(), ent ) == _owned_ents.end() )
			_owned_ents.push_back( ent );
	}

	INLINE bool has_ownership( entid_t ent )
	{
		return ( std::find( _owned_ents.begin(), _owned_ents.end(), ent ) != _owned_ents.end() );
	}

	INLINE vector_entnum &get_owned_ents()
	{
		return _owned_ents;
	}

	INLINE void set_client_state( int state )
	{
		_client_state = state;
	}

	INLINE int get_client_state() const
	{
		return _client_state;
	}

	INLINE int get_client_id() const
	{
		return _client_id;
	}

private:
	int _client_state;
	int _client_id;
	vector_entnum _owned_ents;
	SteamNetworkingIPAddr _addr;
	HSteamNetConnection _socket;
	double _last_heartbeat_time;
};