#include "game/c_basegame.h"
#include "game/physics_utils.h"
#include "net/messages.h"

#include "shader_generator.h"
#include "shader_vertexlitgeneric.h"
#include "shader_lightmappedgeneric.h"
#include "shader_unlitgeneric.h"
#include "shader_unlitnomat.h"
#include "shader_csmrender.h"
#include "shader_skybox.h"
#include "shader_decalmodulate.h"

#include <mouseAndKeyboard.h>
#include <buttonThrower.h>
#include <camera.h>
#include <modelRoot.h>

#include <pgTop.h>

static ConfigVariableInt main_camera_fov( "fov", 70 );

C_BaseGame::C_BaseGame() :
	BaseGameShared(),
	_graphics_engine( GraphicsEngine::get_global_ptr() ),
	_input_mgr( InputDeviceManager::get_global_ptr() ),
	_graphics_pipe( nullptr ),
	_gsg( nullptr ),
	_graphics_window( nullptr ),
	_graphics_pipe_selection( GraphicsPipeSelection::get_global_ptr() ),
	_client( new C_Client ),
	_cam_lens( nullptr ),
	_post_process( nullptr )
{
}

void C_BaseGame::cleanup_bsp_level()
{
}

void C_BaseGame::load_bsp_level( const Filename &path, bool is_transition )
{
	_client->set_client_state( CLIENTSTATE_LOADING );

	BaseGameShared::load_bsp_level( path, is_transition );
	_bsp_loader->do_optimizations();
	NodePathCollection props = _bsp_level.find_all_matches( "**/+BSPProp" );
	for ( int i = 0; i < props.get_num_paths(); i++ )
	{
		create_and_attach_bullet_nodes( props[i] );
	}

	_client->set_client_state( CLIENTSTATE_PLAYING );
}

void C_BaseGame::adjust_window_aspect_ratio( float ratio )
{
	if ( ratio != _old_aspect_ratio )
	{
		_old_aspect_ratio = ratio;

		if ( _cam_lens )
			_cam_lens->set_aspect_ratio( ratio );

		if ( ratio < 1.0f )
		{
			// Window is tall
			_aspect2d.set_scale( 1.0f, ratio, ratio );
			_a2d_top = 1.0f / ratio;
			_a2d_bottom = -1.0f / ratio;
			_a2d_right = 1.0f;
			_a2d_left = -1.0f;
		}
		else
		{
			// Window is wide
			_aspect2d.set_scale( 1.0f / ratio, 1.0f, 1.0f );
			_a2d_top = 1.0f;
			_a2d_bottom = -1.0f;
			_a2d_left = -ratio;
			_a2d_right = ratio;
		}

		// Reposition the aspect2d marker nodes
		_a2d_top_center.set_pos( 0, 0, _a2d_top );
		_a2d_top_center_ns.set_pos( 0, 0, _a2d_top );
		_a2d_bottom_center.set_pos( 0, 0, _a2d_bottom );
		_a2d_bottom_center_ns.set_pos( 0, 0, _a2d_bottom );
		_a2d_left_center.set_pos( _a2d_left, 0, 0 );
		_a2d_left_center_ns.set_pos( _a2d_left, 0, 0 );
		_a2d_right_center.set_pos( _a2d_right, 0, 0 );
		_a2d_right_center_ns.set_pos( _a2d_right, 0, 0 );

		_a2d_top_left.set_pos( _a2d_left, 0, _a2d_top );
		_a2d_top_left_ns.set_pos( _a2d_left, 0, _a2d_top );
		_a2d_top_right.set_pos( _a2d_right, 0, _a2d_top );
		_a2d_top_right_ns.set_pos( _a2d_right, 0, _a2d_top );
		_a2d_bottom_left.set_pos( _a2d_left, 0, _a2d_bottom );
		_a2d_bottom_left_ns.set_pos( _a2d_left, 0, _a2d_bottom );
		_a2d_bottom_right.set_pos( _a2d_right, 0, _a2d_bottom );
		_a2d_bottom_right_ns.set_pos( _a2d_right, 0, _a2d_bottom );
	}
}

