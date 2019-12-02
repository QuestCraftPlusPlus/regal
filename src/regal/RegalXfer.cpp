/*
  Copyright (c) 2011-2012 NVIDIA Corporation
  Copyright (c) 2011-2012 Cass Everitt
  Copyright (c) 2012 Scott Nations
  Copyright (c) 2012 Mathias Schott
  Copyright (c) 2012 Nigel Stewart
  All rights reserved.

  Redistribution and use in source and binary forms, with or without modification,
  are permitted provided that the following conditions are met:

    Redistributions of source code must retain the above copyright notice, this
    list of conditions and the following disclaimer.

    Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions and the following disclaimer in the documentation
    and/or other materials provided with the distribution.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
  INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
  OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
  OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "pch.h" /* For MS precompiled header support */

#include "RegalUtil.h"

#if REGAL_EMULATION && REGAL_EMU_XFER

REGAL_GLOBAL_BEGIN

#include <cstring>

#include <limits>
#include <string>
using std::string;

#include <algorithm>

using std::max;
using std::min;

#include <boost/print/string_list.hpp>
typedef boost::print::string_list<string> string_list;

#if !REGAL_NO_SQUISH
#include <squish.h>
#endif

#include "RegalXfer.h"
#include "RegalLog.h"
#include "RegalToken.h"
#include "RegalHelper.h"

// alloca for VC8

#ifdef _MSC_VER
#include <malloc.h>
#define alloca _alloca
#endif

REGAL_GLOBAL_END

REGAL_NAMESPACE_BEGIN

using namespace ::REGAL_NAMESPACE_INTERNAL::helper::size;
using namespace ::REGAL_NAMESPACE_INTERNAL::Logging;
using namespace ::REGAL_NAMESPACE_INTERNAL::Token;

namespace Emu {

  // scrub target format for sRGB on ES 2.0

  inline GLenum TargetFormat(const RegalContext &context, GLenum internalformat, GLenum format)
  {
    if (context.isES2())
    {
      switch( internalformat )
      {
        case GL_SRGB_EXT:
        case GL_SRGB8:
        case GL_COMPRESSED_SRGB_S3TC_DXT1_EXT:
          format = GL_SRGB_EXT;
          break;
        case GL_SRGB_ALPHA_EXT:
        case GL_SRGB8_ALPHA8:
        case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT:
          format = GL_SRGB_ALPHA_EXT;
          break;
        default:
          break;
      }
    }
    return format;
  }

  inline size_t PixelSize( GLenum format, GLenum type )
  {
    switch( type )
    {
      case GL_UNSIGNED_SHORT_5_5_5_1:
      case GL_UNSIGNED_SHORT_1_5_5_5_REV:
      case GL_UNSIGNED_SHORT_5_6_5:
      case GL_UNSIGNED_SHORT_5_6_5_REV:
      case GL_UNSIGNED_SHORT_4_4_4_4:
      case GL_UNSIGNED_SHORT_4_4_4_4_REV:
        return 2;
      case GL_UNSIGNED_INT_10_10_10_2:
      case GL_UNSIGNED_INT_2_10_10_10_REV:
      case GL_UNSIGNED_INT_8_8_8_8:
      case GL_UNSIGNED_INT_8_8_8_8_REV:
        return 4;
      default:
        return componentsPerPixel(format)*bytesPerComponent(type);
    }
  }

