/**
 * PANDA3D BSP LIBRARY
 * Copyright (c) CIO Team. All rights reserved.
 *
 * @file bsploader.cpp
 * @author Brian Lach
 * @date March 27, 2018
 */

#include "bsploader.h"

#include "bsp_trace.h"

#include "bsp_render.h"
#include "bspfile.h"
#include "entity.h"
#include "mathlib.h"
#include "viftokenizer.h"
#include "bsp_material.h"

#include <array>
#include <bitset>
#include <math.h>

#include <asyncTaskManager.h>
#include <eggData.h>
#include <eggPolygon.h>
#include <eggVertexUV.h>
#include <eggWriter.h>
#include <geomNode.h>
#include <load_egg_file.h>
#include <loader.h>
#include <nodePathCollection.h>
#include <pnmFileTypeJPG.h>
#include <pointLight.h>
#include <randomizer.h>
#include <rigidBodyCombiner.h>
#include <texture.h>
#include <texturePool.h>
#include <textureStage.h>
#include <virtualFileSystem.h>
#include <directionalLight.h>
#include <ambientLight.h>
#include <spotlight.h>
#include <fog.h>
#include <lineSegs.h>
#include <nodePath_ext.h>
#include <graphicsEngine.h>
#include <boundingBox.h>
#include <pStatCollector.h>
#include <cullTraverser.h>
#include <cullTraverserData.h>
#include <cullableObject.h>
#include <transformState.h>
#include <cullHandler.h>
#include <modelRoot.h>
#include <lightReMutexHolder.h>
#include <geomVertexData.h>
#include <geomVertexRewriter.h>
#include <sceneGraphReducer.h>
#include <characterJointEffect.h>
#include <orthographicLens.h>
#include <cullBinAttrib.h>
#include <materialAttrib.h>
#include <materialPool.h>

static PStatCollector bsp_build_leaf_geom_collector( "BSP:BuildAcceleratedLeafGeomStructure" );

#define DEFAULT_BRUSH_SHADER "LightmappedGeneric"

INLINE static void flatten_node( const NodePath &node )
{
        // Mimic a flatten strong operation, but do not attempt to combine
        // Geoms, as each world brush face needs to be it's own Geom for effective leaf
        // culling.

        SceneGraphReducer gr;
        gr.apply_attribs( node.node() );
        gr.flatten( node.node(), ~0 );
        gr.make_compatible_state( node.node() );
        gr.collect_vertex_data( node.node(), ~( SceneGraphReducer::CVD_format |
                                                SceneGraphReducer::CVD_name |
                                                SceneGraphReducer::CVD_animation_type ) );
}

PStatCollector bfa_collector( "BSP:BSPFaceAttrib" );

TypeHandle BSPFaceAttrib::_type_handle;
int BSPFaceAttrib::_attrib_slot;

INLINE BSPFaceAttrib::BSPFaceAttrib() :
        RenderAttrib()
{
}

bool BSPFaceAttrib::has_cull_callback() const
{
        return false;
}

INLINE string BSPFaceAttrib::get_material() const
{
        return _material;
}

INLINE int BSPFaceAttrib::get_face_type() const
{
        return _face_type;
}

CPT( RenderAttrib ) BSPFaceAttrib::make( const string &face_material, int face_type )
{
        BSPFaceAttrib *attrib = new BSPFaceAttrib;
        attrib->_material = face_material;
        attrib->_face_type = face_type;
        return return_new( attrib );
}

CPT( RenderAttrib ) BSPFaceAttrib::make_default()
{
        BSPFaceAttrib *attrib = new BSPFaceAttrib;
        attrib->_material = "default";
        attrib->_face_type = FACETYPE_WALL;
        return return_new( attrib );
}

int BSPFaceAttrib::compare_to_impl( const RenderAttrib *other ) const
{
        const BSPFaceAttrib *ta = (const BSPFaceAttrib *)other;

        if ( _face_type != ta->_face_type )
        {
                return _face_type - ta->_face_type;
        }

        return _material.compare( ta->_material );
}

size_t BSPFaceAttrib::get_hash_impl() const
{
        size_t hash = 0;
        hash = string_hash::add_hash( hash, _material );
        hash = int_hash::add_hash( hash, _face_type );
        return hash;
}

//#define VISUALIZE_PLANE_COLORS
//#define EXTRACT_LIGHTMAPS

#define DEFAULT_GAMMA 2.2
#define QRAD_GAMMA 1.8
#define DEFAULT_OVERBRIGHT 1
#define ATTN_FACTOR 0.03

// Due to some imprecision, we will expand the leaf AABBs just a tiny bit
// as a compensation. 1.0 seems like a lot, but it is defined in Hammer space
// where 1 Hammer unit is 0.0625 Panda units.
#define LEAF_NUDGE 1.0

static int extract_modelnum_s( entity_t *ent )
{
        string model = ValueForKey( ent, "model" );
        if ( model[0] == '*' )
        {
                return atoi( model.substr( 1 ).c_str() );
        }
        return -1;
}

PT( GeomNode ) UTIL_make_cube_outline( const LPoint3 &min, const LPoint3 &max,
                                       const LColor &color, PN_stdfloat thickness )
{
        LineSegs lines;
        lines.set_color( color );
        lines.set_thickness( thickness );
        lines.move_to( min );
        lines.draw_to( LVector3( min.get_x(), min.get_y(), max.get_z() ) );
        lines.draw_to( LVector3( min.get_x(), max.get_y(), max.get_z() ) );
        lines.draw_to( LVector3( min.get_x(), max.get_y(), min.get_z() ) );
        lines.draw_to( min );
        lines.draw_to( LVector3( max.get_x(), min.get_y(), min.get_z() ) );
        lines.draw_to( LVector3( max.get_x(), min.get_y(), max.get_z() ) );
        lines.draw_to( LVector3( min.get_x(), min.get_y(), max.get_z() ) );
        lines.move_to( LVector3( max.get_x(), min.get_y(), max.get_z() ) );
        lines.draw_to( max );
        lines.draw_to( LVector3( min.get_x(), max.get_y(), max.get_z() ) );
        lines.move_to( max );
        lines.draw_to( LVector3( max.get_x(), max.get_y(), min.get_z() ) );
        lines.draw_to( LVector3( min.get_x(), max.get_y(), min.get_z() ) );
        lines.move_to( LVector3( max.get_x(), max.get_y(), min.get_z() ) );
        lines.draw_to( LVector3( max.get_x(), min.get_y(), min.get_z() ) );
        return lines.create();
}

void GetParamsFromEnt( entity_t *mapent )
{
}

NotifyCategoryDef( bspfile, "" );

BSPLoader *BSPLoader::_global_ptr = nullptr;

INLINE int BSPLoader::find_leaf( const NodePath &np )
{
        return find_leaf( np.get_pos( _result ) );
}

INLINE int BSPLoader::find_leaf( const LPoint3 &pos )
{
        if ( !_active_level )
        {
                return 0;
        }

        int i = 0;

        // Walk the BSP tree to find the index of the leaf which contains the specified
        // position.
        while ( i >= 0 )
        {
                const dnode_t *node = &_bspdata->dnodes[i];
                const dplane_t *plane = &_bspdata->dplanes[node->planenum];
                float distance = ( plane->normal[0] * pos.get_x() ) +
                        ( plane->normal[1] * pos.get_y() ) +
                        ( plane->normal[2] * pos.get_z() ) - ( plane->dist / PANDA_TO_HAMMER );

                if ( distance >= 0.0 )
                {
                        i = node->children[0];
                }
                else
                {
                        i = node->children[1];
                }

        }

        return ~i;
}

INLINE int BSPLoader::find_node( const LPoint3 &pos )
{
        if ( !_active_level )
        {
                return 0;
        }

        int i = 0;

        while ( true )
        {
                const dnode_t *node = &_bspdata->dnodes[i];
                const dplane_t *plane = &_bspdata->dplanes[node->planenum];
                float distance = ( plane->normal[0] * pos.get_x() ) +
                        ( plane->normal[1] * pos.get_y() ) +
                        ( plane->normal[2] * pos.get_z() ) - plane->dist ;

                int child;
                if ( distance >= 0.0 )
                {
                        child = node->children[0];
                }
                else
                {
                        child = node->children[1];
                }

                if ( child < 0 )
                {
                        // In a leaf. Return the node.
                        return i;
                }

                i = child;
        }

        return i;
}

LTexCoord BSPLoader::get_vertex_uv( texinfo_t *texinfo, dvertex_t *vert, bool lightmap ) const
{
        float *vpos = vert->point;
        LVector3 vert_pos( vpos[0], vpos[1], vpos[2] );

        LVector3 s_vec, t_vec;
        float s_dist, t_dist;
        if ( !lightmap )
        {
                s_vec = LVector3( texinfo->vecs[0][0], texinfo->vecs[0][1], texinfo->vecs[0][2] );
                s_dist = texinfo->vecs[0][3];

                t_vec = LVector3( texinfo->vecs[1][0], texinfo->vecs[1][1], texinfo->vecs[1][2] );
                t_dist = texinfo->vecs[1][3];
        }
        else
        {
                s_vec = LVector3( texinfo->lightmap_vecs[0][0], texinfo->lightmap_vecs[0][1], texinfo->lightmap_vecs[0][2] );
                s_dist = texinfo->lightmap_vecs[0][3];

                t_vec = LVector3( texinfo->lightmap_vecs[1][0], texinfo->lightmap_vecs[1][1], texinfo->lightmap_vecs[1][2] );
                t_dist = texinfo->lightmap_vecs[1][3];
        }
        

        return LTexCoord( s_vec.dot( vert_pos ) + s_dist, t_vec.dot( vert_pos ) + t_dist );
}

LTexCoord get_lightcoords( texinfo_t *texinfo, dvertex_t *vert, dface_t *face, FaceLightmapData *ld )
{
        float *vpos = vert->point;
        LVector3 vec( vpos[0], vpos[1], vpos[2] );

        float s_scale, t_scale;
        float s_offset, t_offset;
        LTexCoord lightcoord;

        bool flipped = ld->faceentry != nullptr && ld->faceentry->flipped;

        int texsize[2];
        texsize[0] = flipped ? face->lightmap_size[1] : face->lightmap_size[0];
        texsize[1] = flipped ? face->lightmap_size[0] : face->lightmap_size[1];
        int texmins[2];
        texmins[0] = flipped ? face->lightmap_mins[1] : face->lightmap_mins[0];
        texmins[1] = flipped ? face->lightmap_mins[0] : face->lightmap_mins[1];        

        if ( ld->faceentry != nullptr )
        {
                s_scale = 1.0 / (float)ld->faceentry->palette_size[0];
                s_offset = (float)ld->faceentry->xshift * s_scale;
        }
        else
        {
                s_scale = 1.0;
                s_offset = 0.0;
        }
        s_scale = texsize[0] * s_scale;

        if ( ld->faceentry != nullptr )
        {
                t_scale = 1.0 / -(float)ld->faceentry->palette_size[1];
                t_offset = (float)ld->faceentry->yshift * t_scale;
        }
        else
        {
                t_scale = -1.0;
                t_offset = 0.0;
        }
        t_scale = texsize[1] * t_scale;        

        lightcoord[0] = DotProduct( vec, texinfo->lightmap_vecs[0] ) +
                texinfo->lightmap_vecs[0][3];
        lightcoord[1] = DotProduct( vec, texinfo->lightmap_vecs[1] ) +
                texinfo->lightmap_vecs[1][3];

        if ( flipped )
        {
                float tmp = lightcoord[1];
                lightcoord[1] = lightcoord[0];
                lightcoord[0] = tmp;
        }

        lightcoord[0] -= texmins[0];
        lightcoord[0] += 0.5;
        lightcoord[0] /= texsize[0];
        
        lightcoord[1] -= texmins[1];
        lightcoord[1] += 0.5;
        lightcoord[1] /= texsize[1];

        lightcoord[0] = s_offset + lightcoord[0] * s_scale;
        lightcoord[1] = t_offset + lightcoord[1] * t_scale;

        return lightcoord;
}