void C_BaseGame::window_event()
{
	WindowProperties props = _graphics_window->get_properties();
	if ( props != _prev_window_props )
	{
		_prev_window_props = props;

		if ( !props.get_open() )
		{
			std::cout << "User closed main window." << std::endl;
			shutdown();
			return;
		}

		if ( props.get_foreground() && !_window_foreground )
			_window_foreground = true;
		else if ( !props.get_foreground() && _window_foreground )
			_window_foreground = false;

		if ( props.get_minimized() && !_window_minimized )
			_window_minimized = true;
		else if ( !props.get_minimized() && _window_minimized )
			_window_minimized = false;

		adjust_window_aspect_ratio( get_aspect_ratio() );

		if ( _graphics_window->has_size() && _graphics_window->get_sbs_left_y_size() != 0 )
		{
			_pixel2d.set_scale( 2.0f / _graphics_window->get_sbs_left_x_size(),
					    1.0f,
					    2.0f / _graphics_window->get_sbs_left_y_size() );
		}
		else
		{
			LVector2i size = get_size();
			if ( size[0] > 0 && size[1] > 0 )
			{
				_pixel2d.set_scale( 2.0f / size[0], 1.0f, 2.0f / size[1] );
			}
		}
	}
}

void C_BaseGame::handle_window_event( const Event *e, void *data )
{
	C_BaseGame *self = (C_BaseGame *)data;
	self->window_event();
}

void C_BaseGame::do_frame()
{
	_client->tick();
	BaseGameShared::do_frame();
}

bool C_BaseGame::startup()
{
	if ( !BaseGameShared::startup() )
	{
		return false;
	}

	_event_mgr->add_hook( "window-event", handle_window_event, this );

	return true;
}

void C_BaseGame::shutdown()
{
	_event_mgr->remove_hook( "window-event", handle_window_event, this );

	BaseGameShared::shutdown();

	_audio3d = nullptr;
	_music_manager->shutdown();
	_sfx_manager->shutdown();
	_music_manager = nullptr;
	_sfx_manager = nullptr;

	_render_task->remove();
	_render_task = nullptr;
	_data_task->remove();
	_data_task = nullptr;
	_audio_task->remove();
	_audio_task = nullptr;

	_graphics_window->remove_all_display_regions();
	_graphics_engine->remove_all_windows();
	_graphics_window = nullptr;
	_gsg = nullptr;
	_graphics_pipe = nullptr;

	_mouse_and_keyboard.clear();
	_mouse_watcher.clear();
	_kb_button_thrower.clear();

	_dataroot.remove_node();

	_shader_generator = nullptr;
	_post_process->shutdown();
	_post_process = nullptr;

	_a2d_top_left.remove_node();
	_a2d_top_left_ns.remove_node();
	_a2d_top_right.remove_node();
	_a2d_top_right_ns.remove_node();
	_a2d_top_center.remove_node();
	_a2d_top_center_ns.remove_node();
	_a2d_bottom_center.remove_node();
	_a2d_bottom_center_ns.remove_node();
	_a2d_bottom_left.remove_node();
	_a2d_bottom_left_ns.remove_node();
	_a2d_bottom_right.remove_node();
	_a2d_bottom_right_ns.remove_node();
	_a2d_left_center.remove_node();
	_a2d_left_center_ns.remove_node();
	_a2d_right_center.remove_node();
	_a2d_right_center_ns.remove_node();
	_a2d_background.remove_node();

	_pixel2d.remove_node();
	_aspect2d.remove_node();
	_render2d.remove_node();

	_cam.remove_node();
	_camera.remove_node();
}

void C_BaseGame::setup_dgraph()
{
	//
	// Data graph
	//

	_dataroot = NodePath( "dataroot" );

	for ( int i = 0; i < _graphics_window->get_num_input_devices(); i++ )
	{
		std::ostringstream ss;
		ss << "MouseAndKeyboard " << i;
		NodePath mak = _dataroot.attach_new_node( new MouseAndKeyboard( _graphics_window, i, ss.str() ) );
		_mouse_and_keyboard.add_path( mak );

		// Watch the mouse
		PT( MouseWatcher ) mw = new MouseWatcher( "watcher" );
		ModifierButtons mmods = mw->get_modifier_buttons();
		mmods.add_button( KeyboardButton::shift() );
		mmods.add_button( KeyboardButton::control() );
		mmods.add_button( KeyboardButton::alt() );
		mmods.add_button( KeyboardButton::meta() );
		mw->set_modifier_buttons( mmods );
		NodePath mwnp = mak.attach_new_node( mw );
		_mouse_watcher.add_path( mwnp );

		// Watch for keyboard buttons
		PT( ButtonThrower ) bt = new ButtonThrower( "kb-events" );
		ModifierButtons mods;
		mods.add_button( KeyboardButton::shift() );
		mods.add_button( KeyboardButton::control() );
		mods.add_button( KeyboardButton::alt() );
		mods.add_button( KeyboardButton::meta() );
		bt->set_modifier_buttons( mods );
		NodePath btnp = mak.attach_new_node( bt );
		_kb_button_thrower.add_path( btnp );
	}
}