  static void SubImage2D( RegalContext * ctx, GLenum target, GLint internalFormat, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels )
  {
    Internal("Regal::Xfer::SubImage2D","ctx=",ctx," target=",Token::GLenumToString(target)," internalFormat=",Token::GLenumToString(internalFormat)," level=",level," format=",Token::GLenumToString(format)," type=",Token::GLenumToString(type));
    RegalAssert(ctx);

    DispatchTableGL & tbl = ctx->dispatcher.emulation;
    int complex = 0;
    int tgttype = 0;
    if ( format == GL_RGBA8 ) {
      // we've seen this illegal format so squash it now
      format = GL_RGBA;
    }
    int tgtfmt = format;
    if ( format == GL_RGBA && type == GL_UNSIGNED_INT_8_8_8_8_REV ) {
      complex = 1;
      tgtfmt = GL_RGBA;
      tgttype = GL_UNSIGNED_BYTE;
    }
    if ( format == GL_BGRA && type == GL_UNSIGNED_INT_8_8_8_8_REV ) {
      // GAB NOTE Dec 2018: Not sure about this one: BGRA+REVERSE is equivalent to RGBA no?
      //complex = 1;
      tgtfmt = GL_RGBA;
      tgttype = GL_UNSIGNED_BYTE;
    }
    if ( format == GL_BGRA && type == GL_UNSIGNED_BYTE ) {
      // GAB NOTE Dec 2018: This one seems to be missing. But not sure
      complex = 1;
      tgtfmt = GL_RGBA;
      tgttype = GL_UNSIGNED_BYTE;
    }
    if ( format == GL_BGR  && type == GL_UNSIGNED_BYTE ) {
      complex = 2 ;
      tgtfmt = GL_RGB;
      tgttype = GL_UNSIGNED_BYTE;
    }

    if( complex ) {
      GLubyte * vline = (GLubyte *)alloca( width * 4 * sizeof( GLint ) );
      int rowLength = ctx->xfer->unpackRowLength;
      int pixelSize = 0;
      pixelSize = (int)PixelSize( format, type );
      if( rowLength == 0 ) {
        rowLength = pixelSize * width;
        RegalAssert( rowLength != 0 );
      } else {
        rowLength = pixelSize * ctx->xfer->unpackRowLength;
      }
      // now unpack the packed formats into their canonical formats
      const GLubyte * pix = static_cast<const GLubyte *>(pixels);
      pix += ctx->xfer->unpackSkipRows * rowLength + ctx->xfer->unpackSkipPixels * pixelSize;
      tbl.glPixelStorei( GL_UNPACK_SKIP_ROWS, 0 );
      tbl.glPixelStorei( GL_UNPACK_SKIP_PIXELS, 0 );
      for( int i = 0; i < height; i++ ) {
        switch( complex ) {
          case 1:
            // BGRA -> RGBA
            if (format == GL_BGRA) {
              for( int j = 0; j < width * 4; j+=4 )
              {
                vline[ j + 0 ] = pix[ j + 2 ];
                vline[ j + 1 ] = pix[ j + 1 ];
                vline[ j + 2 ] = pix[ j + 0 ];
                vline[ j + 3 ] = pix[ j + 3 ];
              }
            } else if (format == GL_RGBA) {
              for( int j = 0; j < width * 4; j+=4 )
              {
                vline[ j + 0 ] = pix[ j + 3 ];
                vline[ j + 1 ] = pix[ j + 2 ];
                vline[ j + 2 ] = pix[ j + 1 ];
                vline[ j + 3 ] = pix[ j + 0 ];
              }
            }
            break;
          case 2:
            for( int j = 0; j < width * 3; j+=3 )
            {
              vline[ j + 0 ] = pix[ j + 2 ];
              vline[ j + 1 ] = pix[ j + 1 ];
              vline[ j + 2 ] = pix[ j + 0 ];
            }
            break;
          default:
            break;
        }
        tbl.glTexSubImage2D( target, level, xoffset, yoffset + i, width, 1, TargetFormat(*ctx, internalFormat, tgtfmt), tgttype, vline );
        pix += rowLength;
      }
      tbl.glPixelStorei( GL_UNPACK_SKIP_ROWS, ctx->xfer->unpackSkipRows );
      tbl.glPixelStorei( GL_UNPACK_SKIP_PIXELS, ctx->xfer->unpackSkipPixels );
    } else {
        tbl.glTexSubImage2D( target, level, xoffset, yoffset, width, height, TargetFormat(*ctx, internalFormat, tgtfmt), type, pixels );
    }
  }

