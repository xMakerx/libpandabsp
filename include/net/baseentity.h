/**
 * PANDA3D BSP LIBRARY
 *
 * Copyright (c) Brian Lach <brianlach72@gmail.com>
 * All rights reserved.
 *
 * @file baseentity.h
 * @author Brian Lach
 * @date January 24, 2020
 */

#ifndef BASEENTITY_H_
#define BASEENTITY_H_

#include <typedReferenceCount.h>

#include "config_bsp.h"
#include "entityshared.h"
#include "networkvar.h"
#include "server_class.h"
#include <aa_luse.h>

#define MAX_CHANGED_OFFSETS	20

class EXPCL_PANDABSP BaseEntity : public TypedReferenceCount
{
	DECLARE_CLASS( BaseEntity, TypedReferenceCount );
	DECLARE_SERVERCLASS();
public:
	BaseEntity();

	INLINE entid_t get_entnum() const
	{
		return _entnum;
	}

	virtual void precache();
	virtual void spawn();
	virtual void despawn();

	virtual void init( entid_t entnum );

	void network_state_changed();
	void network_state_changed( void *ptr );

	bool is_property_changed( SendProp *prop );

	INLINE unsigned short *get_changed_offsets()
	{
		return _changed_offsets;
	}

	INLINE unsigned short get_change_offset( int i )
	{
		return _changed_offsets[i];
	}

	INLINE int get_num_changed_offsets() const
	{
		return _num_changed_offsets;
	}

	INLINE bool is_entity_fully_changed() const
	{
		return get_num_changed_offsets() >= MAX_CHANGED_OFFSETS;
	}

	INLINE void reset_changed_offsets()
	{
		_num_changed_offsets = 0;
	}

	INLINE void set_owner_client_id( int id )
	{
		_owner_client_id = id;
	}

	INLINE int get_owner_client_id() const
	{
		return _owner_client_id;
	}

	INLINE entid_t get_parent_entity() const
	{
		return _parent_entity;
	}

	INLINE void set_parent_entity( entid_t ent )
	{
		_parent_entity = ent;
	}

public:
	NetworkVar( entid_t, _parent_entity );
	NetworkVec3( _origin );
	NetworkVec3( _angles );
	NetworkVec3( _scale );
	NetworkVar( int, _owner_client_id );

	entid_t _entnum;

	unsigned short _changed_offsets[MAX_CHANGED_OFFSETS];
	int _num_changed_offsets;

public:
	static pmap<std::string, PT( BaseEntity )> _entity_to_class;
};

#define LINK_ENTITY_TO_CLASS(hammer_name, classname) \
static int link_##hammer_name##_to_##classname##(void *) \
{ \
	BaseEntity::_entity_to_class[#hammer_name] = new classname; \
	return 1; \
} \
static int __##classname##__##hammer_name##__init = link_##hammer_name##_to_##classname##(NULL);

EXPCL_PANDABSP PT( BaseEntity ) CreateEntityByName( const std::string &name );

#endif // BASEENTITY_H_