void C_BaseGame::setup_shaders()
{
	//
	// Shaders
	//

	_shader_generator = new BSPShaderGenerator( _graphics_window, _gsg, _camera, _render );
	_gsg->set_shader_generator( _shader_generator );
	_shader_generator->start_update();
	_shader_generator->add_shader( new VertexLitGenericSpec );
	_shader_generator->add_shader( new UnlitGenericSpec );
	_shader_generator->add_shader( new UnlitNoMatSpec );
	_shader_generator->add_shader( new LightmappedGenericSpec );
	_shader_generator->add_shader( new CSMRenderSpec );
	_shader_generator->add_shader( new SkyBoxSpec );
	_shader_generator->add_shader( new DecalModulateSpec );

	setup_postprocess();
}

void C_BaseGame::setup_postprocess()
{
	_post_process = new GamePostProcess;
	_post_process->startup( _graphics_window );
	_post_process->add_camera( _cam );

	_post_process->_hdr_enabled = ConfigVariableBool( "mat_hdr", true );
	_post_process->_bloom_enabled = ConfigVariableBool( "mat_bloom", true );
	_post_process->_fxaa_enabled = ConfigVariableBool( "mat_fxaa", true );

	_post_process->setup();
}

void C_BaseGame::setup_bsp()
{
	BaseGameShared::setup_bsp();

	_bsp_loader->set_win( _graphics_window );
	_bsp_loader->set_camera( _camera );
}

void C_BaseGame::setup_scene()
{
	BaseGameShared::setup_scene();

	// Enable shader generation on all of the main scenes
	if ( _gsg->get_supports_basic_shaders() && _gsg->get_supports_glsl() )
	{
		_render.set_shader_auto();
		_render2d.set_shader_auto();
	}
	else
	{
		// I don't know how this could be possible.
		nassert_raise( "GLSL shaders unsupported by graphics driver" );
		return;
	}

	_render2d = NodePath( "render2d" );
	_render2d.set_depth_test( false );
	_render2d.set_depth_write( false );
	_render2d.set_material_off( true );
	_render2d.set_two_sided( true );

	_aspect2d = _render2d.attach_new_node( new PGTop( "aspect2d" ) );
	float aspect_ratio = get_aspect_ratio();
	_aspect2d.set_scale( 1.0f / aspect_ratio, 1.0f, 1.0f );

	_a2d_background = _aspect2d.attach_new_node( "a2dBackground" );

	_a2d_top = 1.0f;
	_a2d_bottom = -1.0f;
	_a2d_left = -aspect_ratio;
	_a2d_right = aspect_ratio;

	_a2d_top_center = _aspect2d.attach_new_node( "a2dTopCenter" );
	_a2d_top_center_ns = _aspect2d.attach_new_node( "a2dTopCenterNS" );
	_a2d_bottom_center = _aspect2d.attach_new_node( "a2dBottomCenter" );
	_a2d_bottom_center_ns = _aspect2d.attach_new_node( "a2dBottomCenterNS" );
	_a2d_left_center = _aspect2d.attach_new_node( "a2dLeftCenter" );
	_a2d_left_center_ns = _aspect2d.attach_new_node( "a2dLeftCenterNS" );
	_a2d_right_center = _aspect2d.attach_new_node( "a2dRightCenter" );
	_a2d_right_center_ns = _aspect2d.attach_new_node( "a2dRightCenterNS" );

	_a2d_top_left = _aspect2d.attach_new_node( "a2dTopLeft" );
	_a2d_top_left_ns = _aspect2d.attach_new_node( "a2dTopLeftNS" );
	_a2d_top_right = _aspect2d.attach_new_node( "a2dTopRight" );
	_a2d_top_right_ns = _aspect2d.attach_new_node( "a2dTopRightNS" );
	_a2d_bottom_left = _aspect2d.attach_new_node( "a2dBottomLeft" );
	_a2d_bottom_left_ns = _aspect2d.attach_new_node( "a2dBottomLeftNS" );
	_a2d_bottom_right = _aspect2d.attach_new_node( "a2dBottomRight" );
	_a2d_bottom_right_ns = _aspect2d.attach_new_node( "a2dBottomRightNS" );

	_a2d_top_center.set_pos( 0, 0, _a2d_top );
	_a2d_top_center_ns.set_pos( 0, 0, _a2d_top );
	_a2d_bottom_center.set_pos( 0, 0, _a2d_bottom );
	_a2d_bottom_center_ns.set_pos( 0, 0, _a2d_bottom );
	_a2d_left_center.set_pos( _a2d_left, 0, 0 );
	_a2d_left_center_ns.set_pos( _a2d_left, 0, 0 );
	_a2d_right_center.set_pos( _a2d_right, 0, 0 );
	_a2d_right_center_ns.set_pos( _a2d_right, 0, 0 );

	_a2d_top_left.set_pos( _a2d_left, 0, _a2d_top );
	_a2d_top_left_ns.set_pos( _a2d_left, 0, _a2d_top );
	_a2d_top_right.set_pos( _a2d_right, 0, _a2d_top );
	_a2d_top_right_ns.set_pos( _a2d_right, 0, _a2d_top );
	_a2d_bottom_left.set_pos( _a2d_left, 0, _a2d_bottom );
	_a2d_bottom_left_ns.set_pos( _a2d_left, 0, _a2d_bottom );
	_a2d_bottom_right.set_pos( _a2d_right, 0, _a2d_bottom );
	_a2d_bottom_right_ns.set_pos( _a2d_right, 0, _a2d_bottom );

	_pixel2d = _render2d.attach_new_node( new PGTop( "pixel2d" ) );
	_pixel2d.set_pos( -1, 0, 1 );
	LVector2i size = get_size();
	if ( size[0] > 0 && size[1] > 0 )
		_pixel2d.set_scale( 2.0f / size[0], 1.0f, 2.0f / size[1] );
}