PT( EggVertex ) BSPLoader::make_vertex_ai( EggVertexPool *vpool, EggPolygon *poly, dedge_t *edge, int k )
{
        dvertex_t *vert = &_bspdata->dvertexes[edge->v[k]];
        float *vpos = vert->point;
        PT( EggVertex ) v = new EggVertex;
        v->set_pos( LPoint3d( vpos[0], vpos[1], vpos[2] ) );
        return v;
}

PT( EggVertex ) BSPLoader::make_vertex( EggVertexPool *vpool, EggPolygon *poly,
                                        dedge_t *edge, texinfo_t *texinfo,
                                        dface_t *face, int k, FaceLightmapData *ld,
                                        Texture *tex )
{
        dvertex_t *vert = &_bspdata->dvertexes[edge->v[k]];
        float *vpos = vert->point;
        PT( EggVertex ) v = new EggVertex;
        v->set_pos( LPoint3d( vpos[0], vpos[1], vpos[2] ) );

        // The widths and heights are retrieved from the actual loaded textures that were referenced.
        double df_width = 1.0;
        double df_height = 1.0;
        if ( tex != nullptr )
        {
                df_width = tex->get_orig_file_x_size();
                df_height = tex->get_orig_file_y_size();
        }

        // Texture and lightmap coordinates
        LTexCoord uv = get_vertex_uv( texinfo, vert );
        LTexCoord luv = get_lightcoords( texinfo, vert, face, ld );
        LTexCoordd df_uv( uv.get_x() / df_width, -uv.get_y() / df_height );
        LTexCoordd lm_uv( luv[0], luv[1] );
#if 0
        LTexCoordd lm_uv( 0, 0 );
        if ( ld->faceentry != nullptr )
        {
                // This face has an entry in a lightmap palette.
                // Transform the UVs.

                double midtexs[2];
                midtexs[0] = ld->faceentry->flipped ? ld->midtexs[1] : ld->midtexs[0];
                midtexs[1] = ld->faceentry->flipped ? ld->midtexs[0] : ld->midtexs[1];
                double midpolys[2];
                midpolys[0] = ld->faceentry->flipped ? ld->midpolys[1] : ld->midpolys[0];
                midpolys[1] = ld->faceentry->flipped ? ld->midpolys[0] : ld->midpolys[1];
                double dluv[2];
                dluv[0] = ld->faceentry->flipped ? luv[1] : luv[0];
                dluv[1] = ld->faceentry->flipped ? luv[0] : luv[1];
                int texsize[2];
                texsize[0] = ld->faceentry->flipped ? face->lightmap_size[1] : face->lightmap_size[0];
                texsize[1] = ld->faceentry->flipped ? face->lightmap_size[0] : face->lightmap_size[1];

                lm_uv.set( ( midtexs[0] + ld->faceentry->xshift + ( dluv[0] - midpolys[0] ) ),
                          ( midtexs[1] + ld->faceentry->yshift + ( dluv[1] - midpolys[1] ) ) );

                lm_uv[0] /= ld->faceentry->palette_size[0];
                lm_uv[1] /= -ld->faceentry->palette_size[1];
        }
#endif

        v->set_uv( "basetexture", df_uv );
        v->set_uv( "lightmap", lm_uv );

        return v;
}

PT( Texture ) BSPLoader::try_load_texref( texref_t *tref )
{
        if ( _texref_textures.find( tref ) != _texref_textures.end() )
        {
                return _texref_textures[tref];
        }

        string name = tref->name;

	PT( Texture ) tex = nullptr;

	size_t ext_idx = name.find_last_of( "." );
	string basename = name.substr( 0, ext_idx );
	string alpha_file = basename + "_a.rgb";
        Filename alpha_filename = Filename::from_os_specific( alpha_file );
	if ( VirtualFileSystem::get_global_ptr()->exists( alpha_filename ) )
	{
		// A corresponding alpha file exists for this texture, load that up as well.
                bspfile_cat.info()
                        << "Found corresponding alpha file " << alpha_filename.get_fullpath()
                        << " for texture " << name << "\n";
		tex = TexturePool::load_texture( name, alpha_filename );
	}
	else
	{
		// Just load the texture.
		tex = TexturePool::load_texture( name );
	}

        // Now check for a material file.
        Filename mat_filename = Filename::from_os_specific( basename + ".mat" );
        if ( VirtualFileSystem::get_global_ptr()->exists( mat_filename ) )
        {
                string mat_data = VirtualFileSystem::get_global_ptr()->read_file( mat_filename, true );

                TokenVec toks = tokenizer( mat_data );
                Parser p( toks );
                _texref_materials[tref] = p;
                bspfile_cat.info()
                        << "Found material file for texref " << tref->name << "\n";
        }

        if ( tex != nullptr )
        {
                bspfile_cat.info()
                        << "Loaded texref " << tref->name << "\n";
                _texref_textures[tref] = tex;
                return tex;
        }

        return nullptr;
}

/**
 * Generates the least amount of data possible for geometry on the AI side.
 * Used for generating the navmesh. We don't need to make the faces in the same
 * manner as the client, since all we need are the triangle data.
 */
void BSPLoader::make_faces_ai()
{
        bspfile_cat.info()
                << "Making faces for AI...\n";

        PT( EggData ) data = new EggData;
        PT( EggVertexPool ) vpool = new EggVertexPool( "facevpool" );
        data->add_child( vpool );

        for ( int entnum = 0; entnum < _bspdata->numentities; entnum++ )
        {
                entity_t *ent = _bspdata->entities + entnum;
                const char *classname = ValueForKey( ent, "classname" );

                if ( entnum != 0 &&
                     strncmp( classname, "func_wall", 9 ) &&
                     strncmp( classname, "func_detail", 11 ) &&
                     strncmp( classname, "func_illusionary", 16 ) )
                {
                        continue;
                }

                int modelnum = entnum == 0 ? 0 : extract_modelnum_s( ent );
                if ( modelnum == -1 )
                {
                        continue;
                }

                dmodel_t *mdl = _bspdata->dmodels + modelnum;

                for ( int facenum = mdl->firstface; facenum < mdl->firstface + mdl->numfaces; facenum++ )
                {
                        PT( EggPolygon ) poly = new EggPolygon;
                        data->add_child( poly );
                        dface_t *face = _bspdata->dfaces + facenum;
                        texinfo_t *texinfo = &_bspdata->texinfo[face->texinfo];
                        texref_t *texref = &_bspdata->dtexrefs[texinfo->texref];
                        contents_t contents = GetTextureContents( texref->name );
                        if ( contents == CONTENTS_NULL )
                        {
                                continue;
                        }

                        int last_edge = face->firstedge + face->numedges;
                        int first_edge = face->firstedge;
                        for ( int j = last_edge - 1; j >= first_edge; j-- )
                        {
                                int surf_edge = _bspdata->dsurfedges[j];

                                dedge_t *edge;
                                if ( surf_edge >= 0 )
                                        edge = &_bspdata->dedges[surf_edge];
                                else
                                        edge = &_bspdata->dedges[-surf_edge];

                                if ( surf_edge < 0 )
                                {
                                        for ( int k = 0; k < 2; k++ )
                                        {
                                                PT( EggVertex ) v = make_vertex_ai( vpool, poly, edge, k );
                                                vpool->add_vertex( v );
                                                poly->add_vertex( v );
                                        }
                                }
                                else
                                {
                                        for ( int k = 1; k >= 0; k-- )
                                        {
                                                PT( EggVertex ) v = make_vertex_ai( vpool, poly, edge, k );
                                                vpool->add_vertex( v );
                                                poly->add_vertex( v );
                                        }
                                }
                        }
                }
                
        }

        data->remove_unused_vertices( true );
        data->remove_invalid_primitives( true );

        _result.attach_new_node( load_egg_data( data ) );
        _result.set_scale( 1 / 16.0 );
        _result.clear_model_nodes();
        flatten_node( _result );
}

static PT( Shader ) get_csm_caster_shader()
{
        stringstream vshader;
        vshader << "#version 430;\n";
        vshader << "uniform mat4 p3d_ModelViewProjectionMatrix;\n";
        vshader << "in vec4 p3d_Vertex;\n";
        vshader << "out vec4 l_position;\n";
        vshader << "void main() {\n";
        vshader << "\t gl_Position = p3d_ModelViewProjectionMatrix * p3d_Vertex;\n";
        vshader << "\t l_position = gl_Position;\n";
        vshader << "}\n";

        stringstream pshader;
        pshader << "#version 430\n";
        pshader << "in vec4 l_position;\n";
        pshader << "out vec4 o_color;\n";
        pshader << "void main() {\n";
        pshader << "\t float z = (l_position.z / l_position.w) * 0.5 + 0.5;\n";
        pshader << "\t o_color = vec4(z, z, z, 1.0);\n";
        pshader << "}\n";

        return Shader::make( Shader::SL_GLSL, vshader.str(), pshader.str() );
}

