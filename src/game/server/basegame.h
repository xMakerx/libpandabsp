#pragma once

#include "basegame_shared.h"
#include "server.h"

class CHostState
{
public:
	CHostState();

	double curtime;
	double frametime;
	int sim_ticks_this_frame;
	float interval_per_tick;
	int ticks_per_interval;
	int frameticks;
	int tickcount;
	int currentframetick;
	int maxclients;
	float interpolation_amount;
	double remainder;
};

INLINE CHostState::CHostState()
{
	curtime = 0.0;
	frametime = 0.0;
	sim_ticks_this_frame = 0;
	frameticks = 0;
	tickcount = 0;
	currentframetick = 0;
	remainder = 0.0;
	maxclients = 0;
	ticks_per_interval = sv_tickrate;
	interval_per_tick = 1.0f / sv_tickrate;
	maxclients = sv_maxclients;
	interpolation_amount = 0.0f;
}

/**
 * This is the server-side game framework.
 * It serves to initialize, contain, and manage all game/engine systems
 * related to the server.
 */
class EXPORT_SERVER_DLL BaseGame : public BaseGameShared
{
public:
	BaseGame();

	INLINE Server *get_server() const
	{
		return _server;
	}

	virtual void run_cmd( const std::string &full_cmd, Client *cl = nullptr );

	virtual void load_bsp_level( const Filename &path, bool is_transition = false );

	virtual bool startup();
	virtual void make_bsp();
	virtual void setup_bsp();
	virtual void do_frame();
	virtual void run_entities( bool simulating );

	void send_tick();

	int time_to_ticks( double dt );
	double ticks_to_time( double dt );

	CHostState *get_host_state();
	bool is_paused() const;

	static INLINE BaseGame *ptr()
	{
		return (BaseGame *)g_game;
	}

protected:
	virtual void setup_tasks();
	virtual void load_cfg_files();

public:
	PT( Server ) _server;
	CHostState _host_state;
	bool _paused;
	float _frame_start_time;
};

INLINE double BaseGame::ticks_to_time( double dt )
{
	return ( _host_state.interval_per_tick * (float)( dt ) );
}

INLINE int BaseGame::time_to_ticks( double dt )
{
	return ( (int)( 0.5f + (float)( dt ) / _host_state.interval_per_tick ) );
}

INLINE bool BaseGame::is_paused() const
{
	return _paused;
}

INLINE CHostState *BaseGame::get_host_state()
{
	return &_host_state;
}