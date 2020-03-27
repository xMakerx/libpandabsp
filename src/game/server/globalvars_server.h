#pragma once

#include "globalvars_shared.h"
#include "config_serverdll.h"

class BaseGame;

class CServerGlobalVars : public CBaseGlobalVars
{
public:
	CServerGlobalVars( bool is_client ) :
		CBaseGlobalVars( is_client ),
		game( nullptr )
	{
	}

public:
	BaseGame *game;
};

extern EXPORT_SERVER_DLL CServerGlobalVars *g_globals;