static pvector<string> make_bface_shaders( bool lightmap, bool envmap, float envmap_shininess, bool bumped,
                                           bool normalmap, bool recv_projshadows )
{

        pvector<string> result;

        stringstream vshader;
        vshader << "#version 430\n";
        vshader << "in vec4 p3d_Vertex;\n";
        vshader << "in vec2 p3d_MultiTexCoord0;\n";
        if ( lightmap )
        {
                vshader << "in vec2 p3d_MultiTexCoord1;\n";
        }
        if ( recv_projshadows )
        {
                vshader << "out vec4 l_projshadowcoords;\n";
                vshader << "uniform mat4 trans_model_to_clip_of_shadowcam;\n";
        }
        vshader << "uniform mat4 p3d_ModelViewProjectionMatrix;\n";
        vshader << "in vec3 p3d_Normal;\n";
        if ( envmap )
        {
                vshader << "uniform mat4 p3d_ModelViewMatrix;\n";
                vshader << "uniform mat4 p3d_ViewMatrixInverse;\n";
                vshader << "uniform mat4 tpose_view_to_model;\n";
                vshader << "in vec4 mspos_view;\n";
                //vshader << "out vec3 refl_vec;\n";
                vshader << "out vec2 envmap_coords;\n";
        }
        if ( bumped && normalmap )
        {
                vshader << "in vec3 p3d_Tangent;\n";
                vshader << "in vec3 p3d_Binormal;\n";
                vshader << "out vec3 tangent;out vec3 binormal;out vec3 vnormal;\n";
        }
        vshader << "out vec2 diff_texcoord;\n";
        if ( lightmap )
        {
                vshader << "out vec2 lm_texcoord;\n";
        }
        vshader << "void main() {\n";
        vshader << "\t diff_texcoord = p3d_MultiTexCoord0;\n";
        if ( lightmap )
        {
                vshader << "\t lm_texcoord = p3d_MultiTexCoord1;\n";
        }
        vshader << "\t gl_Position = p3d_ModelViewProjectionMatrix * p3d_Vertex;\n";
        if ( envmap )
        {
                vshader << "vec4 eye_normal;\neye_normal.xyz = normalize(mat3(tpose_view_to_model) * p3d_Normal);\n"
                        "eye_normal.w = 0.0;\n";
                vshader << "\t vec3 eye_vec = (p3d_ModelViewMatrix * p3d_Vertex).xyz;//mspos_view.xyz - p3d_Vertex.xyz);\n";
                vshader << "\t vec3 r = reflect(eye_vec, eye_normal.xyz);\n";
                vshader << "\t r = vec3(p3d_ViewMatrixInverse * vec4(r, 0.0));\n";
                vshader << "\t float m = 2.0 * sqrt(pow(r.x, 2.0) + pow(r.y, 2.0) + pow(r.z + 1.0, 2.0));\n";
                vshader << "\t envmap_coords = r.xy / m + 0.5;\n";
        }
        if ( bumped && normalmap )
        {
                vshader << "\t tangent = p3d_Tangent;\n";
                vshader << "\t binormal = p3d_Binormal;\n";
                vshader << "\t vnormal = p3d_Normal;\n";
        }
        if ( recv_projshadows )
        {
                vshader << "vec4 camclip = trans_model_to_clip_of_shadowcam * p3d_Vertex;\n";
                vshader << "l_projshadowcoords = camclip * vec4(0.5,0.5,0.5,1.0) + camclip.w * vec4(0.5,0.5,0.5,0.0);\n";
        }
        vshader << "}\n";

        stringstream pshader;
        pshader << "#version 430\n";
        pshader << "uniform sampler2D p3d_Texture0;\n";
        pshader << "in vec2 diff_texcoord;\n";
        if ( recv_projshadows )
        {
                pshader << "uniform sampler2D shadowtex;\n";
                pshader << "uniform sampler2D shadowdepth;\n";
                pshader << "in vec4 l_projshadowcoords;\n";
        }
        if ( lightmap )
        {
                pshader << "uniform sampler2D p3d_Texture1;\n";
                pshader << "in vec2 lm_texcoord;\n";
        }
        if ( envmap )
        {
                pshader << "uniform sampler2D envmap_texture;\n";
                //pshader << "in vec3 refl_vec;\n";
                pshader << "in vec2 envmap_coords;\n";
        }
        if ( normalmap && bumped )
        {
                pshader << "uniform sampler2D normalmap_texture;\n";
                pshader << "uniform sampler2D lightmap_colors0;uniform sampler2D lightmap_colors1;uniform sampler2D lightmap_colors2;\n";
                pshader << "uniform vec3 bump_basis0;uniform vec3 bump_basis1;uniform vec3 bump_basis2;\n";
                pshader << "in vec3 tangent; in vec3 binormal;in vec3 vnormal;\n";
        }
        pshader << "out vec4 frag_color;\n";

        pshader << "void main() {\n";
        pshader << "\t frag_color = texture2D(p3d_Texture0, diff_texcoord);\n";
        if ( normalmap && bumped )
        {
                
                pshader << "\t mat3 bump_trans = mat3(bump_basis0, bump_basis1, bump_basis2);\n";
                pshader << "\t vec3 norm_col = texture2D( normalmap_texture, diff_texcoord ).rgb;\n";
                pshader << "\t vec3 normal = norm_col * 2.0 - 1.0;\n";
                //pshader << "\t normal *= bump_trans;\n";// * mat3(tangent, binormal, vnormal);\n";
                //pshader << "\t normal *= vnormal.z;\n";
                //pshader << "\t normal += tangent * vnormal.x;\n";
                //pshader << "\t normal += binormal * vnormal.y;\n";
                pshader << "\t normal = normalize( normal );\n";
        }
        if ( envmap )
        {
                pshader << "\t frag_color.rgb += (texture2D(envmap_texture, envmap_coords).rgb * " << envmap_shininess << ");\n";
        }
        if ( lightmap )
        {
                if ( normalmap && bumped )
                {
                        pshader << "vec3 dp = vec3(0, 0, 0);\n";
                        pshader << "dp.x = clamp(dot(normal, bump_basis0), 0, 1);\n";
                        pshader << "dp.y = clamp(dot(normal, bump_basis1), 0, 1);\n";
                        pshader << "dp.z = clamp(dot(normal, bump_basis2), 0, 1);\n";
                        pshader << "dp *= dp;\n";

                        pshader << "vec3 lmcolor0 = texture2D(lightmap_colors0, lm_texcoord).rgb;\n";
                        pshader << "vec3 lmcolor1 = texture2D(lightmap_colors1, lm_texcoord).rgb;\n";
                        pshader << "vec3 lmcolor2 = texture2D(lightmap_colors2, lm_texcoord).rgb;\n";

                        pshader << "float sum = dot(dp, vec3(1.0, 1.0, 1.0));\n";

                        pshader << "vec3 final_lightmap = dp.x*lmcolor0 + dp.y*lmcolor1 + dp.z*lmcolor2;\n";
                        pshader << "final_lightmap *= 1.0 / sum;\n";

                        pshader << "\t frag_color.rgb *= final_lightmap;\n";
                }
                else
                {
                        pshader << "\t frag_color.rgb *= texture2D(p3d_Texture1, lm_texcoord).rgb;\n";
                }
        }
        if ( recv_projshadows )
        {
                //pshader << "vec4 proj = l_projshadowcoords / l_projshadowcoords.w;\n";
                //pshader << "float mapval = texture2D(shadowdepth, proj.xy).r;\n";
                //pshader << "if (mapval > proj.z) { frag_color.rgb *= texture2DProj(shadowtex, l_projshadowcoords).rgb; }\n";

                pshader << "frag_color.rgb *= texture2DProj(shadowtex, l_projshadowcoords).rgb;\n";
        }
        //pshader << "frag_color.rgb = clamp(frag_color.rgb, 0.0, 1.0);\n";
        pshader << "}\n";

        result.push_back( vshader.str() );
        result.push_back( pshader.str() );

        return result;
}

