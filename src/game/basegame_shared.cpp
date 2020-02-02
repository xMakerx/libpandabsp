#include "game/basegame_shared.h"
#include "game/physics_utils.h"

#include "bsp_render.h"

#include <graphicsPipeSelection.h>
#include <frameBufferProperties.h>
#include <perspectiveLens.h>
#include <camera.h>
#include <displayRegion.h>
#include <configVariableInt.h>
#include <configVariableDouble.h>
#include <modelRoot.h>
#include <rescaleNormalAttrib.h>
#include <pgTop.h>
#include <mouseWatcher.h>
#include <mouseAndKeyboard.h>
#include <buttonThrower.h>
#include <load_prc_file.h>

BaseGameShared *g_game = nullptr;

static ConfigVariableInt phys_substeps( "phys_substeps", 1 );

BaseGameShared::BaseGameShared() :
	_task_manager( AsyncTaskManager::get_global_ptr() ),
	_loader( Loader::get_global_ptr() ),
	_bsp_loader( BSPLoader::get_global_ptr() ),
	_ival_mgr( CIntervalManager::get_global_ptr() ),
	_event_mgr( EventHandler::get_global_event_handler() ),
	_vfs( VirtualFileSystem::get_global_ptr() ),
	_quit( false )
{
	g_game = this;
}

void BaseGameShared::cleanup_bsp_level()
{
	if ( !_bsp_level.is_empty() )
	{
		detach_and_remove_bullet_nodes( _bsp_level );
		_bsp_level.remove_node();
	}
	_bsp_loader->cleanup();
}

void BaseGameShared::load_bsp_level( const Filename &path, bool is_transition )
{
	_map = path.get_basename_wo_extension();
	nassertv( _bsp_loader->read( path, is_transition ) );
	_bsp_level = _bsp_loader->get_result();
}

void BaseGameShared::mount_multifile( const Filename &mfpath )
{
	_vfs->mount( mfpath, Filename( "." ), VirtualFileSystem::MF_read_only );
}

void BaseGameShared::mount_multifiles()
{
	mount_multifile( "engine.mf" );
}

void BaseGameShared::load_cfg_file( const Filename &cfgpath )
{
	load_prc_file( cfgpath );
}

void BaseGameShared::load_cfg_data( const std::string &data )
{
	load_prc_file_data( "", data );
}

void BaseGameShared::load_cfg_files()
{
	load_cfg_file( "cfg/engine.cfg" );
}

bool BaseGameShared::startup()
{
	mount_multifiles();
	load_cfg_files();

	setup_rendering();
	setup_scene();
	setup_camera();
	setup_dgraph();
	setup_physics();
	setup_audio();
	setup_tasks();
	setup_shaders();
	setup_bsp();

	_quit = false;

	return true;
}

void BaseGameShared::setup_rendering()
{
}

void BaseGameShared::setup_camera()
{
}

void BaseGameShared::setup_dgraph()
{
}

void BaseGameShared::setup_audio()
{
}

void BaseGameShared::setup_shaders()
{
}

void BaseGameShared::setup_tasks()
{
	//
	// Tasks
	//

	_reset_prev_transform_task = new GenericAsyncTask( "resetPrevTransform", reset_prev_transform_task, this );
	_reset_prev_transform_task->set_sort( -51 );
	_task_manager->add( _reset_prev_transform_task );

	_ival_task = new GenericAsyncTask( "ivalLoop", ival_task, this );
	_ival_task->set_sort( 20 );
	_task_manager->add( _ival_task );

	_physics_task = new GenericAsyncTask( "physicsUpdate", physics_task, this );
	_physics_task->set_sort( 30 );
	_task_manager->add( _physics_task );

	_garbage_collect_task = new GenericAsyncTask( "garbageCollectStates", garbage_collect_task, this );
	_garbage_collect_task->set_sort( 46 );
	_task_manager->add( _garbage_collect_task );
}

void BaseGameShared::setup_scene()
{
	//
	// Scene roots
	//

	_render = NodePath( new BSPRender( "render", _bsp_loader ) );
	_render.set_attrib( RescaleNormalAttrib::make_default() );
	_render.set_two_sided( false );

	_hidden = NodePath( "hidden" );
}

void BaseGameShared::setup_physics()
{
	//
	// Physics
	//

	_physics_world = new BulletWorld;
	// Panda units are in feet, so the gravity is 32 feet per second,
	// not 9.8 meters per second.
	_physics_world->set_gravity( 0.0f, 0.0f, -32.174f );
}

void BaseGameShared::setup_bsp()
{
	//
	// BSP
	//

	_bsp_loader->set_gamma( 2.2f );
	_bsp_loader->set_render( _render );
	_bsp_loader->set_want_visibility( true );
	_bsp_loader->set_want_lightmaps( true );
	_bsp_loader->set_visualize_leafs( false );
	_bsp_loader->set_physics_world( _physics_world );
}

void BaseGameShared::shutdown()
{
	_physics_world = nullptr;
	_render.remove_node();
	_hidden.remove_node();
	_bsp_loader->cleanup();

	_physics_task->remove();
	_physics_task = nullptr;
	_garbage_collect_task->remove();
	_garbage_collect_task = nullptr;
	_ival_task->remove();
	_ival_task = nullptr;
	_reset_prev_transform_task->remove();
	_reset_prev_transform_task = nullptr;

	_quit = true;
}

void BaseGameShared::do_frame()
{
	_task_manager->poll();
}

void BaseGameShared::run()
{
	while ( !_quit )
	{
		do_frame();
	}
}

DEFINE_TASK_FUNC( BaseGameShared::reset_prev_transform_task )
{
	PandaNode::reset_all_prev_transform();
	return AsyncTask::DS_cont;
}

DEFINE_TASK_FUNC( BaseGameShared::physics_task )
{
	BaseGameShared *self = (BaseGameShared *)data;
	double dt = ClockObject::get_global_clock()->get_dt();
	if ( dt <= 0.016f )
	{
		self->_physics_world->do_physics( dt, phys_substeps, 0.016f );
	}
	else if ( dt <= 0.033f )
	{
		self->_physics_world->do_physics( dt, phys_substeps, 0.033f );
	}
	else
	{
		self->_physics_world->do_physics( dt, 0 );
	}
	return AsyncTask::DS_cont;
}


DEFINE_TASK_FUNC( BaseGameShared::ival_task )
{
	InputDeviceManager::get_global_ptr()->update();
	CIntervalManager::get_global_ptr()->step();
	return AsyncTask::DS_cont;
}

DEFINE_TASK_FUNC( BaseGameShared::garbage_collect_task )
{
	TransformState::garbage_collect();
	RenderState::garbage_collect();
	return AsyncTask::DS_cont;
}