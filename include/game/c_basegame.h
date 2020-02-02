#pragma once

#include <windowProperties.h>

#include "game/basegame_shared.h"
#include "net/c_client.h"
#include "shader_generator.h"
#include "game/game_postprocess.h"
#include "audio_3d_manager.h"

/**
 * This is the client-side game framework.
 * It serves to initialize, contain, and manage all game/engine systems
 * related to the client.
 */
class EXPCL_PANDABSP C_BaseGame : public BaseGameShared
{
public:
	C_BaseGame();

	virtual bool startup();
	virtual void shutdown();

	virtual void do_frame();

	virtual void load_bsp_level( const Filename &path, bool is_transition = false );
	virtual void cleanup_bsp_level();

	INLINE LVector2i get_size() const
	{
		if ( !_graphics_window )
			return LVector2i::zero();

		return LVector2i( _graphics_window->get_x_size(),
				  _graphics_window->get_y_size() );
	}

	INLINE float get_aspect_ratio() const
	{
		if ( !_graphics_window )
			return 1.0f;

		return _graphics_window->get_sbs_left_x_size() / _graphics_window->get_sbs_left_y_size();
	}

	INLINE GraphicsEngine *get_graphics_engine() const
	{
		return _graphics_engine;
	}

	INLINE GraphicsStateGuardian *get_gsg() const
	{
		return _gsg;
	}

	INLINE GraphicsPipe *get_graphics_pipe() const
	{
		return _graphics_pipe;
	}

	INLINE GraphicsPipeSelection *get_graphics_pipe_selection() const
	{
		return _graphics_pipe_selection;
	}

	INLINE GraphicsWindow *get_graphics_window() const
	{
		return _graphics_window;
	}

	INLINE C_Client *get_client() const
	{
		return _client;
	}

	void adjust_window_aspect_ratio( float ratio );

protected:
	virtual void setup_camera();
	virtual void setup_bsp();
	virtual void setup_scene();
	virtual void setup_rendering();
	virtual void setup_shaders();
	virtual void setup_dgraph();
	virtual void setup_audio();
	virtual void setup_tasks();
	virtual void setup_postprocess();

	virtual void load_cfg_files();

	void window_event();
	static void handle_window_event( const Event *e, void *data );

private:
	DECLARE_TASK_FUNC( render_task );
	DECLARE_TASK_FUNC( data_task );
	DECLARE_TASK_FUNC( audio_task );

public:
	GraphicsEngine *_graphics_engine;
	PT( GraphicsStateGuardian ) _gsg;
	PT( GraphicsPipe ) _graphics_pipe;
	GraphicsPipeSelection *_graphics_pipe_selection;
	PT( GraphicsWindow ) _graphics_window;
	PT( BSPShaderGenerator ) _shader_generator;
	PT( Audio3DManager ) _audio3d;
	PT( GamePostProcess ) _post_process;
	DataGraphTraverser _dgtrav;
	PT( AudioManager ) _sfx_manager;
	PT( AudioManager ) _music_manager;
	InputDeviceManager *_input_mgr;
	PT( C_Client ) _client;
	PT( PerspectiveLens ) _cam_lens;

	NodePath _camera;
	NodePath _cam;
	NodePath _aspect2d;
	NodePath _render2d;
	NodePath _pixel2d;

	float _a2d_top;
	float _a2d_bottom;
	float _a2d_left;
	float _a2d_right;

	NodePath _a2d_background;
	NodePath _a2d_top_center;
	NodePath _a2d_top_center_ns;
	NodePath _a2d_bottom_center;
	NodePath _a2d_bottom_center_ns;
	NodePath _a2d_left_center;
	NodePath _a2d_left_center_ns;
	NodePath _a2d_right_center;
	NodePath _a2d_right_center_ns;

	NodePath _a2d_top_left;
	NodePath _a2d_top_left_ns;
	NodePath _a2d_top_right;
	NodePath _a2d_top_right_ns;
	NodePath _a2d_bottom_left;
	NodePath _a2d_bottom_left_ns;
	NodePath _a2d_bottom_right;
	NodePath _a2d_bottom_right_ns;

	NodePath _dataroot;
	NodePathCollection _mouse_and_keyboard;
	NodePathCollection _mouse_watcher;
	NodePathCollection _kb_button_thrower;

	WindowProperties _prev_window_props;

	bool _window_foreground;
	bool _window_minimized;
	float _old_aspect_ratio;

protected:
	PT( GenericAsyncTask ) _render_task;
	PT( GenericAsyncTask ) _data_task;
	PT( GenericAsyncTask ) _audio_task;
};