void BSPLoader::make_faces()
{
        bspfile_cat.info()
                << "Making faces...\n";

        // build table of per-face beginning index into vertnormalindices
        int face_vertnormalindices[MAX_MAP_FACES];
        memset( face_vertnormalindices, -1, sizeof( int ) * MAX_MAP_FACES );
        int normal_index = 0;
        for ( int i = 0; i < _bspdata->numfaces; i++ )
        {
                face_vertnormalindices[i] = normal_index;
                normal_index += _bspdata->dfaces[i].numedges;
        }

        // In BSP files, models are brushes that have been grouped together to be used as an entity.
        // We can group all of the face GeomNodes of the model to a root node.
        for ( int modelnum = 0; modelnum < _bspdata->nummodels; modelnum++ )
        {
                dmodel_t *model = &_bspdata->dmodels[modelnum];
                int firstface = model->firstface;
                int numfaces = model->numfaces;
                float *florigin = model->origin;
                float *flmins = model->mins;
                float *flmaxs = model->maxs;
                LPoint3 origin( florigin[0], florigin[1], florigin[2] );
                LPoint3 mins( flmins[0], flmins[1], flmins[2] );
                LPoint3 maxs( flmaxs[0], flmaxs[1], flmaxs[2] );

                stringstream name;
                name << "model-" << modelnum;
                PT( BSPModel ) bspmdl = new BSPModel( name.str() );
                NodePath modelroot = _result.attach_new_node( bspmdl );
                modelroot.set_shader_auto();

                _model_origins[modelroot] = ( ( ( mins + maxs ) / 2.0 ) + origin ) / 16.0;

                for ( int facenum = firstface; facenum < firstface + numfaces; facenum++ )
                {
                        PT( EggData ) data = new EggData;
                        PT( EggVertexPool ) vpool = new EggVertexPool( "facevpool" );
                        data->add_child( vpool );

                        dface_t *face = &_bspdata->dfaces[facenum];

                        PT( EggPolygon ) poly = new EggPolygon;
                        data->add_child( poly );

                        texinfo_t *texinfo = &_bspdata->texinfo[face->texinfo];

                        texref_t *texref = &_bspdata->dtexrefs[texinfo->texref];
                        contents_t contents = GetTextureContents( texref->name );
                        if ( contents == CONTENTS_SKY )
                        {
                                // We don't render sky brushes.
                                continue;
                        }

                        bool skip = false;

                        string envmap = "phase_14/maps/envmap001a.png";
                        float envmap_shininess = -1;
                        bool mat_lightmapped = true;
                        bool mat_normalmap = false;
                        string normalmapfile = "";

                        PT( Texture ) tex = try_load_texref( texref );
                        string texture_name = texref->name;
                        string material = "default";
                        string shader = DEFAULT_BRUSH_SHADER;
                        if ( _texref_materials.find( texref ) != _texref_materials.end() )
                        {
                                bspfile_cat.info()
                                        << "Material properties for " << texref->name << ":\n";
                                Parser *p = &_texref_materials[texref];
                                Object obj = p->_base_objects[0];
                                shader = obj.name;
                                vector<Property> props = p->get_properties( obj );
                                for ( size_t i = 0; i < props.size(); i++ )
                                {
                                        Property *prop = &props[i];
                                        if ( prop->name == "$surfaceprop" )
                                        {
                                                material = prop->value;
                                        }
                                        else if ( prop->name == "$envmap" )
                                        {
                                                envmap = prop->value;
                                        }
                                        else if ( prop->name == "$envmap_shininess" )
                                        {
                                                envmap_shininess = atof( prop->value.c_str() );
                                        }
                                        else if ( prop->name == "$lightmapped" )
                                        {
                                                mat_lightmapped = (bool)atoi( prop->value.c_str() );
                                        }
                                        else if ( prop->name == "$normalmap" )
                                        {
                                                mat_normalmap = true;
                                                normalmapfile = prop->value;
                                        }
                                        
                                        bspfile_cat.info()
                                                << "\t" << prop->name << "\t:\t" << prop->value << "\n";
                                }

                        }
                        transform( texture_name.begin(), texture_name.end(), texture_name.begin(), tolower );

                        bool has_lighting = ( face->lightofs != -1 && _want_lightmaps ) && !skip && mat_lightmapped;
                        bool has_texture = !skip;


                        LightmapPaletteDirectory::LightmapFacePaletteEntry *lmfaceentry = _lightmap_dir.face_index[facenum];

                        /* ************* FROM P3RAD ************* */

                        FaceLightmapData ld;

                        ld.mins[0] = ld.mins[1] = 999999.0;
                        ld.maxs[0] = ld.maxs[1] = -99999.0;

                        for ( int i = 0; i < face->numedges; i++ )
                        {
                                int edge_idx = _bspdata->dsurfedges[face->firstedge + i];
                                dedge_t *edge;
                                dvertex_t *vert;
                                if ( edge_idx >= 0 )
                                {
                                        edge = &_bspdata->dedges[edge_idx];
                                        vert = &_bspdata->dvertexes[edge->v[0]];
                                }
                                else
                                {
                                        edge = &_bspdata->dedges[-edge_idx];
                                        vert = &_bspdata->dvertexes[edge->v[1]];
                                }

                                LTexCoord uv = get_vertex_uv( texinfo, vert, true );

                                if ( uv.get_x() < ld.mins[0] )
                                        ld.mins[0] = uv.get_x();
                                else if ( uv.get_x() > ld.maxs[0] )
                                        ld.maxs[0] = uv.get_x();

                                if ( uv.get_y() < ld.mins[1] )
                                        ld.mins[1] = uv.get_y();
                                else if ( uv.get_y() > ld.maxs[1] )
                                        ld.maxs[1] = uv.get_y();
                        }

                        ld.texmins[0] = floor( ld.mins[0] );
                        ld.texmins[1] = floor( ld.mins[1] );
                        ld.texmaxs[0] = ceil( ld.maxs[0] );
                        ld.texmaxs[1] = ceil( ld.maxs[1] );

                        ld.texsize[0] = floor( (double)( ld.texmaxs[0] - ld.texmins[0] ) + 1 );
                        ld.texsize[1] = floor( (double)( ld.texmaxs[1] - ld.texmins[1] ) + 1 );

                        ld.midpolys[0] = ( ld.mins[0] + ld.maxs[0] ) / 2.0;
                        ld.midpolys[1] = ( ld.mins[1] + ld.maxs[1] ) / 2.0;
                        ld.midtexs[0] = ld.texsize[0] / 2.0;
                        ld.midtexs[1] = ld.texsize[1] / 2.0;

                        ld.faceentry = lmfaceentry;

                        LNormald poly_normal( 0 );

                        int last_edge = face->firstedge + face->numedges;
                        int first_edge = face->firstedge;
                        for ( int j = last_edge - 1; j >= first_edge; j-- )
                        {
                                LNormald normal( 0 );
                                if ( face_vertnormalindices[facenum] != -1 )
                                {
                                        int vert_normal_idx = face_vertnormalindices[facenum] + ( j - first_edge );
                                        vec3_t normalf_v;
                                        VectorCopy( _bspdata->vertnormals[_bspdata->vertnormalindices[vert_normal_idx]].point, normalf_v );
                                        normal = LNormald( normalf_v[0], normalf_v[1], normalf_v[2] );
                                }

                                poly_normal += normal;

                                int surf_edge = _bspdata->dsurfedges[j];

                                dedge_t *edge;
                                if ( surf_edge >= 0 )
                                        edge = &_bspdata->dedges[surf_edge];
                                else
                                        edge = &_bspdata->dedges[-surf_edge];

                                if ( surf_edge < 0 )
                                {
                                        for ( int k = 0; k < 2; k++ )
                                        {
                                                PT( EggVertex ) v = make_vertex( vpool, poly, edge, texinfo,
                                                                                 face, k, &ld, tex );
                                                v->set_normal( normal );
                                                vpool->add_vertex( v );
                                                poly->add_vertex( v );
                                        }
                                }
                                else
                                {
                                        for ( int k = 1; k >= 0; k-- )
                                        {
                                                PT( EggVertex ) v = make_vertex( vpool, poly, edge, texinfo,
                                                                                 face, k, &ld, tex );
                                                v->set_normal( normal );
                                                vpool->add_vertex( v );
                                                poly->add_vertex( v );
                                        }
                                }
                        }

                        data->remove_unused_vertices( true );
                        data->remove_invalid_primitives( true );

                        poly_normal /= face->numedges;

                        int face_type = BSPFaceAttrib::FACETYPE_WALL;

                        bool recv_projshadows = false;
                        if ( poly_normal.almost_equal( LNormald::up(), 0.5 ) )
                        {
                                // A polygon facing upwards could be considered a ground.
                                // Give it the ground bin.
                                poly->set_bin( "ground" );
                                poly->set_draw_order( 18 );
                                face_type = BSPFaceAttrib::FACETYPE_FLOOR;
                                if ( _want_shadows )
                                {
                                        recv_projshadows = true;
                                }
                        }

                        data->recompute_tangent_binormal_auto();

                        NodePath faceroot = _result.attach_new_node( load_egg_data( data ) );

                        if ( has_texture )
                        {
                                faceroot.set_texture( TextureStages::get_basetexture(), tex );
                        }

                        if ( has_lighting )
                        {
                                if ( face->bumped_lightmap && mat_normalmap )
                                {
                                        for ( int n = 0; n < NUM_BUMP_VECTS; n++ )
                                        {
                                                faceroot.set_texture( TextureStages::get_bumped_lightmap( n ),
                                                                      lmfaceentry->palette->palette_tex[n + 1] );
                                        }
                                }
                                else
                                {
                                        faceroot.set_texture( TextureStages::get_lightmap(),
                                                              lmfaceentry->palette->palette_tex[0] );
                                }
                        }
                        if ( mat_normalmap )
                        {
                                PT( Texture ) nmtex = TexturePool::load_texture( normalmapfile );
                                faceroot.set_texture( TextureStages::get_normalmap(), nmtex );
                        }

                        faceroot.wrt_reparent_to( modelroot );
                        if ( Texture::has_alpha( tex->get_format() ) )
                        {
                                faceroot.set_transparency( TransparencyAttrib::M_alpha, 1 );
                        }

                        PT( BSPMaterial ) face_mat = new BSPMaterial;
                        face_mat->set_shader( shader );

                        if ( envmap_shininess > 0.0 )
                        {
                                PT( Texture ) etex = TexturePool::load_texture( envmap );
                                etex->set_wrap_u( SamplerState::WM_repeat );
                                etex->set_wrap_v( SamplerState::WM_repeat );
                                faceroot.set_texture( TextureStages::get_spheremap(), etex );
                                face_mat->set_shininess( envmap_shininess );
                        }

                        faceroot.set_material( Materials::get( face_mat ) );

                        NodePathCollection gn_npc = faceroot.find_all_matches( "**/+GeomNode" );
                        for ( int i = 0; i < gn_npc.get_num_paths(); i++ )
                        {
                                NodePath gnnp = gn_npc.get_path( i );
                                PT( GeomNode ) gn = DCAST( GeomNode, gnnp.node() );
                                for ( int j = 0; j < gn->get_num_geoms(); j++ )
                                {
                                        PT( Geom ) geom = gn->modify_geom( j );
                                        geom->set_bounds_type( BoundingVolume::BT_box );
                                        CPT( RenderAttrib ) bca = BSPFaceAttrib::make( material, face_type );
                                        CPT( RenderState ) old_state = gn->get_geom_state( j );
                                        CPT( RenderState ) new_state = old_state->add_attrib( bca, 1 );

                                        gn->set_geom( j, geom );
                                        gn->set_geom_state( j, new_state );
                                }
                        }

                        if ( skip )
                        {
                                faceroot.hide();
                        }
                }

                _model_roots.push_back( modelroot );
        }

        bspfile_cat.info()
                << "Finished making faces.\n";
}

LColor color_from_rgb_scalar( vec_t *color )
{
        double scalar = color[3];
        return LColor( color[0] * scalar / 255.0,
                       color[1] * scalar / 255.0,
                       color[2] * scalar / 255.0,
                       1.0 );
}

LColor color_from_value( const string &value, bool scale )
{
        double r, g, b, s;
        sscanf( value.c_str(), "%lf %lf %lf %lf", &r, &g, &b, &s );

        if ( scale )
        {
                r *= s / 255.0;
                g *= s / 255.0;
                b *= s / 255.0;
        }

        r /= 255.0;
        g /= 255.0;
        b /= 255.0;

        LColor col( r, g, b, 1.0 );

        return col;
}

INLINE int BSPLoader::extract_modelnum( int entnum )
{
        return extract_modelnum_s( _bspdata->entities + entnum );
}

INLINE void BSPLoader::get_model_bounds( int modelnum, LPoint3 &mins, LPoint3 &maxs )
{
        dmodel_t *mdl = _bspdata->dmodels + modelnum;
        VectorCopy( mdl->mins, mins );
        VectorCopy( mdl->maxs, maxs );
        mins /= 16.0;
        maxs /= 16.0;
}

void BuildGeomNodes_r( PandaNode *node, pvector<PT( GeomNode )> &list )
{
        if ( node->is_of_type( GeomNode::get_class_type() ) )
        {
                list.push_back( DCAST( GeomNode, node ) );
        }

        for ( int i = 0; i < node->get_num_children(); i++ )
        {
                BuildGeomNodes_r( node->get_child( i ), list );
        }
}

pvector<PT( GeomNode )> BuildGeomNodes( const NodePath &root )
{
        pvector<PT( GeomNode )> list;

        BuildGeomNodes_r( root.node(), list );

        return list;
}

struct VDataDef
{
        CPT( GeomVertexData ) vdata;
        CPT( Geom ) geom;
};

struct GNWG_result
{
        PT( GeomNode ) geomnode;
        int geomidx;
};

INLINE GNWG_result get_geomnode_with_geom( const NodePath &root, CPT( Geom ) geom )
{
        GNWG_result result;
        result.geomnode = nullptr;

        NodePathCollection npc = root.find_all_matches( "**/+GeomNode" );
        for ( int i = 0; i < npc.get_num_paths(); i++ )
        {
                PT( GeomNode ) gn = DCAST( GeomNode, npc.get_path( i ).node() );
                for ( int j = 0; j < gn->get_num_geoms(); j++ )
                {
                        CPT( Geom ) g = gn->get_geom( j );
                        if ( geom == g )
                        {
                                result.geomnode = gn;
                                result.geomidx = j;
                        }
                }
        }

        return result;
}

static void clear_model_nodes_below( const NodePath &top )
{
        NodePathCollection npc = top.find_all_matches( "**/+ModelNode" );
        for ( int i = 0; i < npc.get_num_paths(); i++ )
        {
                if ( npc[i] == top )
                {
                        continue;
                }
                npc[i].clear_effects();
                DCAST( ModelNode, npc[i].node() )->set_preserve_transform( ModelNode::PT_drop_node );
        }
}

