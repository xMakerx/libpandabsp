/**
 * PANDA3D BSP LIBRARY
 *
 * Copyright (c) Brian Lach <brianlach72@gmail.com>
 * All rights reserved.
 *
 * @file dt_recv.h
 * @author Brian Lach
 * @date January 25, 2020
 */

#pragma once

#include <datagramIterator.h>
#include "config_bsp.h"

// ------------------------------------------------------------------
// Recv Code
// ------------------------------------------------------------------

enum
{
	RECVFLAGS_NONE,
};

class RecvProp;

typedef void( *RecvProxyFn )( RecvProp *prop, void *object, void *out, DatagramIterator &dgi );

class EXPCL_PANDABSP RecvProp
{
public:
	RecvProp( const char *propname, size_t offset, size_t varsize, RecvProxyFn proxy = nullptr, int flags = 0 ) :
		_prop_name( propname ),
		_offset( offset ),
		_varsize( varsize ),
		_proxy( proxy ),
		_flags( flags ),
		_string_size( 0 )
	{
	}

	INLINE void set_string_size( size_t size )
	{
		_string_size = size;
	}

	INLINE const char *get_prop_name() const
	{
		return _prop_name;
	}

	INLINE size_t get_offset() const
	{
		return _offset;
	}

	INLINE RecvProxyFn get_proxy() const
	{
		return _proxy;
	}

	INLINE size_t get_varsize() const
	{
		return _varsize;
	}

	INLINE size_t get_string_size() const
	{
		return _string_size;
	}

private:
	size_t _offset;
	size_t _varsize;
	size_t _string_size;
	RecvProxyFn _proxy;
	int _flags;
	const char *_prop_name;
};

class EXPCL_PANDABSP RecvTable
{
public:
	RecvTable( const char *table_name = "", RecvProp *props = nullptr, int num_props = 0 ) :
		_table_name( table_name ),
		_num_props( num_props )
	{
	}

	INLINE void set_table_name( const char *name )
	{
		_table_name = name;
	}

	INLINE void set_props( const pvector<RecvProp> &props )
	{
		_props = props;
		_num_props = props.size();
	}

	INLINE void add_prop( const RecvProp &prop )
	{
		_props.push_back( prop );
		_num_props++;
	}

	INLINE void insert_prop( const RecvProp &prop )
	{
		_props.insert( _props.begin(), prop );
		_num_props++;
	}

	INLINE const char *get_table_name() const
	{
		return _table_name;
	}

	INLINE pvector<RecvProp> &get_recv_props()
	{
		return _props;
	}

	INLINE int get_num_props() const
	{
		return _num_props;
	}

	RecvProp *find_recv_prop( const std::string &name );

private:
	const char *_table_name;
	pvector<RecvProp> _props;
	int _num_props;
};

// -------------------------------------------
// Recv table stuff
// -------------------------------------------

#ifdef offsetof
#undef offsetof
#define offsetof( s, m ) ( size_t ) & ( ( (s *)0 )->m )
#endif

#define RECVINFO(varname) \
#varname, offsetof(current_recv_dt_class, varname), sizeof(((current_recv_dt_class *)0)->varname)

#define BEGIN_RECV_TABLE(classname, tablename)		\
BEGIN_RECV_TABLE_INTERNAL(classname, tablename)		\
INHERIT_RECV_TABLE(classname)	\
OPEN_RECV_TABLE()

#define BEGIN_RECV_TABLE_NOBASE(classname, tablename)	\
BEGIN_RECV_TABLE_INTERNAL(classname, tablename)	\
OPEN_RECV_TABLE()

