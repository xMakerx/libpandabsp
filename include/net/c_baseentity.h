/**
 * PANDA3D BSP LIBRARY
 *
 * Copyright (c) Brian Lach <brianlach72@gmail.com>
 * All rights reserved.
 *
 * @file c_baseentity.h
 * @author Brian Lach
 * @date January 25, 2020
 */

#ifndef C_BASEENTITY_H_
#define C_BASEENTITY_H_

#include <typedReferenceCount.h>

#include "config_bsp.h"
#include "entityshared.h"
#include "client_class.h"

#include <aa_luse.h>
#include <nodePath.h>

class EXPCL_PANDABSP C_BaseEntity : public TypedReferenceCount
{
	DECLARE_CLASS( C_BaseEntity, TypedReferenceCount );
	DECLARE_CLIENTCLASS();

public:
	C_BaseEntity();

	INLINE entid_t get_entnum() const
	{
		return _entnum;
	}

	virtual void init( entid_t entnum );
	virtual void precache();
	virtual void spawn();
	virtual void despawn();

	INLINE int get_owner_client_id() const
	{
		return _owner_client_id;
	}

	bool is_owner() const;

	INLINE NodePath get_node_path() const
	{
		return _np;
	}

	INLINE entid_t get_parent_entity() const
	{
		return _parent_entity;
	}

private:
	entid_t _entnum;

	entid_t _parent_entity;

	LVector3 _origin;
	LVector3 _angles;
	LVector3 _scale;

	int _owner_client_id;

	NodePath _np;

public:
	static pmap<std::string, PT( C_BaseEntity )> _networkname_to_class;
};

#endif // BASEENTITY_H_
