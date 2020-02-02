/**
 * PANDA3D BSP LIBRARY
 *
 * Copyright (c) Brian Lach <brianlach72@gmail.com>
 * All rights reserved.
 *
 * @file dt_send.h
 * @author Brian Lach
 * @date January 25, 2020
 */

#pragma once

#include "pandabase.h"
#include <pvector.h>
#include <datagram.h>

#include "config_bsp.h"

class SendProp;
class SendTable;

typedef void( *SendProxyFn )( SendProp *, void *, Datagram & );
typedef void( *SendTableProxyFn )( SendProp *, unsigned char *, SendTable *, Datagram & );

enum
{
	SENDFLAGS_NONE,
};

class SendTable;

class EXPCL_PANDABSP SendProp
{
public:
	SendProp( const char *propname, size_t offset, size_t varsize, int bits = 0, SendProxyFn proxy = nullptr, int flags = 0 ) :
		_prop_name( propname ),
		_offset( offset ),
		_varsize( varsize ),
		_bits( bits ),
		_proxy( proxy ),
		_flags( flags ),
		_send_table( nullptr ),
		_send_table_proxy( nullptr )
	{
	}

	INLINE const char *get_prop_name() const
	{
		return _prop_name;
	}

	INLINE size_t get_offset() const
	{
		return _offset;
	}

	INLINE size_t get_varsize() const
	{
		return _varsize;
	}

	INLINE SendProxyFn get_proxy() const
	{
		return _proxy;
	}

	INLINE int get_bits() const
	{
		return _bits;
	}

	INLINE int get_flags() const
	{
		return _flags;
	}

	INLINE void set_send_table( SendTable *table )
	{
		_send_table = table;
	}

	INLINE SendTable *get_send_table() const
	{
		return _send_table;
	}

	INLINE void set_send_table_proxy( SendTableProxyFn proxy )
	{
		_send_table_proxy = proxy;
	}

	INLINE SendTableProxyFn get_send_table_proxy() const
	{
		return _send_table_proxy;
	}

private:
	const char *_prop_name;
	SendTable *_send_table;
	SendTableProxyFn _send_table_proxy;
	size_t _offset;
	size_t _varsize;
	SendProxyFn _proxy;
	int _flags;
	int _bits;
};

class EXPCL_PANDABSP SendTable
{
public:
	SendTable( const char *tablename = "" ) :
		_table_name( tablename ),
		_num_props( 0 )
	{
	}

	INLINE void set_table_name( const char *name )
	{
		_table_name = name;
	}

	INLINE void set_props( const pvector<SendProp> &props )
	{
		_props = props;
		_num_props = props.size();
	}

	INLINE void add_prop( const SendProp &prop )
	{
		_props.push_back( prop );
		_num_props++;
	}

	INLINE void insert_prop( const SendProp &prop )
	{
		_props.insert( _props.begin(), prop );
		_num_props++;
	}

	INLINE const char *get_table_name() const
	{
		return _table_name;
	}

	INLINE pvector<SendProp> &get_send_props()
	{
		return _props;
	}

	INLINE int get_num_props() const
	{
		return _num_props;
	}

private:
	const char *_table_name;
	pvector<SendProp> _props;
	int _num_props;
};

#define BEGIN_SEND_TABLE(classname, tablename) \
BEGIN_SEND_TABLE_INTERNAL(classname, tablename) \
INHERIT_SEND_TABLE(classname) \
OPEN_SEND_TABLE()

#define BEGIN_SEND_TABLE_NOBASE(classname, tablename) \
BEGIN_SEND_TABLE_INTERNAL(classname, tablename) \
OPEN_SEND_TABLE()