void BSPLoader::load_static_props()
{
        SimpleHashMap<int, NodePath, int_hash> leaf2props;

        for ( size_t propnum = 0; propnum < _bspdata->dstaticprops.size(); propnum++ )
        {
                dstaticprop_t *prop = &_bspdata->dstaticprops[propnum];

                PT( BSPProp ) propnode = new BSPProp( prop->name );
                propnode->set_preserve_transform( ModelNode::PT_local );
                NodePath propnp = _result.attach_new_node( propnode );
                PT( PandaNode ) proproot = Loader::get_global_ptr()->load_sync( prop->name );
                if ( proproot == nullptr )
                {
                        bspfile_cat.warning()
                                << "Could not load static prop " << prop->name << "\n";
                        continue;
                }

                NodePath propmdl( proproot );
                LPoint3 pos;
                VectorCopy( prop->pos, pos );
                LVector3 hpr;
                VectorCopy( prop->hpr, hpr );
                LVector3 scale;
                VectorCopy( prop->scale, scale );
                propnp.set_pos( pos / 16.0 );
                propnp.set_hpr( hpr[1] - 90, hpr[0], hpr[2] );
                propnp.set_scale( scale );
                propmdl.reparent_to( propnp );
                propmdl.clear_model_nodes();
                propmdl.flatten_light();

                entity_t *lightsrc = nullptr;
                LColor lightsrc_col;
                if ( prop->lightsrc != -1 )
                {
                        lightsrc = _bspdata->entities + prop->lightsrc;
                        lightsrc_col = color_from_value( ValueForKey( lightsrc, "_light" ) );
                }

                if ( prop->first_vertex_data != -1 &&
                     ( prop->flags & STATICPROPFLAGS_STATICLIGHTING ) != 0 &&
                     ( prop->flags & STATICPROPFLAGS_DYNAMICLIGHTING ) == 0 )
                {
                        // prop has pre computed per vertex lighting
                        pvector<VDataDef> vdatadefs;

                        // this is actually the most annoying code in the world,
                        // since panda3d makes geoms and all its data const pointers

                        // build a list of unique GeomVertexDatas with a list of each Geom that shares it.
                        // it better be in the same order as the dstaticprop vertex datas
                        pvector<PT( GeomNode )> geomnodes = BuildGeomNodes( propmdl );
                        for ( size_t i = 0; i < geomnodes.size(); i++ )
                        {
                                PT( GeomNode ) gn = geomnodes[i];
                                for ( int j = 0; j < gn->get_num_geoms(); j++ )
                                {
                                        CPT( Geom ) geom = gn->get_geom( j );
                                        CPT( GeomVertexData ) vdata = geom->get_vertex_data();

                                        VDataDef def;
                                        def.vdata = vdata;
                                        def.geom = geom;
                                        vdatadefs.push_back( def );
                                }
                        }

                        // check for a mismatch
                        if ( vdatadefs.size() != prop->num_vertex_datas )
                        {
                                bspfile_cat.warning()
                                        << "Static prop " << prop->name << " vertex data count mismatch. "
                                        << "Will appear fullbright. "
                                        << "Has the model been changed since last map compile?\n";
                                bspfile_cat.warning()
                                        << "Loaded model has " << vdatadefs.size() << " unique vertex datas,\n"
                                        << "dstaticprop has " << prop->num_vertex_datas << "\n";
                                continue;
                        }

                        // now modulate the vertex colors to apply the lighting
                        for ( int i = 0; i < prop->num_vertex_datas; i++ )
                        {
                                dstaticpropvertexdata_t *dvdata = &_bspdata->dstaticpropvertexdatas[prop->first_vertex_data + i];
                                VDataDef *def = &vdatadefs[i];

                                PT( GeomVertexData ) mod_vdata = new GeomVertexData( *def->vdata );
                                if ( !mod_vdata->has_column( InternalName::get_color() ) )
                                {
                                        PT( GeomVertexFormat ) format = new GeomVertexFormat( *mod_vdata->get_format() );
                                        PT( GeomVertexArrayFormat ) array = format->modify_array( 0 );
                                        array->add_column( InternalName::get_color(), 4, GeomEnums::NT_uint8, GeomEnums::C_color );
                                        format->set_array( 0, array );
                                        mod_vdata->set_format( GeomVertexFormat::register_format( format ) );
                                }
                                GeomVertexRewriter color_mod( mod_vdata, InternalName::get_color() );

                                if ( dvdata->num_lighting_samples != mod_vdata->get_num_rows() )
                                {
                                        bspfile_cat.warning()
                                                << "For static prop " << prop->name << ":\n"
                                                << "number of lighting samples does not match number of vertices in vdata\n"
                                                << "number of lighting samples: " << dvdata->num_lighting_samples << "\n"
                                                << "number of vertices: " << mod_vdata->get_num_rows() << "\n";
                                        continue;
                                }

                                for ( int j = 0; j < dvdata->num_lighting_samples; j++ )
                                {
                                        colorrgbexp32_t *sample = &_bspdata->staticproplighting[dvdata->first_lighting_sample + j];
                                        LRGBColor vtx_rgb = color_shift_pixel( sample, _gamma );
                                        LColorf vtx_color( vtx_rgb[0], vtx_rgb[1], vtx_rgb[2], 1.0 );

                                        // get the current vertex color and multiply it by our lighting sample
                                        color_mod.set_row( j );
                                        LColorf curr_color( color_mod.get_data4f() );
                                        color_mod.set_row( j );
                                        curr_color.componentwise_mult( vtx_color );
                                        // now apply it to the modified vertex data
                                        color_mod.set_data4f( curr_color );
                                }

                                // apply the modified vertex data to each geom that shares this vertex data
                                CPT( Geom ) geom = def->geom;
                                GNWG_result res = get_geomnode_with_geom( propmdl, geom );
                                PT( Geom ) mod_geom = res.geomnode->modify_geom( res.geomidx );

                                //if ( lightsrc != nullptr && res.geomnode->get_name() == "__lightsource__" )
                                //{
                                //        bspfile_cat.info()
                                //                << "Applying color " << lightsrc_col << " to vertices on __lightsource__\n";
                                //        for ( int j = 0; j < mod_vdata->get_num_rows(); j++ )
                                //        {
                                //                color_mod.set_row( j );
                                //                color_mod.set_data4f( lightsrc_col );
                                //        }
                                //}

                                if ( res.geomnode->get_name() == "__lightsource__" )
                                {
                                        continue;
                                }
#ifdef CIO
                                else
                                {
                                        // game specific code, yuck
                                        //
                                        // don't apply vertex lighting to shadow models

                                        bool shadow_skip = false;
 
                                        for ( int j = 0; j < res.geomnode->get_num_geoms(); j++ )
                                        {
                                                const RenderState *state = res.geomnode->get_geom_state( j );
                                                const TextureAttrib *tattr;
                                                if ( state->get_attrib( tattr ) )
                                                {
                                                        if ( tattr->get_num_on_stages() == 0 )
                                                        {
                                                                continue;
                                                        }
                                                        Texture *tex = tattr->get_on_texture( tattr->get_on_stage( 0 ) );
                                                        if ( tex->get_name().find( "square_drop_shadow" ) != string::npos ||
                                                             tex->get_name().find( "drop-shadow" ) != string::npos )
                                                        {
                                                                // don't apply vertex lighting to a shadow model
                                                                shadow_skip = true;
                                                                break;
                                                        }
                                                }
                                        }

                                        if ( shadow_skip )
                                                continue;
                                }
#endif

                                mod_geom->set_vertex_data( mod_vdata );
                                res.geomnode->set_geom( res.geomidx, mod_geom );
                        }

                        // since this prop has static lighting applied to the vertices
                        // ignore any shaders, dynamic lights, or materials.
                        // also make sure the vertex colors are rendered.
                        propnp.set_shader_off( 1 );
                        propnp.set_light_off( 1 );
                        propnp.set_material_off( 1 );
                        propnp.set_attrib( ColorAttrib::make_vertex(), 2 );
                }
                else if ( prop->flags & STATICPROPFLAGS_NOLIGHTING )
                {
                        propnp.set_shader_off( 1 );
                        propnp.set_light_off( 1 );
                        propnp.set_material_off( 1 );
                }

#ifdef CIO
                if ( prop->flags & STATICPROPFLAGS_LIGHTMAPSHADOWS ||
                     prop->flags & STATICPROPFLAGS_REALSHADOWS )
                {
                        // game specific code!

                        // we want to strip the fake drop shadows
                        // since we either have lightmap shadows
                        // or realtime depth shadows

                        // GeomNodes with the drop_shadow texture on any of the RenderStates
                        // will be completely removed. it's a little brute force, but should work.

                        NodePathCollection npc = propmdl.find_all_matches( "**/+GeomNode" );
                        for ( int i = 0; i < npc.get_num_paths(); i++ )
                        {
                                NodePath np = npc[i];
                                GeomNode *gn = DCAST( GeomNode, np.node() );
                                for ( int j = 0; j < gn->get_num_geoms(); j++ )
                                {
                                        const RenderState *state = gn->get_geom_state( j );
                                        const TextureAttrib *tattr;
                                        if ( state->get_attrib( tattr ) )
                                        {
                                                if ( tattr->get_num_on_stages() == 0 )
                                                {
                                                        continue;
                                                }
                                                Texture *tex = tattr->get_on_texture( tattr->get_on_stage( 0 ) );
                                                if ( tex->get_name().find( "square_drop_shadow" ) != string::npos ||
                                                     tex->get_name().find( "drop-shadow" ) != string::npos )
                                                {
                                                        np.remove_node();
                                                }
                                        }
                                }
                        }
                }
#endif

                if ( prop->flags & STATICPROPFLAGS_DOUBLESIDE )
                {
                        propmdl.set_two_sided( true, 1 );
                }

                if ( prop->flags & STATICPROPFLAGS_HARDFLATTEN )
                {
                        propmdl.clear_model_nodes();
                        propmdl.flatten_strong();
                }

                // only do group flattening if the prop doesn't
                // use dynamic lighting (ambient probes).
                //
                // grouping all of the static props together
                // will mess up the origin on each prop and they
                // won't be dynamically lit correctly.

                if ( ( prop->flags & STATICPROPFLAGS_GROUPFLATTEN ) != 0 &&
                     ( prop->flags & STATICPROPFLAGS_DYNAMICLIGHTING ) == 0 )
                {
                        // find the leaf this prop resides in
                        int leaf = find_leaf( propnp.get_pos() );
                        if ( leaf2props.find( leaf ) == -1 )
                        {
                                stringstream ss;
                                ss << "propGroupLeaf" << leaf;
                                leaf2props.store( leaf, _result.attach_new_node( new BSPProp( ss.str() ) ) );
                        }
                        // move the prop underneath that leaf node group
                        propnp.wrt_reparent_to( leaf2props[leaf] );
                }
        }

        // any props with the STATICPROPFLAGS_GROUPFLATTEN bit
        // have been grouped together under a common node for each leaf
        //
        // this means that any props in the same leaf will be
        // aggressively flattened together
        //
        // do the flattening
        for ( size_t i = 0; i < leaf2props.size(); i++ )
        {
                NodePath groupnp = leaf2props.get_data( i );
                clear_model_nodes_below( groupnp );
                groupnp.flatten_strong();
        }
}

