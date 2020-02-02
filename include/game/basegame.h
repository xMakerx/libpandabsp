#pragma once

#include "game/basegame_shared.h"
#include "net/server.h"

/**
 * This is the server-side game framework.
 * It serves to initialize, contain, and manage all game/engine systems
 * related to the server.
 */
class EXPCL_PANDABSP BaseGame : public BaseGameShared
{
public:
	BaseGame();

	INLINE Server *get_server() const
	{
		return _server;
	}

	virtual void load_bsp_level( const Filename &path, bool is_transition = false );

	virtual bool startup();
	virtual void setup_bsp();
	virtual void do_frame();

protected:
	virtual void load_cfg_files();

public:
	PT( Server ) _server;
};