#define BEGIN_SEND_TABLE_INTERNAL(classname, tablename)	\
template <typename T>	\
int server_class_init( T * );	\
namespace tablename		\
{				\
	struct ignored;		\
}\
template <>\
int server_class_init<tablename::ignored>( tablename::ignored * );\
namespace tablename \
{ \
	int send_table_init = server_class_init((tablename::ignored *)NULL); \
} \
template <> \
int server_class_init<tablename::ignored>( tablename::ignored * ) \
{ \
	typedef classname current_send_dt_class; \
	SendTable &send_table = classname::get_server_class_s()->get_send_table(); \
	send_table.set_table_name(#tablename);

#define INHERIT_SEND_TABLE(classname) \
	SendTable &base_send_table = classname::BaseClass::get_server_class_s()->get_send_table(); \
	for (size_t i = 0; i < base_send_table.get_num_props(); i++) \
	{ \
		send_table.add_prop(base_send_table.get_prop(i)); \
	}

#define OPEN_SEND_TABLE() \
	static pvector<SendProp> send_props = {

#define END_SEND_TABLE() \
}; \
for (size_t i = 0; i < send_props.size(); i++) \
{ \
	send_table.add_prop(send_props[i]); \
} \
return 1; \
}

#ifdef offsetof
#undef offsetof
#define offsetof( s, m ) ( size_t ) & ( ( (s *)0 )->m )
#endif

#define SENDINFO(varname) \
#varname, offsetof(current_send_dt_class, varname), sizeof(((current_send_dt_class *) 0)->varname)

EXPCL_PANDABSP void SendProxy_Int8( SendProp *prop, void *value, Datagram &dg );
EXPCL_PANDABSP void SendProxy_Int16( SendProp *prop, void *value, Datagram &dg );
EXPCL_PANDABSP void SendProxy_Int32( SendProp *prop, void *value, Datagram &dg );
EXPCL_PANDABSP void SendProxy_Int64( SendProp *prop, void *value, Datagram &dg );

EXPCL_PANDABSP void SendProxy_Uint8( SendProp *prop, void *value, Datagram &dg );
EXPCL_PANDABSP void SendProxy_Uint16( SendProp *prop, void *value, Datagram &dg );
EXPCL_PANDABSP void SendProxy_Uint32( SendProp *prop, void *value, Datagram &dg );
EXPCL_PANDABSP void SendProxy_Uint64( SendProp *prop, void *value, Datagram &dg );

EXPCL_PANDABSP void SendProxy_Float32( SendProp *prop, void *value, Datagram &dg );
EXPCL_PANDABSP void SendProxy_Float64( SendProp *prop, void *value, Datagram &dg );

EXPCL_PANDABSP void SendProxy_String( SendProp *prop, void *data, Datagram &dg );
EXPCL_PANDABSP void SendProxy_CString( SendProp *prop, void *data, Datagram &dg );

EXPCL_PANDABSP void SendTableProxy( SendProp *prop, unsigned char *object, SendTable *table, Datagram &dg );

EXPCL_PANDABSP SendProp SendPropInt( const char *varname, size_t offset, size_t varsize, int bits = 32, int flags = SENDFLAGS_NONE );
EXPCL_PANDABSP SendProp SendPropUint( const char *varname, size_t offset, size_t varsize, int bits = 32, int flags = SENDFLAGS_NONE );
EXPCL_PANDABSP SendProp SendPropFloat( const char *varname, size_t offset, size_t varsize, int bits = 32, int flags = SENDFLAGS_NONE );
EXPCL_PANDABSP SendProp SendPropString( const char *varname, size_t offset, size_t varsize, int flags = SENDFLAGS_NONE );
EXPCL_PANDABSP SendProp SendPropCString( const char *varname, size_t offset, size_t varsize, int flags = SENDFLAGS_NONE );
EXPCL_PANDABSP SendProp SendPropDataTable( const char *varname, SendTable *table );
EXPCL_PANDABSP SendProp SendPropVec3( const char *varname, size_t offset, size_t varsize, int flags = SENDFLAGS_NONE );
EXPCL_PANDABSP SendProp SendPropVec4( const char *varname, size_t offset, size_t varsize, int flags = SENDFLAGS_NONE );
EXPCL_PANDABSP SendProp SendPropVec2( const char *varname, size_t offset, size_t varsize, int flags = SENDFLAGS_NONE );

#define SendPropEntnum SendPropUint