  static bool ShouldDecompress( RegalContext * ctx, GLenum internalFormat )
  {
    if (ctx->info->gl_ext_texture_compression_s3tc)
      return false;

    switch( internalFormat )
    {
      case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:
      case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
        // GAB: added, but need to implement
      case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
      case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
      case GL_COMPRESSED_SRGB_S3TC_DXT1_EXT:
      case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT:
        return true;
        break;
      default:
        break;
    }
    return false;
  }

#if !REGAL_NO_SQUISH

  void DecompressDXT1Line( const GLvoid * data, GLubyte *lines, int width )
  {
    int blocks = width / 4;
    for( int i = 0; i < blocks; i++ ) {
      GLubyte rgba[64];
      squish::Decompress(rgba, ( (const GLubyte *)data ) + i * 8, squish::kDxt1);

      for( int j = 0; j < 4; j++ ) {
        memcpy( lines + i * 16 + j * width * 4, rgba + j * 16, 16 );
      }
    }
  }

  void DecompressDXT5Line( const GLvoid * data, GLubyte *lines, int width )
  {
    int blocks = width / 4;
    for( int i = 0; i < blocks; i++ ) {
      GLubyte rgba[64];
      squish::Decompress(rgba,  ( (const GLubyte *)data ) + i * 16 + 8, squish::kDxt5);
      squish::Decompress(rgba, ( (const GLubyte *)data ) + i * 16, squish::kDxt5);

      for( int j = 0; j < 4; j++ ) {
        memcpy( lines + i * 16 + j * width * 4, rgba + j * 16, 16 );
      }
    }
  }

#endif

  // See also:
  //     [1] http://en.wikipedia.org/wiki/S3_Texture_Compression

