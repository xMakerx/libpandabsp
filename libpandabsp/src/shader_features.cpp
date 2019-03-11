/**
 * PANDA3D BSP LIBRARY
 * Copyright (c) CIO Team. All rights reserved.
 *
 * @file shader_features.cpp
 * @author Brian Lach
 * @date December 26, 2018
 */

#include "shader_features.h"
#include "bsp_material.h"
#include "vifparser.h"
#include "shader_spec.h"

#include <texturePool.h>

static PT( Texture ) brdf_lut = nullptr;
static Texture *get_brdf_lut()
{
        if ( !brdf_lut )
                brdf_lut = TexturePool::load_texture( "phase_14/maps/brdf_lut.png" );

        return brdf_lut;
}

//==============================================================================================//

SHADERFEATURE_PARSE_FUNC( RimLightFeature )
{
        if ( mat->has_keyvalue( "$rimlight" ) &&
                (bool)atoi( mat->get_keyvalue( "$rimlight" ).c_str() ) )
        {
                has_feature = true;

                if ( mat->has_keyvalue( "$rimlightboost" ) )
                {
                        boost = atof( mat->get_keyvalue( "$rimlightboost" ).c_str() );
                }
                if ( mat->has_keyvalue( "$rimlightexponent" ) )
                {
                        exponent = atof( mat->get_keyvalue( "$rimlightexponent" ).c_str() );
                }
        }
}

SHADERFEATURE_SETUP_FUNC( RimLightFeature )
{
        if ( has_feature && ConfigVariableBool( "mat_rimlight", true ) )
        {
                perms.add_permutation( "RIMLIGHT" );
                perms.add_input( ShaderInput( "rimlightParams", LVector2( boost, exponent ) ) );
        }
}

//==============================================================================================//

SHADERFEATURE_PARSE_FUNC( BaseTextureFeature )
{
        if ( mat->has_keyvalue( "$basetexture" ) )
        {
                has_feature = true;

                if ( mat->has_keyvalue( "$basetexture_alpha" ) )
                {
                        // alpha channel supplied in a separate texture,
                        // common in Toontown
                        base_texture = TexturePool::load_texture( mat->get_keyvalue( "$basetexture" ),
                                                                  mat->get_keyvalue( "$basetexture_alpha" ) );
                }
                else
                {
                        base_texture = TexturePool::load_texture( mat->get_keyvalue( "$basetexture" ) );
                }

                if ( mat->has_keyvalue( "$basetexture_wrap" ) )
                {
                        const std::string &wrapmode = mat->get_keyvalue( "$basetexture_wrap" );
                        if ( wrapmode == "clamp" )
                        {
                                base_texture->set_wrap_u( SamplerState::WM_clamp );
                                base_texture->set_wrap_v( SamplerState::WM_clamp );
                        }
                        else if ( wrapmode == "repeat" )
                        {
                                base_texture->set_wrap_u( SamplerState::WM_repeat );
                                base_texture->set_wrap_v( SamplerState::WM_repeat );
                        }
                        else
                        {
                                bspmaterial_cat.warning()
                                        << "BaseTextureFeature: unknown wrap mode `" << wrapmode << "`\n";
                        }
                }
                
        }
}

SHADERFEATURE_SETUP_FUNC( BaseTextureFeature )
{
        if ( has_feature && base_texture )
        {
                perms.add_permutation( "BASETEXTURE" );
                perms.add_input( ShaderInput( "baseTextureSampler", base_texture ) );
        }
}

//==============================================================================================//

SHADERFEATURE_PARSE_FUNC( AlphaFeature )
{
        if ( mat->has_keyvalue( "$alpha" ) )
        {
                alpha = atof( mat->get_keyvalue( "$alpha" ).c_str() );

                has_feature = true;
        }
        else if ( mat->has_keyvalue( "$translucent" ) )
        {
                translucent = (bool)
                        atoi( mat->get_keyvalue( "$translucent" ).c_str() );

                has_feature = true;
        }
}

SHADERFEATURE_SETUP_FUNC( AlphaFeature )
{
        if ( has_feature )
        {
                if ( !translucent && alpha != -1 )
                {
                        std::stringstream ss;
                        ss << alpha;
                        perms.add_permutation( "ALPHA", ss.str() );
                }
                else if ( translucent )
                {
                        perms.add_permutation( "TRANSLUCENT" );
                }
        }
}

//==============================================================================================//

SHADERFEATURE_PARSE_FUNC( EnvmapFeature )
{
        if ( mat->has_keyvalue( "$envmap" ) )
        {
                has_feature = true;

                std::string envmap = mat->get_keyvalue( "$envmap" );
                if ( envmap != "env_cubemap" )
                {
                        envmap_texture = TexturePool::load_cube_map( envmap );
                        envmap_texture->set_minfilter( SamplerState::FT_linear_mipmap_linear );
                }

                if ( mat->has_keyvalue( "$envmaptint" ) )
                {
                        envmap_tint = Parser::to_3f( mat->get_keyvalue( "$envmaptint" ) );
                }
        }
        
}

