#include "game/basegame.h"
#include "net/messages.h"

#include <configVariableDouble.h>

static ConfigVariableDouble sv_tickrate( "sv_tickrate", 16.0 );

BaseGame::BaseGame() :
	BaseGameShared(),
	_server( new Server )
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

void BaseGame::setup_bsp()
{
	BaseGameShared::setup_bsp();
	_bsp_loader->set_ai( true );
}

bool BaseGame::startup()
{
	if ( !BaseGameShared::startup() )
		return false;

	if ( !_server->startup() )
		return false;

	return true;
}

void BaseGame::do_frame()
{
	ClockObject *global_clock = ClockObject::get_global_clock();
	double start_time = global_clock->get_real_time();

	_server->pre_frame_tick();
	BaseGameShared::do_frame();
	_server->post_frame_tick();

	double end_time = global_clock->get_real_time();
	double ms_elapsed = ( end_time - start_time ) * 1000.0;
	double sleep_time = sv_tickrate - ms_elapsed;
	if ( sleep_time > 0 )
		Thread::get_current_thread()->sleep( sleep_time / 1000.0 );
}

void BaseGame::load_cfg_files()
{
	BaseGameShared::load_cfg_files();
	load_cfg_file( "cfg/server.cfg" );
}