  static void CompressedSubImage2D( RegalContext * ctx, GLenum target, GLint level, GLint internalFormat, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const GLvoid *data )
  {
    Internal("Regal::Xfer::CompressedSubImage2D","target=", Token::GLenumToString(target), " level=", level,
             " format=", Token::GLenumToString(format));
    RegalAssert(ctx);
    DispatchTableGL & tbl = ctx->dispatcher.emulation;

#if !REGAL_NO_SQUISH
    if( ShouldDecompress( ctx, format ) )
    {
      Internal("Regal::Xfer::CompressedSubImage2D", "decompressing texture data");
      const int sz = width * 4 * 4;                      // Four scan lines of four bytes (RGBA) per pixel
      GLubyte *vline = (GLubyte *) alloca( sz );         // Allocate enough memory for four scan lines
      memset( vline, 255, sz );                          // Clear to white - debug mode only?
      switch( format )
      {
        // DXT4 and DXT5 (collectively also known as Block Compression 3 or BC3) converts 16 input pixels
        // into 128 bits of output, consisting of 64 bits of alpha channel data (two 8 bit alpha values
        // and a 4x4 3 bit lookup table) followed by 64 bits of color data (encoded the same way as DXT1).[1]
        //
        // 128/16 -> 8 bits per pixel -> one byte per pixel
        // Assumption here is that width is a multiple of 16 pixels?

        case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
        case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT:
          for( GLsizei i = 0; i < height; i+=4 )         // In blocks of four scan lines
          {
            DecompressDXT5Line( static_cast<const GLubyte *>(data) + i * width, vline, width );
            tbl.glTexSubImage2D( target, level, xoffset, yoffset + i, width, 4, TargetFormat(*ctx, internalFormat, GL_RGBA), GL_UNSIGNED_BYTE, vline );
          }
          break;

        // DXT1 (also known as Block Compression 1 or BC1) is the smallest variation of S3TC,
        // storing 16 input pixels in 64 bits of output, consisting of two 16-bit RGB 5:6:5 color
        // values and a 4x4 two bit lookup table.[1]
        //
        // 64/16 -> four bits per pixel -> half a byte per pixel
        // Assumption here is that width is a multiple of 16 pixels?

        case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:
        case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
        case GL_COMPRESSED_SRGB_S3TC_DXT1_EXT:
          for( GLsizei i = 0; i < height; i+=4 )             // In blocks of four scan lines
          {
            DecompressDXT1Line( static_cast<const GLubyte *>(data) + i * width / 2, vline, width );
            tbl.glTexSubImage2D( target, level, xoffset, yoffset + i, width, 4, TargetFormat(*ctx, internalFormat, GL_RGBA), GL_UNSIGNED_BYTE, vline );
          }
          break;

          case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
            // GAB: Need to implement
            break;
          default:
          break;
      }
    }
    else
#endif
    {
      tbl.glCompressedTexSubImage2D( target, level, xoffset, yoffset, width, height, TargetFormat(*ctx, internalFormat, format), imageSize, data );
    }
  }

void Xfer::PixelStore( RegalContext * ctx, GLenum pname, GLint param )
{
  UNUSED_PARAMETER(ctx);

  switch( pname ) {
    case GL_UNPACK_ROW_LENGTH: unpackRowLength = param; break;
    case GL_UNPACK_SKIP_ROWS: unpackSkipRows = param; break;
    case GL_UNPACK_SKIP_PIXELS: unpackSkipPixels = param; break;
    default: break;
  }
}

void Xfer::TexImage2D( RegalContext * ctx, GLenum target, GLint level, GLint internalFormat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels )
{
  Internal("Regal::Xfer::TexImage2D","ctx=",ctx," target=",Token::GLenumToString(target)," level=",level," internalFormat=",Token::GLenumToString(internalFormat));
  RegalAssert(ctx);
  DispatchTableGL &tbl = ctx->dispatcher.emulation;

  if (ctx->isCore() || ctx->isES2())
  {
    switch (internalFormat)
    {
      case 3: internalFormat = GL_RGB;  break;
      case 4: internalFormat = GL_RGBA; break;
      default:                          break;
    }
  }

  switch (internalFormat)
  {
#ifdef __EMSCRITPEN__
      // GAB Note Dec 2018: we could possibly handle:
      //   GL_RGBA4   => using GL_RGBA/GL_RGBA with GL_UNSIGNED_SHORT_4_4_4_4 type
      //   GL_RGB5_A1 => using GL_RGBA/GL_RGBA with GL_UNSIGNED_SHORT_5_5_5_1 type
      // TO BE TESTED
#else
    // Only supported in desktop mode
    case GL_RGB5:
      tbl.glTexImage2D( target, level, internalFormat, width, height, border, format, type, pixels );
      return;

    // Formats common to GL and ES 2.0, just pass-through directly.
    case GL_RGB5_A1:
      tbl.glTexImage2D( target, level, internalFormat, width, height, border, format, type, pixels );
      return;
#endif

      default:
      break;
  }

  // Scrub the internal format for ES2.0

  switch (internalFormat)
  {
    case GL_SLUMINANCE8:       internalFormat = GL_SLUMINANCE;      break;
    case GL_DEPTH_COMPONENT16: internalFormat = GL_DEPTH_COMPONENT; break;
    case GL_SRGB8:             internalFormat = GL_SRGB_EXT;        break;
    case GL_SRGB8_ALPHA8:      internalFormat = GL_SRGB_ALPHA_EXT;  break;
    default:                                                        break;
  }

  GLenum targetType = type;
  GLenum targetFormat = format;

  if (type==GL_UNSIGNED_INT_8_8_8_8_REV)
    targetType = GL_UNSIGNED_BYTE;

  switch (format)
  {
    case GL_RGBA8:   targetFormat = GL_RGBA; break;
    case GL_BGR:     targetFormat = GL_RGB;  break;
    case GL_BGRA:    targetFormat = GL_RGBA; break;
    default:                                 break;
  }

  targetFormat = TargetFormat(*ctx, internalFormat, targetFormat);

  // For ES 2.0, internalFormat == targetFormat

  if (ctx->isES2())
    internalFormat = targetFormat;

  // GAB Note Dec 2018: I think passing NULL to the pixel argument is something that makes Chrome complaining
  tbl.glTexImage2D( target, level, internalFormat, width, height, border, targetFormat, targetType, NULL );
  name2ifmt[ textureBinding2D[ activeTextureIndex ] ] = internalFormat;
  if( pixels ) {
    SubImage2D( ctx, target, internalFormat, level, 0, 0, width, height, format, type, pixels );
  }
}

void Xfer::TexSubImage2D( RegalContext * ctx, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels )
{
  GLint internalFormat = name2ifmt[ textureBinding2D[ activeTextureIndex ] ];
  Internal("Regal::Xfer::TexSubImage2D","target=", Token::GLenumToString(target), " level=", level,
           " internalFormat=", Token::GLenumToString(internalFormat), " format=", Token::GLenumToString(format));
  SubImage2D( ctx, target, internalFormat, level , xoffset, yoffset, width, height, format, type, pixels );
}

void Xfer::CompressedTexImage2D( RegalContext * ctx, GLenum target, GLint level, GLint internalFormat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const GLvoid *data )
{
  Internal("Regal::Xfer::CompressedTexImage2D","target=", Token::GLenumToString(target), " level=", level,
           " format=", Token::GLenumToString(internalFormat));

  RegalAssert(ctx);
  DispatchTableGL & tbl = ctx->dispatcher.emulation;
  if( ShouldDecompress( ctx, internalFormat ) ) {
    Internal("Regal::Xfer::CompressedTexImage2D","decompressing texture data");
    GLenum ifmt = GL_RGBA;
    switch( internalFormat ) {
      case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:
      case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
      case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
      case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
      case GL_COMPRESSED_SRGB_S3TC_DXT1_EXT:
      case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT:
        if (internalFormat == GL_COMPRESSED_SRGB_S3TC_DXT1_EXT ||
            internalFormat == GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT)
          ifmt = GL_SRGB_ALPHA_EXT;
        tbl.glTexImage2D( target, level, ifmt, width, height, border, TargetFormat(*ctx, ifmt, GL_RGBA), GL_UNSIGNED_BYTE, NULL );
        if( width == 4 && height == 4 ) {
          const GLubyte tail[] = { 255, 255, 255, 255,
                                   255, 255, 255, 255,
                                   255, 255, 255, 255,
                                   255, 255, 255, 255 };
          tbl.glTexImage2D( target, level + 1, ifmt, 2, 2, border, TargetFormat(*ctx, ifmt, GL_RGBA), GL_UNSIGNED_BYTE, tail );
          tbl.glTexImage2D( target, level + 2, ifmt, 1, 1, border, TargetFormat(*ctx, ifmt, GL_RGBA), GL_UNSIGNED_BYTE, tail );
        }
        break;
      default:
        break;
    }
    name2ifmt[ textureBinding2D[ activeTextureIndex ] ] = internalFormat;
    if( imageSize ) {
      GLint ifmt = name2ifmt[ textureBinding2D[ activeTextureIndex ] ];
      CompressedSubImage2D( ctx, target, level, ifmt, 0, 0, width, height, internalFormat, imageSize, data );
    }
  } else {
     name2ifmt[ textureBinding2D[ activeTextureIndex ] ] = internalFormat;
     tbl.glCompressedTexImage2D( target, level, internalFormat, width, height, border, imageSize, data );
  }
}

void Xfer::CompressedTexSubImage2D( RegalContext * ctx, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const GLvoid *data )
{
  UNUSED_PARAMETER(format);
  GLint ifmt = name2ifmt[ textureBinding2D[ activeTextureIndex ] ];
  Internal("Regal::Xfer::CompressedTexSubImage2D","target=", Token::GLenumToString(target), " level=", level,
           " format=", Token::GLenumToString(format), " iformat=", Token::GLenumToString(ifmt));

  CompressedSubImage2D( ctx, target, level, ifmt, xoffset, yoffset, width, height, ifmt, imageSize, data );
}

}

REGAL_NAMESPACE_END

#endif // REGAL_EMULATION && REGAL_EMU_XFER
