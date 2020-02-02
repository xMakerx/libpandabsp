/**
 * PANDA3D BSP LIBRARY
 *
 * Copyright (c) Brian Lach <brianlach72@gmail.com>
 * All rights reserved.
 *
 * @file client_class.h
 * @author Brian Lach
 * @date January 25, 2020
 */

#pragma once

#include "pandabase.h"
#include "dt_recv.h"

// -------------------------------------------
// Client class
// -------------------------------------------

class EXPCL_PANDABSP ClientClass
{
public:
	ClientClass( const char *network_name ) :
		_network_name( network_name ),
		_class_id( 0 )
	{
		_client_class_list.push_back( this );
	}

	INLINE void set_recv_table( const RecvTable &table )
	{
		_recv_table = table;
	}

	INLINE RecvTable &get_recv_table()
	{
		return _recv_table;
	}

	INLINE void set_class_id( int class_id )
	{
		_class_id = class_id;
	}

	INLINE int get_class_id() const
	{
		return _class_id;
	}

private:
	RecvTable _recv_table;
	int _class_id;
	const char *_network_name;

public:
	static pvector<ClientClass *> _client_class_list;
};

// This can be used to give all datatables access to protected and private
// members of the class.
#define ALLOW_DATATABLES_PRIVATE_ACCESS() \
	template <typename T>             \
	friend int client_class_init( T* );

#define DECLARE_CLIENTCLASS_NOBASE ALLOW_DATATABLES_PRIVATE_ACCESS

#define DECLARE_CLIENTCLASS()			\
private:					\
	static ClientClass _client_class;	\
public:						\
	static ClientClass *get_client_class_s()\
	{					\
		return &_client_class;		\
	}					\
	virtual ClientClass *get_client_class() \
	{ \
		return &_client_class; \
	} \
	virtual PT( C_BaseEntity ) make_new() \
	{ \
		return new MyClass; \
	} \
	DECLARE_CLIENTCLASS_NOBASE()

#define IMPLEMENT_CLIENTCLASS(classname, networkname)		\
IMPLEMENT_CLASS(classname)					\
ClientClass classname::_client_class(#networkname);		\
static int link_networkname_to_class() \
{ \
	C_BaseEntity::_networkname_to_class[#networkname] = new classname; \
        return 1; \
} \
static int ___ = link_networkname_to_class();

#define IMPLEMENT_CLIENTCLASS_RT(classname, tablename, networkname)	\
IMPLEMENT_CLIENTCLASS(classname, networkname)				\
BEGIN_RECV_TABLE(classname, tablename)

#define IMPLEMENT_CLIENTCLASS_RT_NOBASE(classname, tablename, networkname)	\
IMPLEMENT_CLIENTCLASS(classname, networkname)	\
BEGIN_RECV_TABLE_NOBASE(classname, tablename)