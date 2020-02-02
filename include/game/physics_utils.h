#pragma once

#include <nodePath.h>
#include <vector_string.h>
#include <bulletAllHitsRayResult.h>

struct RayTestClosestNotMeResult_t
{
	bool result;
	BulletRayHit hit;
};

pvector<BulletRayHit> ray_test_all_stored( const LPoint3 &from, const LPoint3 &to,
					   const BitMask32 &mask = BitMask32::all_on() );
void ray_test_closest_not_me( RayTestClosestNotMeResult_t &result,
			      const NodePath &me, const LPoint3 &from, const LPoint3 &to,
			      const BitMask32 &mask = BitMask32::all_on() );

void create_and_attach_bullet_nodes( const NodePath &root_node );
void attach_bullet_nodes( const NodePath &root_node );
void remove_bullet_nodes( const NodePath &root_node );
void detach_bullet_nodes( const NodePath &root_node );

void detach_and_remove_bullet_nodes( const NodePath &root_node );

LVector3 get_throw_vector( const LPoint3 &trace_origin, const LVector3 &trace_vector,
			   const LPoint3 &throw_origin, const NodePath &me );

void optimize_phys( const NodePath &root );
void make_bullet_coll_from_panda_coll( const NodePath &root_node, const vector_string &exclusions = vector_string() );