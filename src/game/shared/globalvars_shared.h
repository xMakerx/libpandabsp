#pragma once

class CBaseGlobalVars
{
public:
	CBaseGlobalVars( bool is_client ) :
		_is_client( is_client ),
		_timestamp_networking_base( 100 ),
		_timestamp_randomize_window( 32 )
	{
		realtime = 0.0;
		curtime = 0.0;
		framecount = 0;
		absoluteframetime = 0.0;
		frametime = 0.0;
		interval_per_tick = 0.0;
		interpolation_amount = 0.0;
		sim_ticks_this_frame = 0;
		max_clients = 0;
		tickcount = 0;
	}

	bool is_client() const
	{
		return _is_client;
	}

	int get_network_base( int tick, int entity ) const
	{
		int entity_mod = entity % _timestamp_randomize_window;
		int base_tick = _timestamp_networking_base *
			(int)( ( tick - entity_mod ) / _timestamp_networking_base );
		return base_tick;
	}
public:
	float realtime;
	float curtime;
	int framecount;
	float absoluteframetime;
	float frametime;
	int tickcount;
	float interval_per_tick;
	float interpolation_amount;
	int sim_ticks_this_frame;
	int max_clients;

private:
	bool _is_client;
	int _timestamp_networking_base;
	int _timestamp_randomize_window;
};