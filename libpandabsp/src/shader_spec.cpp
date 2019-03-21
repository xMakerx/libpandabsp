/**
 * PANDA3D BSP LIBRARY
 * Copyright (c) CIO Team. All rights reserved.
 *
 * @file shader_spec.cpp
 * @author Brian Lach
 * @date November 02, 2018
 */

#include "shader_spec.h"
#include "shader_generator.h"
#include "bsp_material.h"

#include <virtualFileSystem.h>

void ShaderSpec::ShaderSource::read( const Filename &file )
{
        VirtualFileSystem *vfs = VirtualFileSystem::get_global_ptr();
        filename = file;
        if ( !filename.empty() )
        {
                if ( vfs->exists( filename ) )
                {
                        full_source = vfs->read_file( filename, true );

                        size_t end_of_first_line = full_source.find_first_of( '\n' );
                        before_defines = full_source.substr( 0, end_of_first_line );
                        after_defines = full_source.substr( end_of_first_line );

                        has = true;
                }
                else
                {
                        std::cout << "Couldn't find shader source file "
                                << filename.get_fullpath() << std::endl;
                }
        }
}

static ConfigVariableInt parallax_mapping_samples
( "parallax-mapping-samples", 3,
  PRC_DESC( "Sets the amount of samples to use in the parallax mapping "
            "implementation. A value of 0 means to disable it entirely." ) );

static ConfigVariableDouble parallax_mapping_scale
( "parallax-mapping-scale", 0.1,
  PRC_DESC( "Sets the strength of the effect of parallax mapping, that is, "
            "how much influence the height values have on the texture "
            "coordinates." ) );

TypeHandle ShaderSpec::_type_handle;

ShaderSpec::ShaderSpec( const std::string &name, const Filename &vert_file,
                        const Filename &pixel_file, const Filename &geom_file ) :
        ReferenceCount(),
        Namable( name )
{
        read_shader_files( vert_file, pixel_file, geom_file );
}

void ShaderSpec::read_shader_files( const Filename &vert_file,
                                    const Filename &pixel_file,
                                    const Filename &geom_file )
{
        // load up and store the source of the shaders
        _vertex.read( vert_file );
        _pixel.read( pixel_file );
        _geom.read( geom_file );
}

ShaderConfig *ShaderSpec::get_shader_config( const BSPMaterial *mat )
{
        int idx = _config_cache.find( mat );
        if ( idx == -1 )
        {
                PT( ShaderConfig ) conf = make_new_config();
                conf->parse_from_material_keyvalues( mat );
                _config_cache[mat] = conf;
                return conf;
        }

        return _config_cache.get_data( idx );
}

ShaderPermutations ShaderSpec::setup_permutations( const BSPMaterial *mat,
                                                   const RenderState *state,
                                                   const GeomVertexAnimationSpec &anim, 
                                                   PSSMShaderGenerator *generator )
{
        ShaderPermutations result;
        return result;
}

#include <fogAttrib.h>

void ShaderSpec::add_fog( const RenderState *rs, ShaderPermutations &perms )
{
        // Check for fog.
        const FogAttrib *fog;
        if ( rs->get_attrib( fog ) && !fog->is_off() )
        {
                std::stringstream fss;
                fss << (int)fog->get_fog()->get_mode();
                perms.add_permutation( "FOG", fss.str() );
        }
}

#include <colorAttrib.h>

void ShaderSpec::add_color( const RenderState *rs, ShaderPermutations &perms )
{
        // Determine whether or not vertex colors or flat colors are present.
        const ColorAttrib *color;
        rs->get_attrib_def( color );
        ColorAttrib::Type ctype = color->get_color_type();
        if ( ctype != ColorAttrib::T_off )
        {
                perms.add_permutation( "NEED_COLOR" );
                if ( ctype == ColorAttrib::T_flat )
                {
                        perms.add_permutation( "COLOR_FLAT" );
                }
                else if ( ctype == ColorAttrib::T_vertex )
                {
                        perms.add_permutation( "COLOR_VERTEX" );
                }
        }
}

#include "pssmCameraRig.h"

bool ShaderSpec::add_csm( ShaderPermutations &result, PSSMShaderGenerator *generator )
{
        if ( generator->has_shadow_sunlight() )
        {
                result.add_permutation( "HAS_SHADOW_SUNLIGHT" );
                result.permutations["PSSM_SPLITS"] = pssm_splits.get_string_value();
                result.permutations["DEPTH_BIAS"] = depth_bias.get_string_value();
                result.permutations["NORMAL_OFFSET_SCALE"] = normal_offset_scale.get_string_value();

                float xel_size = 1.0 / pssm_size.get_value();

                std::stringstream ss;
                ss << xel_size * softness_factor.get_value();
                result.permutations["SHADOW_BLUR"] = ss.str();
                std::stringstream size_ss;
                size_ss << xel_size;
                result.permutations["SHADOW_TEXEL_SIZE"] = size_ss.str();

                if ( normal_offset_uv_space.get_value() )
                        result.add_permutation( "NORMAL_OFFSET_UV_SPACE" );

                result.add_input( ShaderInput( "pssmSplitSampler", generator->get_pssm_array_texture() ) );
                result.add_input( ShaderInput( "pssmMVPs", generator->get_pssm_rig()->get_mvp_array() ) );
                result.add_input( ShaderInput( "sunVector", generator->get_pssm_rig()->get_sun_vector() ) );

                result.add_input( ShaderInput( "ambientLightIdentifier", ambient_light_identifier.get_value().get_xyz() ) );
                result.add_input( ShaderInput( "ambientLightMin", ambient_light_min.get_value().get_xyz() ) );
                result.add_input( ShaderInput( "ambientLightScale", LVector2( ambient_light_scale ) ) );

                return true;
        }

        return false;
}

#include <clipPlaneAttrib.h>

bool ShaderSpec::add_clip_planes( const RenderState *rs, ShaderPermutations &perms )
{
        // Check for clip planes.
        const ClipPlaneAttrib *clip_plane;
        rs->get_attrib_def( clip_plane );

        std::stringstream ss;
        ss << clip_plane->get_num_on_planes();
        perms.add_permutation( "NUM_CLIP_PLANES", ss.str() );

        return clip_plane->get_num_on_planes() > 0;
}

//=================================================================================================