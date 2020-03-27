#pragma once

#include "globalvars_shared.h"
#include "config_clientdll.h"

class C_BaseGame;

class CClientGlobalVars : public CBaseGlobalVars
{
public:
	CClientGlobalVars( bool is_client ) :
		CBaseGlobalVars( is_client ),
		game( nullptr )
	{
	
	}
public:
	C_BaseGame *game;
};

extern EXPORT_CLIENT_DLL CClientGlobalVars *g_globals;