#define BEGIN_RECV_TABLE_INTERNAL(classname, tablename)		\
template <typename T>						\
int client_class_init( T * );					\
namespace tablename						\
{								\
	struct ignored;						\
}								\
template <>							\
int client_class_init<tablename::ignored>( tablename::ignored * );	\
namespace tablename						\
{								\
	int recv_table_init = client_class_init((tablename::ignored *)NULL);	\
}								\
template <>							\
int client_class_init<tablename::ignored>( tablename::ignored * )	\
{								\
	typedef classname current_recv_dt_class;		\
	RecvTable &recv_table = classname::get_client_class_s()->get_recv_table(); \
	recv_table.set_table_name(#tablename);

#define INHERIT_RECV_TABLE(classname) \
	RecvTable &base_recv_table = classname::BaseClass::get_client_class_s()->get_recv_table(); \
	for (size_t i = 0; i < base_recv_table.get_num_props(); i++) \
	{ \
		recv_table.add_prop(base_recv_table.get_prop(i)); \
	}

#define OPEN_RECV_TABLE() \
	static pvector<RecvProp> recv_props = {			

#define END_RECV_TABLE() \
};							\
for (size_t i = 0; i < recv_props.size(); i++) \
{ \
	recv_table.add_prop(recv_props[i]); \
} \
return 1;\
}

EXPCL_PANDABSP void RecvProxy_Int8( RecvProp *prop, void *object, void *out, DatagramIterator &dgi );
EXPCL_PANDABSP void RecvProxy_Int16( RecvProp *prop, void *object, void *out, DatagramIterator &dgi );
EXPCL_PANDABSP void RecvProxy_Int32( RecvProp *prop, void *object, void *out, DatagramIterator &dgi );
EXPCL_PANDABSP void RecvProxy_Int64( RecvProp *prop, void *object, void *out, DatagramIterator &dgi );

EXPCL_PANDABSP void RecvProxy_Uint8( RecvProp *prop, void *object, void *out, DatagramIterator &dgi );
EXPCL_PANDABSP void RecvProxy_Uint16( RecvProp *prop, void *object, void *out, DatagramIterator &dgi );
EXPCL_PANDABSP void RecvProxy_Uint32( RecvProp *prop, void *object, void *out, DatagramIterator &dgi );
EXPCL_PANDABSP void RecvProxy_Uint64( RecvProp *prop, void *object, void *out, DatagramIterator &dgi );

EXPCL_PANDABSP void RecvProxy_Float32( RecvProp *prop, void *object, void *out, DatagramIterator &dgi );
EXPCL_PANDABSP void RecvProxy_Float64( RecvProp *prop, void *object, void *out, DatagramIterator &dgi );

EXPCL_PANDABSP void RecvProxy_String( RecvProp *prop, void *object, void *out, DatagramIterator &dgi );
EXPCL_PANDABSP void RecvProxy_CString( RecvProp *prop, void *object, void *out, DatagramIterator &dgi );

EXPCL_PANDABSP RecvProp RecvPropInt( const char *varname, size_t offset, size_t varsize, RecvProxyFn proxy = nullptr, int bits = 32, int flags = RECVFLAGS_NONE );
EXPCL_PANDABSP RecvProp RecvPropUint( const char *varname, size_t offset, size_t varsize, RecvProxyFn proxy = nullptr, int bits = 32, int flags = RECVFLAGS_NONE );
EXPCL_PANDABSP RecvProp RecvPropFloat( const char *varname, size_t offset, size_t varsize, RecvProxyFn proxy = nullptr, int bits = 32, int flags = RECVFLAGS_NONE );
EXPCL_PANDABSP RecvProp RecvPropString( const char *varname, size_t offset, size_t varsize, RecvProxyFn proxy = nullptr, int flags = RECVFLAGS_NONE );
EXPCL_PANDABSP RecvProp RecvPropCString( const char *varname, size_t offset, size_t varsize, RecvProxyFn proxy = nullptr, int flags = RECVFLAGS_NONE );
EXPCL_PANDABSP RecvProp RecvPropVec4( const char *varname, size_t offset, size_t varsize, RecvProxyFn proxy = nullptr, int flags = RECVFLAGS_NONE );
EXPCL_PANDABSP RecvProp RecvPropVec3( const char *varname, size_t offset, size_t varsize, RecvProxyFn proxy = nullptr, int flags = RECVFLAGS_NONE );
EXPCL_PANDABSP RecvProp RecvPropVec2( const char *varname, size_t offset, size_t varsize, RecvProxyFn proxy = nullptr, int flags = RECVFLAGS_NONE );

#define RecvPropEntnum RecvPropUint