void C_BaseGame::setup_camera()
{
	//
	// Camera
	//

	_camera = NodePath( new ModelRoot( "camera" ) );
	_camera.reparent_to( _render );
	PT( DisplayRegion ) region = _graphics_window->make_display_region();
	PT( PerspectiveLens ) lens = new PerspectiveLens;
	lens->set_min_fov( main_camera_fov / ( 4. / 3. ) );
	PT( Camera ) camera = new Camera( "MainCamera", lens );
	_cam = NodePath( camera );
	_cam.reparent_to( _camera );
	region->set_camera( _cam );
	_cam_lens = lens;

	camera->set_camera_mask( CAMERA_MAIN );
}

void C_BaseGame::setup_rendering()
{
	//
	// Rendering
	//

	_graphics_pipe = _graphics_pipe_selection->make_default_pipe();
	if ( !_graphics_pipe )
	{
		nassert_raise( "Could not create a graphics pipe!" );
		return;
	}

	_gsg = _graphics_pipe->make_callback_gsg( _graphics_engine );
	nassertv( _gsg != nullptr );

	int window_flags = GraphicsPipe::BF_fb_props_optional | GraphicsPipe::BF_require_window;
	_graphics_window = DCAST(
		GraphicsWindow,
		_graphics_engine->make_output(
			_graphics_pipe, "MainWindow", 0, FrameBufferProperties::get_default(),
			WindowProperties::get_default(), window_flags, _gsg ) );
	if ( !_graphics_window )
	{
		nassert_raise( "Could not open main window!" );
		return;
	}

	_graphics_engine->open_windows();

	_graphics_window->set_clear_color_active( false );
	_graphics_window->set_clear_stencil_active( false );
	_graphics_window->set_clear_depth_active( true );
}

void C_BaseGame::setup_tasks()
{
	BaseGameShared::setup_tasks();

	_data_task = new GenericAsyncTask( "dataLoop", data_task, this );
	_data_task->set_sort( -50 );
	_task_manager->add( _data_task );

	_render_task = new GenericAsyncTask( "igLoop", render_task, this );
	_render_task->set_sort( 50 );
	_task_manager->add( _render_task );

	_audio_task = new GenericAsyncTask( "audioLoop", audio_task, this );
	_audio_task->set_sort( 60 );
	_task_manager->add( _audio_task );
}

void C_BaseGame::setup_audio()
{
	//
	// Audio
	//

	_sfx_manager = AudioManager::create_AudioManager();
	_music_manager = AudioManager::create_AudioManager();
	_music_manager->set_concurrent_sound_limit( 1 );
	_audio3d = new Audio3DManager( _sfx_manager, _camera, _render, 40 );
	_audio3d->set_drop_off_factor( 0.15f );
	_audio3d->set_doppler_factor( 0.15f );
}

void C_BaseGame::load_cfg_files()
{
	BaseGameShared::load_cfg_files();
	load_cfg_file( "cfg/client.cfg" );
}

DEFINE_TASK_FUNC( C_BaseGame::render_task )
{
	C_BaseGame *self = (C_BaseGame *)data;
	self->_graphics_engine->render_frame();
	return AsyncTask::DS_cont;
}

DEFINE_TASK_FUNC( C_BaseGame::data_task )
{
	C_BaseGame *self = (C_BaseGame *)data;
	self->_dgtrav.traverse( self->_dataroot.node() );
	return AsyncTask::DS_cont;
}

DEFINE_TASK_FUNC( C_BaseGame::audio_task )
{
	C_BaseGame *self = (C_BaseGame *)data;
	self->_sfx_manager->update();
	self->_music_manager->update();
	return AsyncTask::DS_cont;
}