void BSPLoader::load_entities()
{
        Loader *loader = Loader::get_global_ptr();

        for ( int entnum = 0; entnum < _bspdata->numentities; entnum++ )
        {
                entity_t *ent = &_bspdata->entities[entnum];

                string classname = ValueForKey( ent, "classname" );
                string id = ValueForKey( ent, "id" );

                vec_t origin[3];
                GetVectorForKey( ent, "origin", origin );

                vec_t angles[3];
                GetVectorForKey( ent, "angles", angles );

                string targetname = ValueForKey( ent, "targetname" );

                if ( !_ai )
                {
                        if ( classname == "env_fog" )
                        {
                                // Fog
                                PN_stdfloat density = FloatForKey( ent, "fogdensity" );
                                LColor fog_color = color_from_value( ValueForKey( ent, "fogcolor" ) );
                                PT( Fog ) fog = new Fog( "env_fog" );
                                fog->set_exp_density( density );
                                fog->set_color( fog_color );
                                NodePath fognp = _render.attach_new_node( fog );
                                _render.set_fog( fog );
                                _nodepath_entities.push_back( fognp );
                        }
                        else if ( !strncmp( classname.c_str(), "trigger_", 8 ) ||
                                  !strncmp( classname.c_str(), "func_water", 10 ) )
                        {
                                // This is a bounds entity. We do not actually care about the geometry,
                                // but the mins and maxs of the model. We will use that to create
                                // a BoundingBox to check if the avatar is inside of it.
                                int modelnum = extract_modelnum_s( ent );
                                if ( modelnum != -1 )
                                {
                                        remove_model( modelnum );

                                        dmodel_t *mdl = &_bspdata->dmodels[modelnum];

                                        PT( CBoundsEntity ) entity = new CBoundsEntity;
                                        entity->set_data( entnum, ent, this, mdl );
                                        _class_entities.push_back( entity );
#ifdef HAVE_PYTHON
                                        PyObject *py_ent = DTool_CreatePyInstance<CBoundsEntity>( entity, true );
                                        make_pyent( entity, py_ent, classname );
#endif
                                }
                        }
                        else if ( !strncmp( classname.c_str(), "func_", 5 ) )
                        {
                                // Brush entites begin with func_, handle those accordingly.
                                int modelnum = extract_modelnum_s( ent );
                                if ( modelnum != -1 )
                                {
                                        // Brush model
                                        NodePath modelroot = get_model( modelnum );
                                        // render all faces of brush model in a single batch
                                        clear_model_nodes_below( modelroot );
                                        modelroot.flatten_strong();

                                        if ( !strncmp( classname.c_str(), "func_wall", 9 ) ||
                                             !strncmp( classname.c_str(), "func_detail", 11 ) ||
                                             !strncmp( classname.c_str(), "func_illusionary", 17 ) )
                                        {
                                                // func_walls and func_details aren't really entities,
                                                // they are just hints to the compiler. we can treat
                                                // them as regular brushes part of worldspawn.
                                                modelroot.wrt_reparent_to( get_model( 0 ) );
                                                flatten_node( modelroot );
                                                NodePathCollection npc = modelroot.get_children();
                                                for ( int n = 0; n < npc.get_num_paths(); n++ )
                                                {
                                                        npc[n].wrt_reparent_to( get_model( 0 ) );
                                                }
                                                remove_model( modelnum );
                                                continue;
                                        }

                                        PT( CBrushEntity ) entity = new CBrushEntity;
                                        entity->set_data( entnum, ent, this, modelnum, &_bspdata->dmodels[modelnum], modelroot );
                                        _class_entities.push_back( entity );

#ifdef HAVE_PYTHON
                                        PyObject *py_ent = DTool_CreatePyInstance<CBrushEntity>( entity, true );
                                        make_pyent( entity, py_ent, classname );

#endif
                                }
                        }
                        else
                        {
                                // We don't know what this entity is exactly, maybe they linked it to a python class.
                                // It didn't start with func_, so we can assume it's just a point entity.
                                PT( CPointEntity ) entity = new CPointEntity;
                                entity->set_data( entnum, ent, this );
                                _class_entities.push_back( entity );

#ifdef HAVE_PYTHON
                                PyObject *py_ent = DTool_CreatePyInstance<CPointEntity>( entity, true );
                                make_pyent( entity, py_ent, classname );
#endif
                        }
                }
                else
                {
#ifdef HAVE_PYTHON
                        if ( _svent_to_class.find( classname ) != _svent_to_class.end() )
                        {
                                if ( _sv_ent_dispatch != nullptr )
                                {
                                        PyObject *ret = PyObject_CallMethod( _sv_ent_dispatch, "createServerEntity",
                                                                             "Oi", _svent_to_class[classname], entnum );
                                        if ( !ret )
                                        {
                                                PyErr_Print();
                                        }
                                        else
                                        {
                                                PT( CPointEntity ) entity = new CPointEntity;
                                                entity->set_data( entnum, ent, this );
                                                _class_entities.push_back( entity );
                                                Py_INCREF( ret );
                                                _py_entities.push_back( ret );
                                                _cent_to_pyent[entity] = ret;
                                        }
                                }
                        }
#endif
                }
                
        }

        // Now load all of the entities at the application level.
        for ( size_t i = 0; i < _py_entities.size(); i++ )
        {
                PyObject_CallMethod( _py_entities[i], "load", NULL );
        }
}

#ifdef HAVE_PYTHON
void BSPLoader::set_server_entity_dispatcher( PyObject *dispatch )
{
        _sv_ent_dispatch = dispatch;
}

void BSPLoader::link_server_entity_to_class( const string &name, PyTypeObject *type )
{
        _svent_to_class[name] = type;
}

void BSPLoader::make_pyent( CBaseEntity *cent, PyObject *py_ent, const string &classname )
{
        if ( _entity_to_class.find( classname ) != _entity_to_class.end() )
        {
                // A python class was linked to this entity!
                PyObject *obj = PyObject_CallObject( (PyObject *)_entity_to_class[classname], NULL );
                if ( obj == nullptr )
                        PyErr_PrintEx( 1 );
                Py_INCREF( obj );
                PyObject_SetAttrString( obj, "cEntity", py_ent );
                // Don't call load just yet, we need to have all of the entities created first, because some
                // entities rely on others.
                _py_entities.push_back( obj );
                _cent_to_pyent[cent] = obj;
        }
}
#endif

void BSPLoader::remove_model( int modelnum )
{
        NodePath modelroot = get_model( modelnum );
        if ( !modelroot.is_empty() )
        {
                _model_roots.erase( find( _model_roots.begin(), _model_roots.end(), modelroot ) );
                _model_origins.erase( modelroot );
                modelroot.remove_node();
        }
}

INLINE bool BSPLoader::is_cluster_visible( int curr_cluster, int cluster ) const
{
        if ( !_active_level )
        {
                return true;
        }
        if ( curr_cluster == cluster )
        {
                return true;
        }
        else if ( !_has_pvs_data )
        {
                return false;
        }

        // 1 means that the specified leaf is visible from the current leaf
        // 0 means it's not
        int dat = _leaf_pvs[curr_cluster][( cluster - 1 ) >> 3] & ( 1 << ( ( cluster - 1 ) & 7 ) );
        return dat != 0;
}

INLINE pvector<BoundingBox *> BSPLoader::get_visible_leaf_bboxs() const
{
        LightReMutexHolder holder( _leaf_aabb_lock );

        return _visible_leaf_bboxs;
}

void BSPLoader::update()
{
        // Update visibility

        LightReMutexHolder holder( _leaf_aabb_lock );

        int curr_leaf_idx = find_leaf( _camera.get_pos( _render ) );
        if ( curr_leaf_idx != _curr_leaf_idx )
        {
                _curr_leaf_idx = curr_leaf_idx;
                _visible_leaf_bboxs.clear();
                _visible_leafs.clear();

                // Add ourselves to the visible list.
                _visible_leaf_bboxs.push_back( _leaf_bboxs[curr_leaf_idx] );
                _visible_leafs.push_back( curr_leaf_idx );

                if ( _vis_leafs )
                {
                        _leaf_visnp[curr_leaf_idx].set_color_scale( LColor( 0, 1, 0, 1 ), 1 );
                }

                for ( int i = 1; i < _bspdata->dmodels[0].visleafs + 1; i++ )
                {
                        if ( i == curr_leaf_idx )
                        {
                                continue;
                        }
                        const dleaf_t *leaf = &_bspdata->dleafs[i];
                        if ( is_cluster_visible( curr_leaf_idx, i ) )
                        {
                                if ( _vis_leafs )
                                {
                                        _leaf_visnp[i].set_color_scale( LColor( 0, 0, 1, 1 ), 1 );
                                }
                                _visible_leaf_bboxs.push_back( _leaf_bboxs[i] );
                                _visible_leafs.push_back( i );
                        }
                        else
                        {
                                if ( _vis_leafs )
                                {
                                        _leaf_visnp[i].set_color_scale( LColor( 1, 0, 0, 1 ), 1 );
                                }
                        }
                }
        }
}

AsyncTask::DoneStatus BSPLoader::update_task( GenericAsyncTask *task, void *data )
{
        BSPLoader *self = (BSPLoader *)data;
        if ( self->_want_visibility )
        {
                self->update();
        }

        return AsyncTask::DS_cont;
}

void BSPLoader::read_materials_file()
{
        VirtualFileSystem *vfs = VirtualFileSystem::get_global_ptr();

        if ( !vfs->exists( _materials_file ) )
        {
                return;
        }
        string data = vfs->read_file( _materials_file, true );

        string texname = "";
        string material = "";
        bool in_texname = true;

        for ( size_t i = 0; i < data.length(); i++ )
        {
                char c = data[i];
                if ( in_texname )
                {
                        if ( c != ' ' && c != '\t' )
                                texname += c;
                        else
                        {
                                in_texname = false;
                        }
                }
                else
                {
                        if ( c == '\n' || i == data.length() - 1 )
                        {
                                _materials[texname] = material;
                                texname = "";
                                material = "";
                                in_texname = true;
                        }
                        else if ( c != '\r' )
                        {
                                material += c;
                        }
                }
        }
}

void BSPLoader::setup_shadowcam()
{
        PT( Camera ) cam = new Camera( "shadowcam" );
        cam->set_camera_mask( _shadowcam_mask );
        cam->set_scene( _render );
        PT( OrthographicLens ) lens = new OrthographicLens();
        lens->set_film_size( _shadow_filmsize, _shadow_filmsize );
        cam->set_lens( lens );

        NodePath state( "state" );
        state.set_color_scale_off( 10 );
        state.set_shader_off( 10 );
        state.set_texture_off( 10 );
        state.set_light_off( 10 );
        state.set_fog_off( 10 );
        state.set_material_off( 10 );
        state.set_attrib( IgnorePVSAttrib::make(), 10 );
        state.set_color( _shadow_color, 10 );

        cam->set_initial_state( state.get_state() );

        _render.hide( _shadowcam_mask );

        _shadowcam = _camera.attach_new_node( cam );
        _shadowcam.set_attrib( IgnorePVSAttrib::make(), 1 );
        _shadowcam.set_pos( _shadowcam_pos );
        _shadowcam.look_at( 0, 0, 0 );
        _shadowcam.set_compass( _render );

        PT( GraphicsOutput ) buf = _win->make_texture_buffer( "shadow", _shadow_texsize, _shadow_texsize );
        buf->set_clear_color_active( true );
        buf->set_clear_color( LColor( 1 ) );
        _shadow_buf = buf;
        PT( DisplayRegion ) dr = buf->make_display_region();
        dr->set_camera( _shadowcam );

        PT( Texture ) tex = buf->get_texture();
        tex->set_border_color( LColor( 1 ) );
        tex->set_wrap_u( SamplerState::WM_border_color );
        tex->set_wrap_v( SamplerState::WM_border_color );
        _shadow_tex = tex;

        _shadow_depth = new Texture( "shadowdepth" );
        buf->add_render_texture( _shadow_depth, GraphicsOutput::RTM_bind_or_copy,
                                 GraphicsOutput::RTP_depth_stencil );
        if ( _win->get_gsg()->get_supports_shadow_filter() )
        {
                _shadow_depth->set_minfilter( SamplerState::FT_shadow );
                _shadow_depth->set_magfilter( SamplerState::FT_shadow );
        }
}

Texture *BSPLoader::get_shadow_tex() const
{
        return _shadow_tex;
}

