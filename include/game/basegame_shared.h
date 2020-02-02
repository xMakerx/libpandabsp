#ifndef BASEGAME_H_
#define BASEGAME_H_

#include "config_bsp.h"
#include "bsploader.h"
#include "tasks.h"

#include <filename.h>
#include <asyncTaskManager.h>
#include <genericAsyncTask.h>
#include <loader.h>
#include <referenceCount.h>
#include <graphicsEngine.h>
#include <graphicsStateGuardian.h>
#include <graphicsPipe.h>
#include <graphicsPipeSelection.h>
#include <graphicsWindow.h>
#include <nodePath.h>
#include <bulletWorld.h>
#include <dataGraphTraverser.h>
#include <audioManager.h>
#include <cIntervalManager.h>
#include <inputDeviceManager.h>
#include <eventHandler.h>
#include <virtualFileSystem.h>

class EXPCL_PANDABSP BaseGameShared : public ReferenceCount
{
public:
	BaseGameShared();

	void load_cfg_file( const Filename &cfgpath );
	void load_cfg_data( const std::string &data );

	void mount_multifile( const Filename &mfpath );

	virtual void cleanup_bsp_level();
	virtual void load_bsp_level( const Filename &path, bool is_transition = false );

	virtual bool startup();
	virtual void shutdown();

	virtual void do_frame();
	void run();

	INLINE Loader *get_loader() const
	{
		return _loader;
	}

	INLINE AsyncTaskManager *get_task_manager() const
	{
		return _task_manager;
	}

	INLINE BSPLoader *get_bsp_loader() const
	{
		return _bsp_loader;
	}

	INLINE VirtualFileSystem *get_vfs() const
	{
		return _vfs;
	}

protected:
	virtual void setup_rendering();
	virtual void setup_scene();
	virtual void setup_camera();
	virtual void setup_dgraph();
	virtual void setup_physics();
	virtual void setup_audio();
	virtual void setup_tasks();
	virtual void setup_shaders();
	virtual void setup_bsp();
	virtual void load_cfg_files();
	virtual void mount_multifiles();

private:
	DECLARE_TASK_FUNC( physics_task );
	DECLARE_TASK_FUNC( garbage_collect_task );
	DECLARE_TASK_FUNC( ival_task );
	DECLARE_TASK_FUNC( reset_prev_transform_task );

public:
	AsyncTaskManager *_task_manager;
	Loader *_loader;
	BSPLoader *_bsp_loader;
	NodePath _bsp_level;
	EventHandler *_event_mgr;
	CIntervalManager *_ival_mgr;
	VirtualFileSystem *_vfs;
	PT( BulletWorld ) _physics_world;

	std::string _map;
	
	NodePath _render;
	NodePath _hidden;

protected:
	bool _quit;

	PT( GenericAsyncTask ) _garbage_collect_task;
	PT( GenericAsyncTask ) _ival_task;
	PT( GenericAsyncTask ) _physics_task;
	PT( GenericAsyncTask ) _reset_prev_transform_task;
};

extern EXPCL_PANDABSP BaseGameShared *g_game;

#endif // BASEGAME_H_