SHADERFEATURE_SETUP_FUNC( EnvmapFeature )
{
        if ( has_feature && ConfigVariableBool( "mat_envmaps", true ) )
        {
                perms.add_permutation( "ENVMAP" );

                // if no envmap texture,
                // we expect it to be filled in by the closest env_cubemap.
                if ( envmap_texture )
                {
                        perms.add_input( ShaderInput( "envmapSampler", envmap_texture ) );
                }

                perms.add_input( ShaderInput( "envmapTint", envmap_tint ) );

                // For PBR Image-Based Lighting
                perms.add_input( ShaderInput( "brdfLUTSampler", get_brdf_lut() ) );
        }
}

//==============================================================================================//

SHADERFEATURE_PARSE_FUNC( DetailFeature )
{
        if ( mat->has_keyvalue( "$detail" ) )
        {
                has_feature = true;

                detail_texture = TexturePool::load_texture( mat->get_keyvalue( "$detail" ) );
                if ( mat->has_keyvalue( "$detailfactor" ) )
                {
                        detail_factor = atof( mat->get_keyvalue( "$detailfactor" ).c_str() );
                }
                if ( mat->has_keyvalue( "$detailscale" ) )
                {
                        detail_scale = atof( mat->get_keyvalue( "$detailscale" ).c_str() );
                }
                if ( mat->has_keyvalue( "$detailtint" ) )
                {
                        detail_tint = Parser::to_3f
                        ( mat->get_keyvalue( "$detailtint" ) );
                }
        }  
}

SHADERFEATURE_SETUP_FUNC( DetailFeature )
{
        if ( has_feature && detail_texture )
        {
                perms.add_permutation( "DETAIL" );
                perms.add_input( ShaderInput( "detailSampler", detail_texture ) );

                perms.add_input( ShaderInput( "detailParams",
                                              LVector2( detail_factor, detail_scale ) ) );
                perms.add_input( ShaderInput( "detailTint", detail_tint ) );
        }
}

//==============================================================================================//

SHADERFEATURE_PARSE_FUNC( HalfLambertFeature )
{
        if ( mat->has_keyvalue( "$halflambert" ) )
        {
                if ( atoi( mat->get_keyvalue( "$halflambert" ).c_str() ) )
                {
                        has_feature = true;
                        halflambert = true;
                }
                else
                {
                        has_feature = false;
                        halflambert = false;
                }
        }
}

SHADERFEATURE_SETUP_FUNC( HalfLambertFeature )
{
        if ( has_feature && halflambert )
        {
                perms.add_permutation( "HALFLAMBERT" );
        }
}

//==============================================================================================//

SHADERFEATURE_PARSE_FUNC( BumpmapFeature )
{
        if ( mat->has_keyvalue( "$bumpmap" ) )
        {
                has_feature = true;

                bump_tex = TexturePool::load_texture( mat->get_keyvalue( "$bumpmap" ) );
        }
}

SHADERFEATURE_SETUP_FUNC( BumpmapFeature )
{
        if ( has_feature && bump_tex )
        {
                perms.add_permutation( "BUMPMAP" );
                perms.add_input( ShaderInput( "bumpSampler", bump_tex ) );
        }
}

//==============================================================================================//

SHADERFEATURE_PARSE_FUNC( LightwarpFeature )
{
        if ( mat->has_keyvalue( "$lightwarp" ) )
        {
                has_feature = true;

                lightwarp_tex = TexturePool::load_texture( mat->get_keyvalue( "$lightwarp" ) );
                lightwarp_tex->set_wrap_u( SamplerState::WM_clamp );
                lightwarp_tex->set_wrap_v( SamplerState::WM_clamp );
        }
}

SHADERFEATURE_SETUP_FUNC( LightwarpFeature )
{
        if ( has_feature && lightwarp_tex )
        {
                perms.add_permutation( "LIGHTWARP" );
                perms.add_input( ShaderInput( "lightwarpSampler", lightwarp_tex ) );
        }
}

//==============================================================================================//

SHADERFEATURE_PARSE_FUNC( SelfIllumFeature )
{
        if ( mat->has_keyvalue( "$selfillum" ) &&
                (bool)atoi( mat->get_keyvalue( "$selfillum" ).c_str() ) )
        {
                has_feature = true;

                if ( mat->has_keyvalue( "$selfillumtint" ) )
                {
                        selfillumtint = Parser::to_3f( mat->get_keyvalue( "$selfillumtint" ) );
                }
        }
}

SHADERFEATURE_SETUP_FUNC( SelfIllumFeature )
{
        if ( has_feature )
        {
                perms.add_permutation( "SELFILLUM" );
                perms.add_input( ShaderInput( "selfillumTint", selfillumtint ) );
        }
}

//==============================================================================================//

SHADERFEATURE_PARSE_FUNC( ARME_Feature )
{
        if ( mat->has_keyvalue( "$arme" ) )
        {
                has_feature = true;
                arme_texture = TexturePool::load_texture( mat->get_keyvalue( "$arme" ) );
        }
}

SHADERFEATURE_SETUP_FUNC( ARME_Feature )
{
        if ( has_feature && arme_texture )
        {
                perms.add_permutation( "ARME" );
                perms.add_input( ShaderInput( "armeSampler", arme_texture ) );
        }
}