bool BSPLoader::read( const Filename &file )
{
        cleanup();

        if ( !_ai )
        {
                if ( _win == nullptr )
                {
                        bspfile_cat.error()
                                << "Cannot load BSP file: no GraphicsWindow was specified\n";
                        return false;
                }
                if ( _camera.is_empty() && _want_visibility )
                {
                        bspfile_cat.error()
                                << "Cannot load BSP file: visibility requested but no Camera NodePath specified\n";
                        return false;
                }
                if ( _render.is_empty() )
                {
                        bspfile_cat.error()
                                << "Cannot load BSP file: no render NodePath specified\n";
                        return false;
                }
        }

        _generated_shader_seq++;

        dtexdata_init();

        PT( BSPRoot ) root = new BSPRoot( "maproot" );
        _result = NodePath( root );

        if ( !_ai )
        {
                read_materials_file();

                // Scale down the entire loaded level as a conversion from Hammer units to Panda units.
                // Hammer units are tiny compared to panda.
                _result.set_scale( HAMMER_TO_PANDA );

                if ( _want_shadows )
                {
                        setup_shadowcam();
                }
        }
        
        _has_pvs_data = false;

        _leaf_pvs.resize( MAX_MAP_LEAFS );

        VirtualFileSystem *vfs = VirtualFileSystem::get_global_ptr();

        bspfile_cat.info()
                << "Reading " << file.get_fullpath() << "...\n";
        nassertr( vfs->exists( file ), false );

        string data;
        nassertr( vfs->read_file( file, data, true ), false );
        int length = data.length();
        char *buffer = new char[length + 1];
        memcpy( buffer, data.c_str(), length );
        _bspdata = LoadBSPImage( (dheader_t *)buffer );

        ParseEntities(_bspdata);

        _leaf_aabb_lock.acquire();
        _leaf_bboxs.resize( MAX_MAP_LEAFS );
        // Decompress the per leaf visibility data.
        for ( int i = 0; i < _bspdata->dmodels[0].visleafs + 1; i++ )
        {
                dleaf_t *leaf = &_bspdata->dleafs[i];

                uint8_t *pvs = new uint8_t[( MAX_MAP_LEAFS + 7 ) / 8];
                memset( pvs, 0, ( _bspdata->dmodels[0].visleafs + 7 ) / 8 );

                if ( leaf->visofs != -1 )
                {
                        DecompressVis( _bspdata, &_bspdata->dvisdata[leaf->visofs], pvs, ( MAX_MAP_LEAFS + 7 ) / 8 );
                        _has_pvs_data = true;
                }

                _leaf_pvs[i] = pvs;

                PT( BoundingBox ) bbox = new BoundingBox(
                        LVector3( ( leaf->mins[0] - LEAF_NUDGE ) / 16.0, ( leaf->mins[1] - LEAF_NUDGE ) / 16.0, ( leaf->mins[2] - LEAF_NUDGE ) / 16.0 ),
                        LVector3( ( leaf->maxs[0] + LEAF_NUDGE ) / 16.0, ( leaf->maxs[1] + LEAF_NUDGE ) / 16.0, ( leaf->maxs[2] + LEAF_NUDGE ) / 16.0 )
                );
                _leaf_bboxs[i] = bbox;
        }
        _leaf_aabb_lock.release();

        if ( !_ai )
        {
                LightmapPalettizer lmp( this );
                _lightmap_dir = lmp.palettize_lightmaps();

                make_faces();
                SceneGraphReducer gr;
                gr.apply_attribs( _result.node() );

                _result.set_shader_off();
                _result.set_attrib( BSPFaceAttrib::make_default(), 1 );
        }
        else
        {
                make_faces_ai();
        }

        load_entities();

        if ( !_ai )
        {
                load_static_props();

                if ( _vis_leafs )
                {
                        Randomizer random;
                        // Make a cube outline of the bounds for each leaf so we can visualize them.
                        for ( int leafnum = 0; leafnum < _bspdata->dmodels[0].visleafs + 1; leafnum++ )
                        {
                                dleaf_t *leaf = &_bspdata->dleafs[leafnum];
                                LPoint3 mins( ( leaf->mins[0] - LEAF_NUDGE ) / 16.0, ( leaf->mins[1] - LEAF_NUDGE ) / 16.0, ( leaf->mins[2] - LEAF_NUDGE ) / 16.0 );
                                LPoint3 maxs( ( leaf->maxs[0] + LEAF_NUDGE ) / 16.0, ( leaf->maxs[1] + LEAF_NUDGE ) / 16.0, ( leaf->maxs[2] + LEAF_NUDGE ) / 16.0 );
                                NodePath leafvis = _result.attach_new_node( UTIL_make_cube_outline( mins, maxs, LColor( 1, 1, 1, 1 ), 2 ) );
                                leafvis.clear_model_nodes();
                                leafvis.flatten_strong();
                                _leaf_visnp.push_back( leafvis );
                        }
                }

                _amb_probe_mgr.process_ambient_probes();

                _update_task = new GenericAsyncTask( file.get_basename_wo_extension() + "-updateTask", update_task, this );
                AsyncTaskManager::get_global_ptr()->add( _update_task );
        }

        _colldata = SetupCollisionBSPData( _bspdata );

        _active_level = true;

        return true;
}

void BSPLoader::update_dynamic_node( const NodePath &node )
{
        if ( _active_level )
        {
                _amb_probe_mgr.update_node( node.node(), node.get_net_transform() );
        }
}

void BSPLoader::do_optimizations()
{
        // Do some house keeping

        for ( size_t i = 0; i < _model_roots.size(); i++ )
        {
                NodePath mdlroot = _model_roots[i];
                BSPModel *mdlnode = DCAST( BSPModel, mdlroot.node() );
                if ( i == 0 )
                {
                        // We can do some hard flattening on model 0, which is just all of the
                        // static brushes that aren't tied to entities.
                        clear_model_nodes_below( mdlroot );
                        flatten_node( mdlroot );
                }
                else
                {
                	// For non zero models, the origin matters, so we will only flatten the children.
                        mdlnode->set_preserve_transform( ModelNode::PT_local );
                	NodePathCollection mdlroots = mdlroot.find_all_matches("**/+ModelNode");
                        
                	for ( int j = 0; j < mdlroots.get_num_paths(); j++ )
                	{
                                // Apply my transform to the GeomNode
                                NodePath np = mdlroots.get_path( j );
                                NodePath gnp = np.find( "**/+GeomNode" );
                                gnp.set_transform( np.get_transform() );
                                gnp.reparent_to( mdlroot );
                                np.remove_node();
                                flatten_node( gnp );
                	}
                        flatten_node( mdlroot );

                        // Now restore the bmodel's origin
                        NodePath temp( "temp" );
                        temp.node()->steal_children( mdlnode );

                        mdlroot.set_pos( _model_origins[mdlroot] );

                        NodePathCollection children = temp.get_children();
                        for ( int j = 0; j < children.get_num_paths(); j++ )
                        {
                                NodePath child = children[j];
                                child.wrt_reparent_to( mdlroot );
                        }
                        
                }
        }


        // We can flatten the props since they just sit there.
        //NodePathCollection npc = _result.find_all_matches( "**/+BSPProp" );
        //for ( int i = 0; i < npc.get_num_paths(); i++ )
        //{
        //        DCAST( BSPProp, npc[i].node() )->set_preserve_transform( ModelNode::PT_local );
        //        clear_model_nodes_below( npc[i] );
        //        npc[i].flatten_strong();
        //}

        //========================================================================
        // Since it's safe to assume that worldspawn geometry will never move,
        // we can predetermine which leafs contain which worldspawn Geoms.
        // This will greatly optimize Cull, as we no longer have to test
        // each Geom against each visible leaf to determine visibility,
        // we already know which Geoms are visible.

        std::cout
                << "There are " << _bspdata->dmodels[0].numfaces << " world faces." << std::endl;

        bsp_build_leaf_geom_collector.start();

        std::cout
                << "Building accelerated leaf Geom structure...\n";

        _leaf_geom_list.resize( _bspdata->dmodels[0].visleafs + 1 );
        NodePath worldspawn = get_model( 0 );
        NodePathCollection geomnodes = worldspawn.find_all_matches( "**/+GeomNode" );
        for ( int i = 0; i < geomnodes.get_num_paths(); i++ )
        {
                CPT( TransformState ) transform = geomnodes[i].get_net_transform();
                CPT( RenderState ) node_state = geomnodes[i].get_net_state();
                
                PT( GeomNode ) gn = DCAST( GeomNode, geomnodes[i].node() );
                int num_geoms = gn->get_num_geoms();
                _leaf_geoms.reserve( _leaf_geoms.size() + num_geoms );
                for ( int j = 0; j < num_geoms; j++ )
                {
                        CPT( Geom ) geom = gn->get_geom( j );
                        CPT( RenderState ) geom_state = gn->get_geom_state( j );

                        CPT( RenderState ) net_state = node_state->compose( geom_state );
                        size_t geom_idx = _leaf_geoms.size();
                        _leaf_geoms.push_back( WorldSpawnGeomState( geom, net_state ) );
                        for ( int leafnum = 0; leafnum < _bspdata->dmodels[0].visleafs + 1; leafnum++ )
                        {
                                PT( BoundingBox ) leaf_bounds = _leaf_bboxs[leafnum];

                                // Move the Geom bounds into leaf AABB space (world space).
                                PT( GeometricBoundingVolume ) geom_gbv = geom->get_bounds()
                                        ->make_copy()->as_geometric_bounding_volume();
                                geom_gbv->xform( transform->get_mat() );

                                if ( leaf_bounds->contains( geom_gbv ) != BoundingVolume::IF_no_intersection )
                                {
                                        // This leaf contains this geom!
                                        _leaf_geom_list[leafnum].push_back( geom_idx );
                                }
                        }
                }
        }

        std::cout
                << "Done.\n";

        bsp_build_leaf_geom_collector.stop();

        _result.premunge_scene( _win->get_gsg() );
        _result.prepare_scene( _win->get_gsg() );
}

void BSPLoader::set_materials_file( const Filename &file )
{
        _materials_file = file;
}

void BSPLoader::add_dynamic_node( const NodePath &node )
{
        _explicit_dynamic_nodes.push_back( WeakNodePath( node ) );
}

void BSPLoader::cleanup()
{
        if ( _active_level )
        {
                // Cleanup any shaders we generated on the Geom states.

                if ( !_render.is_empty() )
                {
                        NodePathCollection npc = _render.find_all_matches( "**/+GeomNode" );

                        for ( size_t i = 0; i < _explicit_dynamic_nodes.size(); i++ )
                        {
                                if ( _explicit_dynamic_nodes[i].was_deleted() )
                                {
                                        continue;
                                }

                                npc.add_paths_from( _explicit_dynamic_nodes[i].get_node_path().find_all_matches( "**/+GeomNode" ) );
                        }

                        for ( int i = 0; i < npc.get_num_paths(); i++ )
                        {
                                PT( GeomNode ) gn = DCAST( GeomNode, npc[i].node() );
                                for ( int j = 0; j < gn->get_num_geoms(); j++ )
                                {
                                        CPT( RenderState ) state = gn->get_geom_state( j );
                                        const ShaderAttrib *geom_shattr;
                                        state->get_attrib_def( geom_shattr );
                                        if ( ( geom_shattr->auto_shader() && geom_shattr->get_flag( BSPSHADERFLAG_AUTO ) ) )
                                        {
                                                // We generated a shader for this Geom, remove it.
                                                state = state->remove_attrib( ShaderAttrib::get_class_slot() );
                                                state->_generated_shader = nullptr;
                                                gn->set_geom_state( j, state );
                                        }
                                }
                        }
                }
        }

        _active_level = false;

        _amb_probe_mgr.cleanup();

        _model_origins.clear();
        for ( size_t i = 0; i < _model_roots.size(); i++ )
        {
                if ( !_model_roots[i].is_empty() )
                        _model_roots[i].remove_node();
        }
        _model_roots.clear();

        _materials.clear();

        _geom_shader_cache.clear();

        _leaf_pvs.clear();

        _leaf_aabb_lock.acquire();
        _leaf_geom_list.clear();
        _leaf_geoms.clear();
        _visible_leafs.clear();
        _leaf_bboxs.clear();
        _visible_leaf_bboxs.clear();
        _leaf_aabb_lock.release();

        if ( _shadow_buf != nullptr )
        {
                _win->get_engine()->remove_window( _shadow_buf );
                _shadow_buf = nullptr;
        }

        if ( !_shadowcam.is_empty() )
        {
                _shadowcam.remove_node();
        }

        if ( _update_task != nullptr )
        {
                _update_task->remove();
                _update_task = nullptr;
        }

        _has_pvs_data = false;

        for ( size_t i = 0; i < _nodepath_entities.size(); i++ )
        {
                _nodepath_entities[i].remove_node();
        }
        _nodepath_entities.clear();

        _cent_to_pyent.clear();

#ifdef HAVE_PYTHON
        for ( size_t i = 0; i < _py_entities.size(); i++ )
        {
                PyObject_CallMethod( _py_entities[i], "unload", NULL );
                Py_DECREF( _py_entities[i] );
        }
        _py_entities.clear();
#endif
        _class_entities.clear();

        _lightmap_dir.face_index.clear();
        _lightmap_dir.face_entries.clear();
        _lightmap_dir.entries.clear();

        if ( !_result.is_empty() )
                _result.remove_node();

        if ( _colldata )
                delete _colldata;
        _colldata = nullptr;

        if ( _bspdata )
                delete _bspdata;
        _bspdata = nullptr;
}

