#include "basegame.h"
#include "netmessages.h"
#include "sv_bsploader.h"
#include "baseplayer.h"
#include "globalvars_server.h"

#include <configVariableDouble.h>
#include <pStatClient.h>

static int host_frameticks = 0;
static int host_tickcount  = 0;
static int host_currentframetick = 0;

static CServerGlobalVars vars( false );

BaseGame::BaseGame()
	: BaseGameShared()
	, _server( new Server ),
	_frame_start_time( 0 ),
	_paused( false )
{
	g_globals = &vars;
	g_globals->game = this;
}

void BaseGame::run_cmd( const std::string &full_cmd, Client *cl )
{
}

void BaseGame::load_bsp_level( const Filename &path, bool is_transition )
{
	Datagram dg = BeginMessage( NETMSG_CHANGE_LEVEL );
	dg.add_string( path.get_basename_wo_extension() );
	dg.add_uint8( (int)is_transition );
	_server->broadcast_datagram( dg );

	BaseGameShared::load_bsp_level( path, is_transition );
}

void BaseGame::make_bsp()
{
	_bsp_loader = new CSV_BSPLoader;
	BSPLoader::set_global_ptr( _bsp_loader );
}

void BaseGame::setup_bsp()
{
	_bsp_loader->set_ai( true );
}

bool BaseGame::startup()
{
	if ( !BaseGameShared::startup() )
		return false;

	if ( !_server->startup() )
		return false;

	g_globals->interval_per_tick = _host_state.interval_per_tick;

	return true;
}

void BaseGame::run_entities( bool simulating )
{
	if ( simulating )
	{
		for ( auto itr : *_server->get_entlist() )
		{
			CBaseEntity *ent = itr.second;
			ent->run_thinks();
		}
	}
	else
	{
		// Only simulate players
		Server::clientmap_t *clients = _server->get_clients();
		for ( auto itr : *clients )
		{
			Client *cl = itr.second;
			if ( cl->get_player() )
				cl->get_player()->run_thinks();
		}
	}
}

/**
 * Run a frame from the server.
 */
void BaseGame::do_frame()
{
	ClockObject *global_clock = ClockObject::get_global_clock();
	global_clock->tick();

	float curtime = global_clock->get_real_time();
	float dt = curtime - _frame_start_time;
	_frame_start_time = curtime;

	g_globals->curtime = curtime;
	g_globals->absoluteframetime = dt;
	g_globals->frametime = dt;
	g_globals->realtime = curtime;
	g_globals->framecount++;

	_host_state.curtime = curtime;
	_host_state.frametime = dt;
	
	double prevremainder;
	int numticks;

	prevremainder = _host_state.remainder;
	if ( prevremainder < 0 )
		prevremainder = 0;

	_host_state.remainder += dt;

	std::cout << "_host_state.remainder = " << _host_state.remainder << std::endl;

	numticks = 0; // how many ticks we will simulate this frame
	if ( _host_state.remainder >= _host_state.interval_per_tick )
	{
		numticks = (int)( _host_state.remainder / _host_state.interval_per_tick );

		// round to nearest even ending tick in alternate ticks mode so the last
		// tick is always simulated prior to updating the network data
		if ( sv_alternateticks )
		{
			int starttick = _host_state.tickcount;
			int endtick = starttick + numticks;
			endtick = AlignValue( endtick, 2 );
			numticks = endtick - starttick;
		}

		_host_state.remainder -= numticks * _host_state.interval_per_tick;
	}

	std::cout << "Simulating " << numticks << " ticks" << std::endl;

	_host_state.interpolation_amount = 0.0f;

	_host_state.frameticks = numticks;
	_host_state.currentframetick = 0;
	_host_state.sim_ticks_this_frame = 1;
	g_globals->sim_ticks_this_frame = 1;

	global_clock->set_mode( ClockObject::M_slave );
	for ( int tick = 0; tick < numticks; tick++ )
	{
		g_globals->tickcount = _host_state.tickcount;
		g_globals->curtime = _host_state.interval_per_tick * _host_state.tickcount;
		g_globals->frametime = _host_state.interval_per_tick;
		global_clock->set_frame_time( g_globals->curtime );
		global_clock->set_dt( g_globals->frametime );

		std::cout << "tick " << _host_state.tickcount << std::endl;
		std::cout << "\ttick in frame: " << tick << std::endl;
		std::cout << "\tcurtime: " << g_globals->curtime << std::endl;
		std::cout << "\tframetime: " << g_globals->frametime << std::endl;

		_server->read_incoming_messages();
		_server->run_networking_events();

		bool simulating = !is_paused();

		PandaNode::reset_all_prev_transform();
		_event_mgr->process_events();
		CIntervalManager::get_global_ptr()->step();
		
		// Run entities
		run_entities( simulating );

		// Simulate Bullet physics
		_physics_world->do_physics( g_globals->frametime );

		//bool finaltick = ( tick == ( numticks - 1 ) );
		_host_state.tickcount++;
		_host_state.currentframetick++;
		_host_state.sim_ticks_this_frame++;
		g_globals->sim_ticks_this_frame++;
	}

	// Restore initial clock values
	global_clock->set_frame_time( curtime );
	global_clock->set_dt( dt );
	global_clock->set_mode( ClockObject::M_normal );
	g_globals->frametime = dt;
	g_globals->curtime = curtime;

	send_tick();
	_server->snapshot();

	TransformState::garbage_collect();
	RenderState::garbage_collect();

	double end_time   = global_clock->get_real_time();
	double ms_elapsed = ( end_time - curtime ) * 1000.0;
	//std::cout << "Server frame took " << ms_elapsed << " ms, sleeping for ";
	double sleep_time = 0;//sv_tickrate - ms_elapsed;
	//std::cout << sleep_time << " ms.\n";
	if ( sleep_time > 0 )
		Thread::get_current_thread()->sleep( sleep_time / 1000.0 );

	PStatClient::main_tick();
}

void BaseGame::send_tick()
{
	Datagram dg = BeginMessage( NETMSG_TICK );
	dg.add_int32( g_globals->tickcount );
	dg.add_float32( _host_state.interval_per_tick );
	_server->broadcast_datagram( dg );
}

void BaseGame::load_cfg_files()
{
	BaseGameShared::load_cfg_files();
	load_cfg_file( "cfg/server.cfg" );
}

void BaseGame::setup_tasks()
{
}