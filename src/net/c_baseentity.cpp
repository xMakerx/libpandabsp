/**
 * PANDA3D BSP LIBRARY
 *
 * Copyright (c) Brian Lach <brianlach72@gmail.com>
 * All rights reserved.
 *
 * @file c_baseentity.cpp
 * @author Brian Lach
 * @date January 25, 2020
 */

#include "net/c_baseentity.h"
#include "net/c_client.h"

#include <modelRoot.h>

pmap<std::string, PT( C_BaseEntity )> C_BaseEntity::_networkname_to_class;

C_BaseEntity::C_BaseEntity() :
	_entnum( 0 ),
	_owner_client_id( -1 ),
	_np( new ModelRoot( "entity" ) )
{
}

bool C_BaseEntity::is_owner() const
{
	return _owner_client_id == g_client->get_my_client_id();
}

void C_BaseEntity::init( entid_t entnum )
{
	_entnum = entnum;
}

void RecvProxy_ParentEntity( RecvProp *prop, void *object, void *out, DatagramIterator &dgi )
{
	RecvProxy_Int32( prop, object, out, dgi );

	C_BaseEntity *ent = (C_BaseEntity *)object;
	ent->get_node_path().reparent_to(
		g_client->find_entity_by_id( ent->get_parent_entity() )->get_node_path() );
}

IMPLEMENT_CLIENTCLASS_RT_NOBASE( C_BaseEntity, DT_BaseEntity, BaseEntity )
	RecvPropInt( RECVINFO( _owner_client_id ) ),
	RecvPropVec3( RECVINFO( _origin ) ),
	RecvPropVec3( RECVINFO( _angles ) ),
	RecvPropVec3( RECVINFO( _scale ) ),
	RecvPropEntnum( RECVINFO( _parent_entity ), RecvProxy_ParentEntity )
END_RECV_TABLE()