BSPLoader::BSPLoader() :
#ifdef HAVE_PYTHON
        _sv_ent_dispatch( nullptr ),
#endif
        _update_task( nullptr ),
        _win( nullptr ),
        _has_pvs_data( false ),
        _want_visibility( true ),
        _physics_type( PT_panda ),
        _vis_leafs( false ),
        _want_lightmaps( true ),
        _curr_leaf_idx( -1 ),
        _leaf_aabb_lock( "leafAABBMutex" ),
        _gamma( DEFAULT_GAMMA ),
        _diffuse_stage( new TextureStage( "diffuse_stage" ) ),
        _lightmap_stage( new TextureStage( "lightmap_stage" ) ),
        _envmap_stage( new TextureStage( "envmap_stage" ) ),
        _shadow_stage( new TextureStage( "shadow_stage" ) ),
        _amb_probe_mgr( this ),
        _active_level( false ),
        _ai( false ),
        _shadowcam_pos( -30, 25, 40 ),
        _shadowcam_mask( BitMask32::bit( 5 ) ),
        _shadow_color( 0.5, 0.5, 0.5, 1.0 ),
        _shadow_tex( nullptr ),
        _shadow_buf( nullptr ),
        _shadow_depth( nullptr ),
        _shadow_filmsize( 60 ),
        _shadow_texsize( 1024 ),
        _want_shadows( true ),
        _wireframe( false ),
        _bspdata( nullptr ),
        _colldata( nullptr )
{
        _diffuse_stage->set_texcoord_name( "basetexture" );
        _diffuse_stage->set_sort( 0 );

        _lightmap_stage->set_texcoord_name( "lightmap" );
        _lightmap_stage->set_mode( TextureStage::M_modulate );
        _lightmap_stage->set_sort( 1 );

        _shadow_stage->set_texcoord_name( "shadow" );
        _shadow_stage->set_sort( 2 );

        _envmap_stage->set_mode( TextureStage::M_add );
}

void BSPLoader::set_wireframe( bool flag )
{
        _wireframe = flag;
}

INLINE bool BSPLoader::get_wireframe() const
{
        return _wireframe;
}

void BSPLoader::set_want_shadows( bool flag )
{
        _want_shadows = flag;
}

void BSPLoader::set_shadow_resolution( int filmsize, int texsize )
{
        _shadow_filmsize = filmsize;
        _shadow_texsize = texsize;
}

void BSPLoader::cast_shadows( NodePath &node )
{
        node.show_through( _shadowcam_mask );
}

void BSPLoader::set_shadow_color( const LColor &color )
{
        _shadow_color = color;
        if ( !_shadowcam.is_empty() )
        {
                Camera *cam = DCAST( Camera, _shadowcam.node() );
                CPT( RenderState ) state = cam->get_initial_state();
                state = state->set_attrib( ColorAttrib::make_flat( _shadow_color ), 10 );
                cam->set_initial_state( state );
        }
}

void BSPLoader::set_shadow_cam_bitmask( const BitMask32 &mask )
{
        _shadowcam_mask = mask;
        if ( !_shadowcam.is_empty() )
        {
                DCAST( Camera, _shadowcam.node() )->set_camera_mask( _shadowcam_mask );
                _render.hide( mask );
        }
}

void BSPLoader::set_shadow_cam_pos( const LPoint3 &pos )
{
        _shadowcam_pos = pos;
        if ( !_shadowcam.is_empty() )
        {
                _shadowcam.set_pos( pos );
        }
}

/**
 * Sets whether or not this is an AI/Server instance of the loader.
 * If this is true, only the AI views of entities will be loaded,
 * and nothing related to rendering will be dealt with.
 */
void BSPLoader::set_ai( bool ai )
{
        _ai = ai;
}

/**
 * Gets whether or not this is an AI/Server instance of the loader.
 * If this is true, only the AI views of entities will be loaded,
 * and nothing related to rendering will be dealt with.
 */
INLINE bool BSPLoader::is_ai() const
{
        return _ai;
}

INLINE bool BSPLoader::has_active_level() const
{
        return _active_level;
}

/**
 * Returns true if there is an active BSP level loaded
 * with a PVS and visibility has been toggled on.
 */
INLINE bool BSPLoader::has_visibility() const
{
        return _active_level && _want_visibility && _has_pvs_data;
}

void BSPLoader::set_camera( const NodePath &camera )
{
        _camera = camera;
}

void BSPLoader::set_render( const NodePath &render )
{
        _render = render;
}

void BSPLoader::set_want_visibility( bool flag )
{
        _want_visibility = flag;
}

void BSPLoader::set_gamma( PN_stdfloat gamma, int overbright )
{
        _gamma = gamma;
}

INLINE PN_stdfloat BSPLoader::get_gamma() const
{
        return _gamma;
}

void BSPLoader::set_win( GraphicsWindow *win )
{
        _win = win;
}

void BSPLoader::set_visualize_leafs( bool flag )
{
        _vis_leafs = flag;
}

void BSPLoader::set_want_lightmaps( bool flag )
{
        _want_lightmaps = flag;
}

void BSPLoader::link_entity_to_class( const string &entname, PyTypeObject *type )
{
        _entity_to_class[entname] = type;
}

NodePath BSPLoader::get_result() const
{
        return _result;
}

int BSPLoader::get_num_entities() const
{
        return _bspdata->numentities;
}

string BSPLoader::get_entity_value( int entnum, const char *key ) const
{
        entity_t *ent = &_bspdata->entities[entnum];
        return ValueForKey( ent, key );
}

int BSPLoader::get_entity_value_int( int entnum, const char *key ) const
{
        entity_t *ent = &_bspdata->entities[entnum];
        return IntForKey( ent, key );
}

float BSPLoader::get_entity_value_float( int entnum, const char *key ) const
{
        entity_t *ent = &_bspdata->entities[entnum];
        return FloatForKey( ent, key );
}

LVector3 BSPLoader::get_entity_value_vector( int entnum, const char *key ) const
{
        entity_t *ent = &_bspdata->entities[entnum];

        vec3_t vec;
        GetVectorForKey( ent, key, vec );

        return LVector3( vec[0], vec[1], vec[2] );
}

LColor BSPLoader::get_entity_value_color( int entnum, const char *key, bool scale ) const
{
        entity_t *ent = &_bspdata->entities[entnum];

        return color_from_value( ValueForKey( ent, key ), scale );
}

NodePath BSPLoader::get_entity( int entnum ) const
{
        return _nodepath_entities[entnum];
}

NodePath BSPLoader::get_model( int modelnum ) const
{
        stringstream search;
        search << "**/model-" << modelnum;
        return _result.find( search.str() );
}

#ifdef HAVE_PYTHON

void BSPLoader::link_cent_to_pyent( int entnum, PyObject *pyent )
{
        Py_INCREF( pyent );
        _cent_to_pyent[get_c_entity( entnum )] = pyent;
}

PyObject *BSPLoader::get_py_entity_by_target_name( const string &targetname ) const
{
        for ( CEntToPyEnt::const_iterator itr = _cent_to_pyent.begin(); itr != _cent_to_pyent.end(); ++itr )
        {
                CBaseEntity *cent = itr->first;
                PyObject *pyent = itr->second;
                string tname = ValueForKey( &_bspdata->entities[cent->get_entnum()], "targetname" );
                if (  tname == targetname )
                {
                        Py_INCREF( pyent );
                        return pyent;
                }
        }

        Py_RETURN_NONE;
}

void BSPLoader::get_entity_keyvalues( PyObject *list, const int entnum )
{
        entity_t *ent = _bspdata->entities + entnum;
        for ( epair_t *ep = ent->epairs; ep->next != nullptr; ep = ep->next )
        {
                PyObject *kv = PyTuple_New( 2 );
                PyTuple_SetItem( kv, 0, PyString_FromString( ep->key ) );
                PyTuple_SetItem( kv, 1, PyString_FromString( ep->value ) );
                PyList_Append( list, kv );
        }
}

PyObject *BSPLoader::find_all_entities( const string &classname )
{
        PyObject *list = PyList_New( 0 );

        for ( auto itr = _cent_to_pyent.begin(); itr != _cent_to_pyent.end(); ++itr )
        {
                string cls = ValueForKey( _bspdata->entities + itr->first->get_entnum(), "classname" );
                if ( classname == cls )
                {
                        PyList_Append( list, itr->second );
                }
        }

        Py_INCREF( list );
        return list;
}

#endif

void BSPLoader::set_physics_type( int type )
{
        _physics_type = type;
}

BSPLoader *BSPLoader::get_global_ptr()
{
        if ( _global_ptr == nullptr )
                _global_ptr = new BSPLoader;
        return _global_ptr;
}

/**
 * Checks if the specified bounding volume intersects any
 * of the potentially visible leaf bounding boxes.
 */
INLINE bool BSPLoader::pvs_bounds_test( const GeometricBoundingVolume *bounds )
{
        LightReMutexHolder holder( _leaf_aabb_lock );

        size_t num_aabbs = _visible_leaf_bboxs.size();
        for ( size_t i = 0; i < num_aabbs; i++ )
        {
                if ( _visible_leaf_bboxs[i]->contains( bounds ) != BoundingVolume::IF_no_intersection )
                {
                        // Bounds intersected one of the potentially visible leafs.
                        return true;
                }
        }

        // No intersections.
        return false;
}

INLINE CPT( GeometricBoundingVolume ) BSPLoader::make_net_bounds( const TransformState *net_transform,
                                                                  const GeometricBoundingVolume *original )
{
        if ( net_transform->is_identity() )
        {
                return original;
        }

        PT( GeometricBoundingVolume ) gbv = DCAST(
                GeometricBoundingVolume, original->make_copy() );
        gbv->xform( net_transform->get_mat() );
        return gbv;
}

/**
 * Traces a line along the BSP tree. Returns true if the line traced
 * all the way to the end, false if the line intersected a face.
 */
bool BSPLoader::trace_line( const LPoint3 &start, const LPoint3 &end )
{
        if ( !_active_level )
        {
                return true;
        }

        Ray ray( ( start + LPoint3( 0, 0, 0.05 ) ) * 16, end * 16, LPoint3::zero(), LPoint3::zero() );
        Trace trace;
        CM_BoxTrace( ray, 0, CONTENTS_SOLID, false, _colldata, trace );

        return !trace.has_hit();
}

INLINE bspdata_t *BSPLoader::get_bspdata() const
{
        return _bspdata;
}

INLINE CBaseEntity *BSPLoader::get_c_entity( const int entnum ) const
{
        for ( size_t i = 0; i < _class_entities.size(); i++ )
        {
                if ( _class_entities[i]->get_entnum() == entnum )
                {
                        return _class_entities[i];
                }
        }

        return nullptr;
}

void BSPLoader::set_texture_contents_file( const Filename &file )
{
        SetTextureContentsFile( file.get_fullpath().c_str() );
}