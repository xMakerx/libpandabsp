#include "lightmap_palettes.h"
#include "bspfile.h"
#include "bsploader.h"
#include "TexturePacker.h"

#include <bitset>
#include <pnmFileTypeJPG.h>
#include <cstdio>

NotifyCategoryDef( lightmapPalettizer, "" );

// Max size per palette before making a new one.
static const int max_palette = 1024;

// Currently we pack every single lightmap into one texture,
// no matter how big. The way to split the lightmap palettes
// is verrry slow atm.
//#define LMPALETTE_SPLIT

LightmapPalettizer::LightmapPalettizer( const BSPLoader *loader ) :
        _loader( loader )
{
}

INLINE PNMImage lightmap_img_for_face( const BSPLoader *loader, const dface_t *face, int lmnum = 0 )
{
        int width = face->lightmap_size[0] + 1;
        int height = face->lightmap_size[1] + 1;
        int num_luxels = width * height;

        if ( num_luxels <= 0 )
        {
                lightmapPalettizer_cat.warning()
                        << "Face has 0 size lightmap, will appear fullbright" << std::endl;
                PNMImage img( 16, 16 );
                img.fill( 1.0 );
                return img;
        }

        PNMImage img( width, height );

        int luxel = 0;

        int bump_sample_count = face->bumped_lightmap ? NUM_BUMP_VECTS + 1 : 1;

        for ( int y = 0; y < height; y++ )
        {
                for ( int x = 0; x < width; x++ )
                {
                        LRGBColor luxel_col( 1, 1, 1 );

                        // To get the final pixel color, multiply all of the individual lightstyle samples together.
                        for ( int lightstyle = 0; lightstyle < 4; lightstyle++ )
                        {
                                if ( face->styles[lightstyle] == 0xFF )
                                {
                                        // Doesn't have this lightstyle.
                                        continue;
                                }

                                colorrgbexp32_t *sample = SampleLightmap( loader->get_bspdata(), face, luxel, lightstyle, lmnum );

                                luxel_col.componentwise_mult( color_shift_pixel( sample, loader->get_gamma() ) );

                        }

                        img.set_xel( x, y, luxel_col );
                        luxel++;
                }
        }

        return img;
}

LightmapPaletteDirectory LightmapPalettizer::palettize_lightmaps()
{
        LightmapPaletteDirectory dir;

        pvector<Palette> result_vec;
        Palette pal;
        pal.packer = TEXTURE_PACKER::createTexturePacker();
        result_vec.push_back( pal );

        // First step, build sources.
        for ( int facenum = 0; facenum < _loader->get_bspdata()->numfaces; facenum++ )
        {
                dface_t *face = _loader->get_bspdata()->dfaces + facenum;
                if ( face->lightofs == -1 )
                {
                        // Face does not have a lightmap.
                        continue;
                }

                LightmapSource src;
                src.facenum = facenum;
                if ( face->bumped_lightmap )
                {
                        for ( int n = 0; n < NUM_BUMP_VECTS + 1; n++ )
                        {
                                src.lightmap_img[n] = lightmap_img_for_face( _loader, face, n );
                        }
                }
                else
                {
                        src.lightmap_img[0] = lightmap_img_for_face( _loader, face, 0 );
                }
                
                _sources.push_back( src );
        }

        for ( size_t i = 0; i < _sources.size(); i++ )
        {
                dface_t *face = _loader->get_bspdata()->dfaces + _sources[i].facenum;

#ifdef LMPALETTE_SPLIT
                bool any_fit = false;
                // See if this lightmap can fit in any palette.
                for ( size_t j = 0; j < result_vec.size(); j++ )
                {
                        Palette *ppal = &result_vec[j];
                            
                        if ( ppal->packer->wouldTextureFit( face->lightmap_size[0] + 1, face->lightmap_size[1] + 1, true, false, max_palette, max_palette ) )
                        {
                                ppal->packer->addNewTexture( face->lightmap_size[0] + 1, face->lightmap_size[1] + 1 );
                                ppal->sources.push_back( &_sources[i] );
                                any_fit = true;
                                break;
                        }
                }

                if ( !any_fit )
                {
                        // We need to make a new palette for this lightmap, it won't fit in the current ones.
                        Palette newpal;
                        newpal.packer = TEXTURE_PACKER::createTexturePacker();
                        newpal.packer->addNewTexture( face->lightmap_size[0] + 1, face->lightmap_size[1] + 1 );
                        newpal.sources.push_back( &_sources[i] );
                        result_vec.push_back( newpal );
                }
#else
                result_vec[0].packer->addNewTexture( face->lightmap_size[0] + 1, face->lightmap_size[1] + 1 );
                result_vec[0].sources.push_back( &_sources[i] );
#endif
        }


        // We've found a palette for each lightmap to fit in. Now we need to create the palette and remember
        // the offset into the palette for each face's lightmap.
        for ( size_t i = 0; i < result_vec.size(); i++ )
        {
                Palette *pal = &result_vec[i];
                int width, height;
                pal->packer->packTextures( width, height, true, false );

                PT( LightmapPaletteDirectory::LightmapPaletteEntry ) entry = new LightmapPaletteDirectory::LightmapPaletteEntry;

                for ( int n = 0; n < NUM_BUMP_VECTS + 1; n++ )
                {
                        pal->palette_img[n] = PNMImage( width, height );
                        pal->palette_img[n].fill( 0.0 );
                        entry->palette_tex[n] = new Texture;
                }

                for ( size_t j = 0; j < pal->sources.size(); j++ )
                {
                        LightmapSource *src = pal->sources[j];
                        int xshift, yshift, lmwidth, lmheight;
                        bool rotated = pal->packer->getTextureLocation( j, xshift, yshift, lmwidth, lmheight );

                        PT( LightmapPaletteDirectory::LightmapFacePaletteEntry ) face_entry = new LightmapPaletteDirectory::LightmapFacePaletteEntry;
                        face_entry->palette = entry;
                        face_entry->flipped = rotated;
                        face_entry->xshift = xshift;
                        face_entry->yshift = yshift;
                        face_entry->palette_size[0] = width;
                        face_entry->palette_size[1] = height;

                        if ( _loader->get_bspdata()->dfaces[src->facenum].bumped_lightmap )
                        {
                                for ( int n = 0; n < NUM_BUMP_VECTS + 1; n++ )
                                {
                                        for ( int y = 0; y < lmheight; y++ )
                                        {
                                                for ( int x = 0; x < lmwidth; x++ )
                                                {
                                                        pal->palette_img[n].set_xel( x + xshift, y + yshift, rotated ? src->lightmap_img[n].get_xel( y, x ) : src->lightmap_img[n].get_xel( x, y ) );
                                                }
                                        }
                                }
                                
                        }
                        else
                        {
                                for ( int y = 0; y < lmheight; y++ )
                                {
                                        for ( int x = 0; x < lmwidth; x++ )
                                        {
                                                pal->palette_img[0].set_xel( x + xshift, y + yshift, rotated ? src->lightmap_img[0].get_xel( y, x ) : src->lightmap_img[0].get_xel( x, y ) );
                                        }
                                }
                        }
                        
                        

                        
                        dir.face_index[src->facenum] = face_entry;
                        dir.face_entries.push_back( face_entry );
                }

                for ( int n = 0; n < NUM_BUMP_VECTS + 1; n++ )
                {
                        entry->palette_tex[n]->load( pal->palette_img[n] );
                        entry->palette_tex[n]->set_magfilter( SamplerState::FT_linear );
                        entry->palette_tex[n]->set_minfilter( SamplerState::FT_linear_mipmap_linear );
#if 1
                        stringstream ss;
                        ss << "test_palette_" << n << ".jpg";
                        entry->palette_tex[n]->write( Filename( ss.str() ) );
#endif
                }

                dir.entries.push_back( entry );

                TEXTURE_PACKER::releaseTexturePacker( pal->packer );
                pal->packer = nullptr;
        }

        return dir;
}