
/*
  Copyright (c) 2011-2013 NVIDIA Corporation
  Copyright (c) 2011-2013 Cass Everitt
  Copyright (c) 2012 Scott Nations
  Copyright (c) 2012 Mathias Schott
  Copyright (c) 2012 Nigel Stewart
  Copyright (c) 2013 Seth Williams
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

#if REGAL_EMULATION

REGAL_GLOBAL_BEGIN

#include <cstring>

#include <string>
using std::string;

#include <boost/print/string_list.hpp>
typedef boost::print::string_list<string> string_list;

#include "RegalIff.h"
#include "RegalLog.h"
#include "RegalToken.h"
#include "RegalHelper.h"
#include "RegalShader.h"

REGAL_GLOBAL_END

REGAL_NAMESPACE_BEGIN

namespace Emu
{

using namespace ::REGAL_NAMESPACE_INTERNAL::Logging;
using namespace ::REGAL_NAMESPACE_INTERNAL::Shader;
using namespace ::REGAL_NAMESPACE_INTERNAL::Token;

typedef Iff::State State;
typedef Iff::State::Texture Texture;
typedef Iff::State::Light Light;
typedef Iff::State::MaterialUniform MaterialUniform;
typedef Iff::State::Store Store;
typedef Iff::State::StoreUniform StoreUniform;
typedef Iff::Program Program;

enum FFLightEl
{
  LE_Ambient = 0,
  LE_Diffuse = 1,
  LE_Specular = 2,
  LE_Position = 3,
  LE_SpotDir = 4,
  LE_Atten = 5,
  LE_Elements = 6
};
enum FFMaterialEl
{
  ME_Ambient = 0,
  ME_Diffuse = 1,
  ME_Specular = 2,
  ME_Emission = 3,
  ME_Shininess = 4,
  ME_Elements = 5
};

static Iff::TextureTargetBitfield TargetToBitfield( GLenum target )
{
  switch( target )
  {
    case GL_TEXTURE_1D:
      return Iff::TT_1D;
    case GL_TEXTURE_2D:
      return Iff::TT_2D;
    case GL_TEXTURE_RECTANGLE:
      return Iff::TT_Rect;
    case GL_TEXTURE_3D:
      return Iff::TT_3D;
    case GL_TEXTURE_CUBE_MAP:
      return Iff::TT_CubeMap;
    default:
      break;
  }
  return Iff::TT_None;
}

static void GenerateVertexShaderSource( const Iff * rff, const Iff::State & state, string_list & src )
{
  Internal("Regal::Iff::GenerateVertexShaderSource", boost::print::optional(rff,Logging::pointers));

  const bool gles = rff->gles;
  const bool legacy = rff->legacy;
  const Store & st = state.processed;

  bool hasNormalMap = false;
  bool hasSphereMap = false;
  bool hasReflectionMap = false;
  bool hasEyeLinearTexGen = false;

  size_t n = array_size( st.tex );
  for ( size_t i = 0; i < n; i++ )
  {
    RegalAssertArrayIndex( st.tex, i );
    if ( st.tex[i].enables == 0 )
    {
      continue;
    }
    for ( size_t j = 0; j < 4; j++ )
    {
      RegalAssertArrayIndex( st.tex[i].texgen, j );
      if ( st.tex[i].texgen[j].enable == false )
      {
        continue;
      }
      Iff::TexgenMode mode = st.tex[i].texgen[j].mode;
      hasNormalMap = hasNormalMap || mode == Iff::TG_NormalMap;
      hasSphereMap = hasSphereMap || mode == Iff::TG_SphereMap;
      hasReflectionMap = hasReflectionMap || mode == Iff::TG_ReflectionMap;
      hasEyeLinearTexGen = hasEyeLinearTexGen || mode == Iff::TG_EyeLinear;
    }
  }
  bool hasClipPlanes = false;
  n = array_size( st.clipPlaneEnabled );
  for ( size_t i = 0; i < n; i++ )
  {
    RegalAssertArrayIndex( st.clipPlaneEnabled, i );
    hasClipPlanes = hasClipPlanes || st.clipPlaneEnabled[i];
  }

  if ( gles )
  {
#if REGAL_FORCE_DESKTOP_GLSL
    // Desktop version 140 corresponds to OpenGL 3.1,
    // which corresponds to the functionality of ES2.
    src << "#version 140\n";
#else
    src << "#version 100\n";
#endif
  }
  else if ( legacy )
  {
    src << "#version 120\n";
  }
  else
  {
    src << "#version 140\n";
  }
  src << "// program number " << rff->progcount << "\n";

  if ( gles || legacy )
  {
    src << "#define in attribute\n";
    src << "#define out varying\n";
  }

  if ( st.shadeModelFlat && ! legacy && ! gles )
  {
    src << "#define FLAT flat\n";
  }
  else
  {
    src << "#define FLAT  \n";
  }
  if ( st.lighting )
  {
    src << "\n";
    src << "#define ME_AMBIENT "   << int(ME_Ambient) << "\n";
    src << "#define ME_DIFFUSE "   << int(ME_Diffuse) << "\n";
    src << "#define ME_SPECULAR "  << int(ME_Specular) << "\n";
    src << "#define ME_EMISSION "  << int(ME_Emission) << "\n";
    src << "#define ME_SHININESS " << int(ME_Shininess) << "\n";
    src << "#define ME_ELEMENTS "  << int(ME_Elements) << "\n";
    src << "\n";
    src << "#define LE_AMBIENT "  << int(LE_Ambient) << "\n";
    src << "#define LE_DIFFUSE "  << int(LE_Diffuse) << "\n";
    src << "#define LE_SPECULAR " << int(LE_Specular) << "\n";
    src << "#define LE_POSITION " << int(LE_Position) << "\n";
    src << "#define LE_SPOTDIR "  << int(LE_SpotDir) << "\n";
    src << "#define LE_ATTEN "    << int(LE_Atten) << "\n";
    src << "#define LE_ELEMENTS " << int(LE_Elements) << "\n";
    src << "\n";
  }

  if ( gles )
  {
    src << "precision highp float;\n";
  }
  RegalAssertArrayIndex( rff->ffAttrMap, RFF2A_Color );
  if ( ~st.attrArrayFlags & ( 1 << rff->ffAttrMap[ RFF2A_Color ] ) )
  {
    src << "uniform vec4 rglAttrib[" << REGAL_EMU_MAX_VERTEX_ATTRIBS << "];\n";
  }
  src << "uniform mat4 rglModelViewMatrix;\n";
  src << "uniform mat4 rglProjectionMatrix;\n";
  src << "in vec4 rglVertex;\n";
  n = array_size( st.tex );
  for ( size_t i = 0; i < n; i++ )
  {
    RegalAssertArrayIndex( st.tex, i );
    if ( st.tex[i].useMatrix )
    {
      src << "uniform mat4 rglTextureMatrix" << i << ";\n";
    }
  }
  n = array_size( st.clipPlaneEnabled );
  for ( size_t i = 0; i < n; i++ )
  {
    RegalAssertArrayIndex( st.clipPlaneEnabled, i );
    if ( st.clipPlaneEnabled[i] )
    {
      src << "uniform vec4 rglClipPlane" << i << ";\n";
      if ( gles || legacy )
      {
        src << "out float rglClipDistance" << i << ";\n";
      }
    }
  }
  //src << "in vec4 rglWeight;\n";
  if ( st.lighting || hasNormalMap || hasSphereMap || hasReflectionMap )
  {
    src << "uniform mat3 rglNormalMatrix;\n";
    src << "in vec3 rglNormal;\n";
  }
  if ( st.lighting )
  {
    src << "uniform vec4 rglLightModelAmbient;\n";
    src << "uniform vec4 rglMaterialFront" << "[ ME_ELEMENTS ];\n";
    if ( st.lightModelTwoSide )
    {
      src << "uniform vec4 rglMaterialBack" << "[ ME_ELEMENTS ];\n";
    }
    for ( size_t i = 0; i < REGAL_FIXED_FUNCTION_MAX_LIGHTS; i++ )
    {
      if ( st.light[ i ].enable )
      {
        src << "uniform vec4 rglLight" << i << "[ LE_ELEMENTS ];\n";
      }
    }
  }
  RegalAssertArrayIndex( rff->ffAttrMap, RFF2A_Color );
  if ( st.attrArrayFlags & ( 1 << rff->ffAttrMap[ RFF2A_Color ] ) && ( st.lighting == false || st.colorMaterial ) )
  {
    src << "in vec4 rglColor;\n";
  }
  else
  {
    src << "#define rglColor rglAttrib[" << rff->ffAttrMap[ RFF2A_Color ] << "]\n";
  }
  if ( st.colorSum && st.lighting == false )
  {
    src << "in vec4 rglSecondaryColor;\n";
  }
  if ( st.fog.enable )
  {
    src << "uniform vec4 rglFog[2];\n";
    src << "out vec4 rglFOG;\n";
    if (  st.fog.useDepth == false )
    {
      src << "in float rglFogCoord;\n";
    }
  }
  n = array_size( st.tex );
  for ( size_t i = 0; i < n; i++ )
  {
    RegalAssertArrayIndex( st.tex, i );
    if ( st.tex[i].enables == 0 )
    {
      continue;
    }
    src << "in vec4 rglMultiTexCoord" << i << ";\n";
  }
  src << "FLAT out vec4 rglFrontColor;\n";
  if ( st.lighting )
  {
    if ( st.lightModelTwoSide )
    {
      src << "FLAT out vec4 rglBackColor;\n";
    }
    if ( st.lightModelSeparateSpecular )
    {
      src << "out vec4 rglSCOL0;\n";
      if ( st.lightModelTwoSide )
      {
        src << "out vec4 rglSCOL1;\n";
      }
    }
  }
  else if( st.colorSum )
  {
    src << "out vec4 rglSCOL0;\n";
  }

  n = array_size( st.tex );
  // GAB Note Jan 2019: define rglTEXCOORD like gl_TexCoord, that is an array of vec4
  if (n > 0) {
    src << "out vec4 rglTEXCOORD[" << n << "];\n";
  }
  for ( size_t i = 0; i < n; i++ )
  {
    RegalAssertArrayIndex( st.tex, i );
    const State::Texture & t = st.tex[i];
    if ( t.enables == 0 )
    {
      continue;
    }
    const char *tc[] = { "S", "T", "R", "Q" };
    for ( size_t j = 0; j < 4; j++ )
    {
      RegalAssertArrayIndex( t.texgen, j );
      const State::Texgen & g = t.texgen[j];
      if ( g.enable == false )
      {
        continue;
      }
      switch( g.mode )
      {
        case Iff::TG_EyeLinear:
          src << "uniform vec4 rglTexGen" << i << "Eye" << tc[j] << ";\n";
          break;
        case Iff::TG_ObjectLinear:
          src << "uniform vec4 rglTexGen" << i << "Obj" << tc[j] << ";\n";
          break;
        default:
          src << "//ERROR: unsupported gen mode\n";
          break;
      }
    }
  }

  if ( hasNormalMap )
  {
    src << "vec4 ComputeNormalMap( vec3 n ) {\n";
    src << "    return vec4( n.x, n.y, n.z, 0.0 );\n";
    src << "}\n";
  }

  if ( hasReflectionMap )
  {
    src << "vec4 ComputeReflectionMap( vec3 i, vec3 n ) {\n";
    src << "    vec3 r = reflect( i, n );\n";
    src << "    return vec4( r.x, r.y, r.z, 0.0 );\n";
    src << "}\n";
  }

  if ( hasSphereMap )
  {
    src << "vec4 ComputeSphereMap( vec3 i, vec3 n ) {\n";
    src << "    vec3 r = reflect( i, n );\n";
    src << "    float minv = 1.0 / sqrt( 2.0 * r.x + 2.0 * r.y + 2.0 * (r.z + 1.0 ) );\n";
    src << "    return vec4( r.x * minv + 0.5 , r.y * minv + 0.5, 0.0, 0.0 );\n";
    src << "}\n";
  }

  src << "void main() {\n";
  src << "    gl_Position = rglProjectionMatrix * rglModelViewMatrix * rglVertex;\n";
  if ( st.lighting || hasNormalMap || hasReflectionMap || hasEyeLinearTexGen ||
      hasClipPlanes || ( st.fog.enable && st.fog.useDepth ) || hasSphereMap )
  {
    src << "    vec4 eh = rglModelViewMatrix * rglVertex;\n";
  }

  if ( st.lighting || hasNormalMap || hasReflectionMap || hasSphereMap )
  {
    src << "    vec4 ep = eh / eh.w;\n";
    src << "    vec3 ev = -normalize( ep.xyz );\n";
    src << "    vec3 en = rglNormalMatrix * rglNormal;\n";
    if ( st.normalize )
    {
      src << "    en = normalize( en );\n";
    }
  }
  if ( st.lighting )
  {
    src << "    vec4 mFront[ ME_ELEMENTS ];\n";
    src << "    mFront[ ME_AMBIENT   ] = rglMaterialFront[ ME_AMBIENT   ];\n";
    src << "    mFront[ ME_DIFFUSE   ] = rglMaterialFront[ ME_DIFFUSE   ];\n";
    src << "    mFront[ ME_SPECULAR  ] = rglMaterialFront[ ME_SPECULAR  ];\n";
    src << "    mFront[ ME_EMISSION  ] = rglMaterialFront[ ME_EMISSION  ];\n";
    src << "    mFront[ ME_SHININESS ] = rglMaterialFront[ ME_SHININESS ];\n";
    if ( st.lightModelTwoSide )
    {
      src << "    vec4 mBack [ ME_ELEMENTS ];\n";
      src << "    mBack [ ME_AMBIENT   ] = rglMaterialBack [ ME_AMBIENT   ];\n";
      src << "    mBack [ ME_DIFFUSE   ] = rglMaterialBack [ ME_DIFFUSE   ];\n";
      src << "    mBack [ ME_SPECULAR  ] = rglMaterialBack [ ME_SPECULAR  ];\n";
      src << "    mBack [ ME_EMISSION  ] = rglMaterialBack [ ME_EMISSION  ];\n";
      src << "    mBack [ ME_SHININESS ] = rglMaterialBack [ ME_SHININESS ];\n";
    }
    if ( st.colorMaterial )
    {
      switch( st.colorMaterialTarget0 )
      {
        case Iff::CM_None:
          break;
        case Iff::CM_Ambient:
          src << "    mFront[ ME_AMBIENT ] = rglColor;\n";
          break;
        case Iff::CM_Diffuse:
          src << "    mFront[ ME_DIFFUSE ] = rglColor;\n";
          break;
        case Iff::CM_Specular:
          src << "    mFront[ ME_SPECULAR ] = rglColor;\n";
          break;
        case Iff::CM_Emission:
          src << "    mFront[ ME_EMISSION ] = rglColor;\n";
          break;
        case Iff::CM_AmbientAndDiffuse:
          src << "    mFront[ ME_AMBIENT ] = rglColor;\n";
          src << "    mFront[ ME_DIFFUSE ] = rglColor;\n";
          break;
        default:
          src << "//ERROR: unsupported color material[0] target\n";
          break;
      }
      if ( st.lightModelTwoSide )
      {
        switch( st.colorMaterialTarget1 )
        {
          case Iff::CM_None:
            break;
          case Iff::CM_Ambient:
            src << "    mBack[ ME_AMBIENT ] = rglColor;\n";
            break;
          case Iff::CM_Diffuse:
            src << "    mBack[ ME_DIFFUSE ] = rglColor;\n";
            break;
          case Iff::CM_Specular:
            src << "    mBack[ ME_SPECULAR ] = rglColor;\n";
            break;
          case Iff::CM_Emission:
            src << "    mBack[ ME_EMISSION ] = rglColor;\n";
            break;
          case Iff::CM_AmbientAndDiffuse:
            src << "    mBack[ ME_AMBIENT ] = rglColor;\n";
            src << "    mBack[ ME_DIFFUSE ] = rglColor;\n";
            break;
          default:
            src << "//ERROR: unsupported color material[1] target\n";
            break;
        }
      }
    }
    src << "    rglFrontColor.xyz = mFront[ ME_EMISSION ].xyz;\n";
    src << "    rglFrontColor.w = mFront[ ME_DIFFUSE ].w;\n";
    src << "    rglFrontColor.xyz += rglLightModelAmbient.xyz * mFront[ ME_AMBIENT ].xyz;\n";
    if ( st.lightModelSeparateSpecular )
    {
      src << "    rglSCOL0 = vec4( 0, 0, 0, 0);\n";
    }
    if ( st.lightModelTwoSide )
    {
      src << "    rglBackColor.xyz = mBack[ ME_EMISSION ].xyz;\n";
      src << "    rglBackColor.w = mBack[ ME_DIFFUSE ].w;\n";
      src << "    rglBackColor.xyz += rglLightModelAmbient.xyz * mBack[ ME_AMBIENT ].xyz;\n";
      if ( st.lightModelSeparateSpecular )
      {
        src << "    rglSCOL1 = vec4( 0, 0, 0, 0);\n";
      }
    }
    n = array_size( st.light );
    for ( size_t i = 0; i < n; i++ )
    {
      RegalAssertArrayIndex( st.light, i );
      if ( st.light[ i ].enable )
      {
        src << "    {\n";
        string attenmul = "";
        src << "        vec4 lvec = rglLight" << i << "[ LE_POSITION ];\n";
        if ( st.light[ i ].attenuate || st.light[ i ].spotlight )
        {
          src << "        float att = 1.0;\n";
          attenmul = "att *";
        }
        if ( st.light[ i ].local )
        {
          src << "        lvec.xyz -= ep.xyz;\n";
          src << "        float ad = sqrt( dot( lvec.xyz, lvec.xyz ) );\n";
          src << "        lvec.xyz /= ad;\n";
        }
        if ( st.lightModelLocalViewer )
        {
          src << "        vec3 hvec = normalize( lvec.xyz + ev );\n";
        }
        else
        {
          src << "        vec3 hvec = normalize( lvec.xyz + vec3( 0, 0, 1 ) );\n";
        }
        src << "        vec3 ambient = rglLight" << i << "[ LE_AMBIENT ].xyz * mFront[ ME_AMBIENT ].xyz;\n";
        src << "        float dc = max( dot( en, lvec.xyz ), 0.0 );\n";
        src << "        float sc = max( dot( en, hvec ), 0.0 );\n";
        src << "        vec3 diffuse = dc * rglLight" << i << "[ LE_DIFFUSE ].xyz * mFront[ ME_DIFFUSE ].xyz;\n";
        src << "        sc = ( dc > 0.0 && sc > 0.0 ) ? exp( mFront[ ME_SHININESS ].x * log( sc ) ) : 0.0;\n";
        src << "        vec3 specular = sc * rglLight" << i << "[ LE_SPECULAR ].xyz * mFront[ ME_SPECULAR ].xyz;\n";
        if ( st.light[ i ].attenuate && st.light[ i ].local )
        {
          src << "        vec3 dist = vec3( 1.0, ad, ad * ad );\n";
          src << "        att = 1.0 / dot( dist, rglLight" << i << "[ LE_ATTEN ].xyz );\n";
        }
        if ( st.light[ i ].spotlight )
        {
          src << "        float spcut = cos( radians( rglLight" << i << "[ LE_SPOTDIR ].w ) );\n";
          src << "        float sd = dot( rglLight" << i << "[ LE_SPOTDIR ].xyz, lvec.xyz );\n";
          src << "        att *= ( sd > spcut ) ? exp( rglLight" << i << "[ LE_ATTEN ].w * log( sd ) ) : 0.0;\n";
        }

        if ( st.lightModelSeparateSpecular )
        {
          src << "        rglFrontColor.xyz += " << attenmul << " ( ambient + diffuse );\n";
          src << "        rglSCOL0.xyz += " << attenmul << " specular;\n";
        }
        else
        {
          src << "        rglFrontColor.xyz += " << attenmul << " ( ambient + diffuse + specular );\n";
        }
        if ( st.lightModelTwoSide )
        {
          src << "        ambient = rglLight" << i << "[ LE_AMBIENT ].xyz * mBack[ ME_AMBIENT ].xyz;\n";
          src << "        dc = max( dot( -en, lvec.xyz ), 0.0 );\n";
          src << "        sc = max( dot( -en, hvec ), 0.0 );\n";
          src << "        diffuse = dc * rglLight" << i << "[ LE_DIFFUSE ].xyz * mBack[ ME_DIFFUSE ].xyz;\n";
          src << "        sc = ( dc > 0.0 && sc > 0.0 ) ? exp( mBack[ ME_SHININESS ].x * log( sc ) ) : 0.0;\n";
          src << "        specular = sc * rglLight" << i << "[ LE_SPECULAR ].xyz * mBack[ ME_SPECULAR ].xyz;\n";
          if ( st.lightModelSeparateSpecular )
          {
            src << "        rglBackColor.xyz += " << attenmul << " ( ambient + diffuse );\n";
            src << "        rglSCOL1.xyz += " << attenmul << " specular;\n";
          }
          else
          {
            src << "        rglBackColor.xyz += " << attenmul << " ( ambient + diffuse + specular );\n";
          }
        }
        src << "    }\n";
      }
    }
  }
  else
  {
    src << "    rglFrontColor = rglColor;\n";
    if ( st.colorSum )
    {
      src << "    rglSCOL0 = rglSecondaryColor;\n";
    }
  }

  // clamp all the output colors to (0.0, 1.0)

  src << "    rglFrontColor = clamp( rglFrontColor, 0.0, 1.0 );\n";
  if ( st.lighting )
  {
    if ( st.lightModelTwoSide )
    {
      src << "    rglBackColor = clamp( rglBackColor, 0.0, 1.0 );\n";
    }
    if ( st.lightModelSeparateSpecular )
    {
      src << "    rglSCOL0 = clamp( rglSCOL0, 0.0, 1.0 );\n";
      if ( st.lightModelTwoSide )
      {
        src << "    rglSCOL1 = clamp( rglSCOL1, 0.0, 1.0 );\n";
      }
    }
  }
  else if( st.colorSum )
  {
    src << "    rglSCOL0 = clamp( rglSCOL0, 0.0, 1.0 );\n";
  }

  if ( st.fog.enable )
  {
    if ( st.fog.useDepth )
    {
      src << "    rglFOG = eh;\n";
    }
    else
    {
      src << "    rglFOG = vec4(0, 0, -rglFogCoord, 1);\n";
    }
  }

  if ( hasNormalMap )
  {
    src << "    vec4 nmTc = ComputeNormalMap( en );\n";
  }
  if ( hasReflectionMap )
  {
    src << "    vec4 rmTc = ComputeReflectionMap( -ev, en );\n";
  }
  if ( hasSphereMap )
  {
    src << "    vec4 smTc = ComputeSphereMap( -ev, en );\n";
  }

  bool tc_declared = false;
  n = array_size( state.processed.tex );
  for ( size_t i = 0; i < n; i++ )
  {
    RegalAssertArrayIndex( state.processed.tex, i );
    const State::Texture & t = state.processed.tex[i];
    if ( t.enables == 0 )
    {
      continue;
    }
    if ( !tc_declared )
    {
      src << "    vec4 tc;\n";
      tc_declared = true;
    }
    src << "    tc = rglMultiTexCoord" << i << ";\n";
    const char *comp[] = { "x", "y", "z", "w" };
    const char *tc[] = { "S", "T", "R", "Q" };
    for ( size_t j = 0; j < 4; j++ )
    {
      RegalAssertArrayIndex( t.texgen, j );
      if ( t.texgen[j].enable == false )
      {
        continue;
      }
      switch( t.texgen[j].mode )
      {
        case Iff::TG_ObjectLinear:
          src << "    tc." << comp[j] << " = dot( rglTexGen" << i << "Obj" << tc[j] << ", rglVertex );\n";
          break;
        case Iff::TG_EyeLinear:
          src << "    tc." << comp[j] << " = dot( rglTexGen" << i << "Eye" << tc[j] << ", eh );\n";
          break;
        case Iff::TG_NormalMap:
          src << "    tc." << comp[j] << " = nmTc." << comp[j] << ";\n";
          break;
        case Iff::TG_ReflectionMap:
          src << "    tc." << comp[j] << " = rmTc." << comp[j] << ";\n";
          break;
        case Iff::TG_SphereMap:
          src << "    tc." << comp[j] << " = smTc." << comp[j] << ";\n";
          break;
        default:
          src << "//ERROR: unsupported tex gen mode\n";
          break;
      }
    }
    if ( t.useMatrix )
    {
      src << "    rglTEXCOORD[" << i << "] = rglTextureMatrix" << i << " * tc;\n";
    }
    else
    {
      src << "    rglTEXCOORD[" << i << "] = tc;\n";
    }
  }
  n = array_size( st.clipPlaneEnabled );
  for ( size_t i = 0; i < n; i++ )
  {
    RegalAssertArrayIndex( st.clipPlaneEnabled, i );
    if ( st.clipPlaneEnabled[i] )
    {
      if ( gles || legacy )
      {
        src << "    rglClipDistance" << i;
      }
      else
      {
        src << "    gl_ClipDistance[" << i << "]";
      }
      src << " = dot( eh, rglClipPlane" << i << " );\n";
    }
  }
  src << "}\n";
}

static void AddTexEnv( size_t i, Iff::TexenvMode env, GLenum fmt,  string_list & s )
{
  Internal("Regal::Iff::AddTexEnv","()");

  switch( env )
  {
    case Iff::TEM_Replace:
      switch( fmt )
      {
        case GL_ALPHA:
          s << "    p.w = s.w;\n";
          break;
        case GL_LUMINANCE:
        case GL_RGB:
          s << "    p.xyz = s.xyz;\n";
          break;
        case GL_LUMINANCE_ALPHA:
        case GL_RGBA:
          s << "    p = s;\n";
          break;
        default:
          s << "    p = s; //ERROR: Unsupported tex fmt " << Token::GLenumToString(fmt) << "\n";
          break;
      }
      break;
    case Iff::TEM_Modulate:
      switch( fmt )
      {
        case GL_ALPHA:
          s << "    p.w *= s.w;\n";
          break;
        case GL_LUMINANCE:
        case GL_RGB:
          s << "    p.xyz *= s.xyz;\n";
          break;
        case GL_LUMINANCE_ALPHA:
        case GL_RGBA:
          s << "    p *= s;\n";
          break;
        default:
          s << "    p = s; //ERROR: Unsupported tex fmt" << Token::GLenumToString(fmt) << "\n";
          break;
      }
      break;
    case Iff::TEM_Blend:
      switch( fmt )
      {
        case GL_ALPHA:
          s << "    p.w *= s.w;\n";
          break;
        case GL_LUMINANCE:
        case GL_RGB:
          s << "    p.xyz = mix( p.xyz, rglTexEnvColor" << i << ".xyz, s.xyz );\n";
          break;
        case GL_LUMINANCE_ALPHA:
        case GL_RGBA:
          s << "    p.xyz = mix( p.xyz, rglTexEnvColor" << i << ".xyz, s.xyz );\n";
          s << "    p.w *= s.w;\n";
          break;
        default:
          s << "    p = s; //ERROR: Unsupported tex fmt " << Token::GLenumToString(fmt) << "\n";
          break;
      }
      break;
    case Iff::TEM_Add:
      switch( fmt )
      {
        case GL_ALPHA:
          s << "    p.w *= s.w;\n";
          break;
        case GL_LUMINANCE:
        case GL_RGB:
          s << "    p.xyz += s.xyz;\n";
          break;
        case GL_LUMINANCE_ALPHA:
        case GL_RGBA:
          s << "    p.xyz += s.xyz;\n";
          s << "    p.w *= s.w;\n";
          break;
        default:
          s << "    p = s; //ERROR: Unsupported tex fmt " << Token::GLenumToString(fmt) << "\n";
          break;
      }
      break;
    case Iff::TEM_Decal:
      switch( fmt )
      {
        case GL_ALPHA:
        case GL_LUMINANCE:
        case GL_LUMINANCE_ALPHA:
          s << "    //ERROR: tex env mode undefined for texture format " << Token::GLenumToString(fmt) << "\n";
          break;
        case GL_RGB:
          s << "    p.xyz = s.xyz;\n";
          break;
        case GL_RGBA:
          s << "    p.xyz = mix( p.xyz, s.xyz, s.w );\n";
          break;
        default:
          s << "    p = s; //ERROR: Unsupported tex fmt " << Token::GLenumToString(fmt) << "\n";
          break;
      }
      break;
    default:
      s << "    //ERROR: Unsupported tex env mode\n";
      break;
  }
}

static void AddTexEnvCombine( Iff::TextureEnv & env, string_list & s )
{
  Internal("Regal::Iff::AddTexEnvCombine","()");

  bool skipAlpha = env.rgb.mode == Iff::TEC_Dot3Rgba;
  int rgbSources = 0;
  int aSources = 0;
  s << "    {\n";
  switch( env.rgb.mode )
  {
    case Iff::TEC_Replace:
      rgbSources = 1;
      break;
    case Iff::TEC_Modulate:
    case Iff::TEC_Add:
    case Iff::TEC_AddSigned:
    case Iff::TEC_Dot3Rgb:
    case Iff::TEC_Dot3Rgba:
      rgbSources = 2;
      break;
    case Iff::TEC_Interpolate:
      rgbSources = 3;
      break;
    default:
      break;
  }
  if ( skipAlpha == false )
  {
    switch( env.a.mode )
    {
      case Iff::TEC_Replace:
        aSources = 1;
        break;
      case Iff::TEC_Modulate:
      case Iff::TEC_Add:
      case Iff::TEC_AddSigned:
      case Iff::TEC_Subtract:
      case Iff::TEC_Dot3Rgb:
      case Iff::TEC_Dot3Rgba:
        aSources = 2;
        break;
      case Iff::TEC_Interpolate:
        aSources = 3;
        break;
      default:
        break;
    }
  }
  for ( int i = 0; i < rgbSources; i++ )
  {
    bool skip = false;
    bool selecttexenvcolor = false;
    string source;
    Iff::TexenvCombineSrc src = i == 0 ? env.rgb.src0 : i == 1 ? env.rgb.src1 : env.rgb.src2;
    switch( src )
    {
      case Iff::TCS_PrimaryColor:
        source = "rglFrontColor";
        break;
      case Iff::TCS_Constant:
        // GAB Note Dec 2018: rglConstantColor is never bound, rglTexEnvColor must be used instead. The index will be append later on.
        // source = "rglConstantColor";
        source = "rglTexEnvColor";
        selecttexenvcolor = true;
        break;
      case Iff::TCS_Previous:
        source = "p";
        break;
      case Iff::TCS_Texture:
        source = "s";
        break;
      default:
        skip = true;
        break;
    }
    string suffix;
    Iff::TexenvCombineOp op = i == 0 ? env.rgb.op0 : i == 1 ? env.rgb.op1 : env.rgb.op2;
    switch( op )
    {
      case Iff::TCO_Alpha:
      case Iff::TCO_OneMinusAlpha:
        suffix = ".www";
        break;
      case Iff::TCO_Color:
      case Iff::TCO_OneMinusColor:
        suffix = ".xyz";
        break;
      default:
        skip = true;
        break;
    }
    if ( skip )
    {
      s << "        vec3 csrc" << i << " = vec3(0.0f, 0.0f, 0.0f);\n";
    }
    else
    {
      s << "        vec3 csrc" << i << " = ";
      if ( op == Iff::TCO_OneMinusColor || op == Iff::TCO_OneMinusAlpha )
      {
        // GAB Note Dec 2018: fixed Shader compilation issue due to usage of int instead of float
        // s << "1 - ";
        s << "1.0 - ";
      }
      // GAB Note Dec 2018: in case TexEnvColor is chosen, be sure to add the index
      if (selecttexenvcolor)
      {
          s << source << i << suffix << ";\n";
      }
      else {
          s << source << suffix << ";\n";
      }
    }
  }
  switch( env.rgb.mode )
  {
    case Iff::TEC_Replace:
      s << "        p.xyz = csrc0;\n";
      break;
    case Iff::TEC_Modulate:
      s << "        p.xyz = csrc0 * csrc1;\n";
      break;
    case Iff::TEC_Add:
      s << "        p.xyz = csrc0 + csrc1;\n";
      break;
    case Iff::TEC_AddSigned:
      s << "        p.xyz = csrc0 + csrc1 - 0.5;\n";
      break;
    case Iff::TEC_Subtract:
      s << "        p.xyz = csrc0 - csrc1;\n";
      break;
    case Iff::TEC_Dot3Rgb:
    case Iff::TEC_Dot3Rgba:
      // GAB Note Dec 2018: fixed Shader compilation issue ue to bad swizzle (float assigned to .xyz)
      // s << "        p.xyz = dot( ( 2.0 * csrc0 - 1.0 ), ( 2.0 * csrc1 - 1.0 ) );\n"; // <=
      s << "        float NdotL = dot( ( 2.0 * csrc0 - 1.0 ), ( 2.0 * csrc1 - 1.0 ) );\n";
      s << "        p.xyz = vec3(NdotL,NdotL,NdotL);\n";
      break;
    case Iff::TEC_Interpolate:
      s << "        p.xyz = mix( csrc0, csrc1, csrc2);\n";
      break;
    default:
      s << "//ERROR: Unsupported tex env combine rgb mode\n";
      break;
      break;
  }
  // GAB Note Dec 2018: always apply the scale, so that value will be clamped
  //if ( env.rgb.scale != 1.0 )
  {
    // GAB Note Dec 2018: fixed Shader compilation due to float->int conversion (eg. 1.0 => 1)
    // s << "        p.xyz = clamp(" << env.rgb.scale << " * p.xyz, 0.0, 1.0);\n";
    std::ostringstream stringStream;
    stringStream << std::fixed << env.rgb.scale;
    s << "        p.xyz = clamp(" << stringStream.str() << " * p.xyz, 0.0, 1.0);\n";
  }

  if ( skipAlpha )
  {
    s << "        p.w = p.x;\n";
  }
  else
  {
    for ( int i = 0; i < aSources; i++ )
    {
      bool skip = false;
      bool selecttexenvcolor = false;
      string source;
      Iff::TexenvCombineSrc src = i == 0 ? env.a.src0 : i == 1 ? env.a.src1 : env.a.src2;
      switch( src )
      {
        case Iff::TCS_PrimaryColor:
          source = "rglFrontColor";
          break;
        case Iff::TCS_Constant:
          // GAB Note Dec 2018: rglConstantColor is never bound, use rglTexEnvColor instead. The index will be append later on.
          // source = "rglConstantColor";
          source = "rglTexEnvColor";
          selecttexenvcolor = true;
          break;
        case Iff::TCS_Previous:
          source = "p";
          break;
        case Iff::TCS_Texture:
          source = "s";
          break;
        default:
          skip = true;
          break;
      }
      if ( skip )
      {
        s << "        float asrc" << i << " = 0.0f;\n";
      }
      else
      {
        Iff::TexenvCombineOp op = i == 0 ? env.a.op0 : i == 1 ? env.a.op1 : env.a.op2;
        s << "        float asrc" << i << " = ";
        if ( op == Iff::TCO_OneMinusAlpha )
        {
          // GAB Note Dec 2018: fixed Shader compilation issue due to usage of int instead of float
          // s << "1 - ";
          s << "1.0 - ";
        }
        // GAB Note Dec 2018: in case TexEnvColor is chosen, be sure to add the index
        if (selecttexenvcolor)
        {
            s << source << i << ".w;\n";
        }
        else
        {
            s << source << ".w;\n";
        }

      }
    }
    switch( env.a.mode )
    {
      case Iff::TEC_Replace:
        s << "        p.w = asrc0;\n";
        break;
      case Iff::TEC_Modulate:
        s << "        p.w = asrc0 * asrc1;\n";
        break;
      case Iff::TEC_Add:
        s << "        p.w = asrc0 + asrc1;\n";
        break;
      case Iff::TEC_AddSigned:
        s << "        p.w = asrc0 + asrc1 - 0.5;\n";
        break;
      case Iff::TEC_Subtract:
        s << "        p.w = asrc0 - asrc1;\n";
        break;
      case Iff::TEC_Interpolate:
        s << "        p.w = mix( asrc0, asrc1, asrc2 );\n";
        break;
      default:
        s << "//ERROR: Unsupported tex env combine alpha mode\n";
        break;
        break;
    }
    // GAB Note Dec 2018: always apply the scale, so that value will be clamped
    //if ( env.a.scale != 1.0 )
    {
      // GAB Note Dec 2018: fixed Shader compilation due to float->int conversion (eg. 1.0 => 1)
      // s << "        p.w = clamp(" << env.a.scale << " * p.w, 0.0, 1.0);\n";
      std::ostringstream stringStream;
      stringStream << std::fixed << env.a.scale;
      s << "        p.w = clamp(" << stringStream.str() << " * p.w, 0.0, 1.0);\n";
    }
  }
  s << "    }\n";
}

static string TargetSuffix( Iff::TextureTargetBitfield ttb )
{
  switch( ttb )
  {
    case Iff::TT_1D:
      return "1D";
    case Iff::TT_2D:
      return "2D";
    case Iff::TT_Rect:
      return "Rect";
    case Iff::TT_3D:
      return "3D";
    case Iff::TT_CubeMap:
      return "Cube";
    default:
      break;
  }
  return "";
}

static string TextureFetch( bool es, bool legacy, Iff::TextureTargetBitfield b )
{
  if ( es || legacy )
  {
    switch( b )
    {
      case Iff::TT_1D:
        return "texture1D";
      case Iff::TT_2D:
        return "texture2D";
      case Iff::TT_CubeMap:
        return "textureCube";
      default:
        break;
    }
  }
  return "texture";
}

static string TextureFetchSwizzle( bool es, bool legacy, Iff::TextureTargetBitfield b )
{
  if ( es || legacy )
  {
    switch( b )
    {
      case Iff::TT_1D:
        return ".x";
      case Iff::TT_2D:
        return ".xy";
      case Iff::TT_Rect:
        return ".xy";
      case Iff::TT_3D:
        return ".xyz";
      case Iff::TT_CubeMap:
        return ".xyz";
      default:
        break;
    }
    return "";
  }
  switch( b )
  {
    case Iff::TT_1D:
      return ".x";
    case Iff::TT_2D:
      return ".xy";
    case Iff::TT_Rect:
      return ".xy";
    case Iff::TT_3D:
      return ".xyz";
    case Iff::TT_CubeMap:
      return ".xyz";
    default:
      break;
  }
  return "";
}

static void GenerateFragmentShaderSource( Iff * rff, string_list &src )
{
  Internal("Regal::Iff::GenerateFragmentShaderSource", boost::print::optional(rff,Logging::pointers));

  const Store & st = rff->ffstate.processed;
  if ( rff->gles )
  {
#if REGAL_FORCE_DESKTOP_GLSL
    src << "#version 140\n";
#else
    src << "#version 100\n";
#endif
  }
  else if( rff->legacy )
  {
    src << "#version 120\n";
  }
  else
  {
    src << "#version 140\n";
  }
  src << "// program number " << rff->progcount << "\n";
  if ( rff->gles || rff->legacy )
  {
    src << "#define in varying\n";
    src << "#define rglFragColor gl_FragColor\n";
  }
  else
  {
    src << "out vec4 rglFragColor;\n";
  }
  if ( st.shadeModelFlat && ! rff->legacy && ! rff->gles )
  {
    src << "#define FLAT flat\n";
  }
  else
  {
    src << "#define FLAT  \n";
  }
  if ( rff->gles )
  {
    src << "precision highp float;\n";
  }
  src << "FLAT in vec4 rglFrontColor;\n";
  if ( st.lighting )
  {
    if ( st.lightModelTwoSide )
    {
      src << "FLAT in vec4 rglBackColor;\n";
    }
    if ( st.lightModelSeparateSpecular )
    {
      src << "in vec4 rglSCOL0;\n";
      if ( st.lightModelTwoSide )
      {
        src << "in vec4 rglSCOL1;\n";
      }
    }
  }
  else if( st.colorSum )
  {
    src << "in vec4 rglSCOL0;\n";
  }
  if ( st.fog.enable )
  {
    src << "uniform vec4 rglFog[2];\n";
    src << "in vec4 rglFOG;\n";
  }
  bool needsConstantColor = false;
  size_t n = array_size( rff->ffstate.processed.tex );
  // GAB Note Jan 2019: define rglTEXCOORD like gl_TexCoord, that is an array of vec4
  if (n > 0) {
    src << "in vec4 rglTEXCOORD[" << n << "];\n";
  }
  for ( size_t i = 0; i < n; i++ )
  {
    RegalAssertArrayIndex( rff->ffstate.processed.tex, i );
    Texture t = rff->ffstate.processed.tex[i];
    if ( t.enables == 0 )
    {
      continue;
    }
    src << "uniform sampler" << TargetSuffix( static_cast<Iff::TextureTargetBitfield>(t.enables) ) << " rglSampler" << i << ";\n";
    Iff::TextureEnv & env = t.unit.env;
    if ( env.mode == Iff::TEM_Combine )
    {
      needsConstantColor =
        env.rgb.src0 == Iff::TCS_Constant ||
        env.rgb.src1 == Iff::TCS_Constant ||
        env.rgb.src2 == Iff::TCS_Constant ;

      // GAB Note Dec 2018: constant color must be mapped to TexEnvColor for things to work later on
      if (needsConstantColor)
      {
          src << "uniform vec4 rglTexEnvColor" << i << ";\n";
      }
    }
    if ( env.mode == Iff::TEM_Blend )
    {
      src << "uniform vec4 rglTexEnvColor" << i << ";\n";
    }
  }
  // GAB Note Dec 2018: rglConstantColor is never bound, so I disable it
  //if ( needsConstantColor )
  //{
  //  src << "uniform vec4 rglConstantColor;\n";
  //}
  n = array_size( st.clipPlaneEnabled );
  for ( size_t i = 0; i < n; i++ )
  {
    RegalAssertArrayIndex( st.clipPlaneEnabled, i );
    if ( ( rff->gles || rff->legacy ) && st.clipPlaneEnabled[i] )
    {
      src << "in float rglClipDistance" << i << ";\n";
    }
  }
  if ( st.alphaTest.enable )
  {
    if (st.alphaTest.comp != Iff::CF_Never && st.alphaTest.comp != Iff::CF_Always )
    {
      src << "uniform vec2 rglAlphaRef;\n";
    }
  }
  src << "void main() {\n";

  if ( rff->gles || rff->legacy )
  {
    n = array_size( st.clipPlaneEnabled );
    for ( size_t i = 0; i < n; i++ )
    {
      RegalAssertArrayIndex( st.clipPlaneEnabled, i );
      if ( st.clipPlaneEnabled[i] )
      {
        src << "    if( rglClipDistance" << i << " < 0.0 ) discard;\n";
      }
    }
  }
  if ( st.lighting && st.lightModelTwoSide )
  {
    src << "    vec4 p = gl_FrontFacing ? rglFrontColor : rglBackColor;\n";
  }
  else
  {
    src << "    vec4 p = rglFrontColor;\n";
  }
  bool s_declared = false;
  n = array_size( rff->ffstate.processed.tex );
  for ( size_t i = 0; i < n; i++ )
  {
    RegalAssertArrayIndex( rff->ffstate.processed.tex, i );
    Texture t = rff->ffstate.processed.tex[ i ];
    Iff::TextureTargetBitfield b = rff->ffstate.GetTextureEnable( i );
    if ( b == 0 )
    {
      continue;
    }
    if ( !s_declared )
    {
      src << "    vec4 s;\n";
      s_declared = true;
    }
    switch( b )
    {
      case Iff::TT_1D:
      case Iff::TT_2D:
      {
        src << "    s = " << TextureFetch( rff->gles, rff->legacy, b )
            << "( rglSampler" << i << ", rglTEXCOORD[" << i << "]"
            << TextureFetchSwizzle( rff->gles, rff->legacy, b ) << " / rglTEXCOORD[" << i << "].w );\n";
        break;
      }
      case Iff::TT_CubeMap:
      {
        src << "    s = " << TextureFetch( rff->gles, rff->legacy, b )
            << "( rglSampler" << i << ", rglTEXCOORD[" << i << "]"
            << TextureFetchSwizzle( rff->gles, rff->legacy, b ) << " );\n";
        break;
      }
      default:
        src << "// ERROR: Unsupported texture target\n";
        src << "    s = vec4( 0.0f, 0.0f, 0.0f, 0.0f );\n";
        break;
    }
    switch( t.unit.env.mode )
    {
      case Iff::TEM_Replace:
      case Iff::TEM_Modulate:
      case Iff::TEM_Add:
      case Iff::TEM_Decal:
      case Iff::TEM_Blend:
        AddTexEnv( i, t.unit.env.mode, t.unit.fmt, src );
        break;
      case Iff::TEM_Combine:
        AddTexEnvCombine( t.unit.env, src );
        break;
      default:
        src << "//ERROR: Unsupported texture env mode\n";
        break;
    }
  }
  src << "    rglFragColor = p;\n";
  if ( st.lighting )
  {
    if ( st.lightModelSeparateSpecular )
    {
      if ( st.lightModelTwoSide )
      {
        src << "    rglFragColor += gl_FrontFacing ? rglSCOL0 : rglSCOL1;\n";
      }
      else
      {
        src << "    rglFragColor += rglSCOL0;\n";
      }
    }
  }
  else if( st.colorSum )
  {
    src << "    rglFragColor.xyz += rglSCOL0.xyz;\n";
  }
  if ( st.fog.enable )
  {
    src << "    float f = abs( -rglFOG.z / rglFOG.w );\n";
    switch( st.fog.mode )
    {
      case Iff::FG_Linear:
        src << "    float fogFactor = ( rglFog[0].z - f ) / ( rglFog[0].z - rglFog[0].y );\n";
        break;
      case Iff::FG_Exp:
        src << "    float fogFactor = exp( -( rglFog[0].x * f ) );\n";
        break;
      case Iff::FG_Exp2:
        src << "    float fogFactor = exp( -( rglFog[0].x * rglFog[0].x * f * f ) );\n";
        break;
      default:
        src << "//ERROR: Unsupported fog mode\n";
        src << "    float fogFactor = 0.0f;\n";
        break;
    }
    src << "    fogFactor = clamp( fogFactor, 0.0, 1.0 );\n";
    src << "    rglFragColor.xyz = mix( rglFog[1].xyz, rglFragColor.xyz, fogFactor );\n";
  }
  if ( st.alphaTest.enable )
  {
    switch( st.alphaTest.comp )
    {
      case Iff::CF_Never:
        src << "    discard;\n";
        break;
      case Iff::CF_Less:
        src << "    if( rglFragColor.w >= rglAlphaRef.x ) discard;\n";
        break;
      case Iff::CF_Greater:
        src << "    if( rglFragColor.w <= rglAlphaRef.x ) discard;\n";
        break;
      case Iff::CF_Lequal:
        src << "    if( rglFragColor.w > rglAlphaRef.x ) discard;\n";
        break;
      case Iff::CF_Gequal:
        src << "    if( rglFragColor.w < rglAlphaRef.x ) discard;\n";
        break;
      case Iff::CF_Equal:
        src << "    if( rglFragColor.w != rglAlphaRef.x ) discard;\n";
        break;
      case Iff::CF_NotEqual:
        src << "    if( rglFragColor.w == rglAlphaRef.x ) discard;\n";
        break;
      case Iff::CF_Always:
        break;
      default:
        src << "//ERROR: Unsupported alpha comp func\n";
        break;
    }
  }

  src << "}\n";
}

static void Copy( Float4 & dst, const GLfloat * src )
{
  dst.x = src[0];
  dst.y = src[1];
  dst.z = src[2];
  dst.w = src[3];
}

static void Copy( GLfloat * dst, Float4 & src )
{
  dst[0] = src.x;
  dst[1] = src.y;
  dst[2] = src.z;
  dst[3] = src.w;
}

static void Transform( Float4 & dst, const r3::Matrix4f & m, const GLfloat * src )
{
  r3::Vec4f v( src );
  m.MultMatrixVec( v );
  dst.x = v.x;
  dst.y = v.y;
  dst.z = v.z;
  dst.w = v.w;
}

// Not currently used
#if 0
void TransformDir( Float4 & dst, const r3::Matrix4f & m, const GLfloat * src )
{
  r3::Vec3f v( src );
  m.MultMatrixDir( v );
  v.Normalize();
  dst.x = v.x;
  dst.y = v.y;
  dst.z = v.z;
}
#endif

static r3::Matrix4f RescaleNormal( const r3::Matrix4f & m )
{
  r3::Matrix4f r = m;
  for ( int i = 0; i < 3; i++ )
  {
    r3::Vec3f v( r( i, 0 ), r( i, 1 ), r( i, 2 ) );
    v.Normalize();
    r( i, 0 ) = v.x;
    r( i, 1 ) = v.y;
    r( i, 2 ) = v.z;
  }
  return r;
}

bool State::SetEnable( Iff * ffn, bool enable, GLenum cap )
{
  Internal("Regal::Iff::State::SetEnable",enable," ",Token::GLenumToString(cap));

  Iff::Version & ver = ffn->ver;
  int shift = 0;
  switch( cap )
  {
    case GL_TEXTURE_1D:
      shift = TP_1D;
      break;
    case GL_TEXTURE_2D:
      shift = TP_2D;
      break;
    case GL_TEXTURE_RECTANGLE:
      shift = TP_Rect;
      break;
    case GL_TEXTURE_3D:
      shift = TP_3D;
      break;
    case GL_TEXTURE_CUBE_MAP:
      shift = TP_CubeMap;
      break;
    case GL_COLOR_SUM:
      raw.ver = ver.Update();
      raw.colorSum = enable;
      return true;
    case GL_FOG:
      raw.ver = ver.Update();
      raw.fog.enable = enable;
      return true;
    case GL_LIGHTING:
      raw.ver = ver.Update();
      raw.lighting = enable;
      return true;
    case GL_LIGHT0:
    case GL_LIGHT1:
    case GL_LIGHT2:
    case GL_LIGHT3:
    case GL_LIGHT4:
    case GL_LIGHT5:
    case GL_LIGHT6:
    case GL_LIGHT7:
      raw.ver = ver.Update();
      RegalAssertArrayIndex( raw.light, cap - GL_LIGHT0 );
      raw.light[ cap - GL_LIGHT0 ].enable = enable;
      return true;
    case GL_COLOR_MATERIAL:
      raw.ver = ver.Update();
      raw.colorMaterial = enable;
      return true;
    case GL_RESCALE_NORMAL:
      raw.ver = ver.Update();
      raw.rescaleNormal = enable;
      return true;
    case GL_NORMALIZE:
      raw.ver = ver.Update();
      raw.normalize = enable;
      return true;
    case GL_TEXTURE_GEN_S:
    case GL_TEXTURE_GEN_T:
    case GL_TEXTURE_GEN_R:
    case GL_TEXTURE_GEN_Q:
    {
      int activeTex = ffn->activeTextureIndex;
      if ( activeTex < REGAL_EMU_MAX_TEXTURE_UNITS )
      {
        raw.ver = ver.Update();
        int idx = cap - GL_TEXTURE_GEN_S;
        RegalAssertArrayIndex( raw.tex, activeTex );
        RegalAssertArrayIndex( raw.tex[ activeTex ].texgen, idx );
        raw.tex[ activeTex ].texgen[ idx ].enable = enable;
      }
      return true;
    }
    case GL_CLIP_PLANE0:
    case GL_CLIP_PLANE1:
    case GL_CLIP_PLANE2:
    case GL_CLIP_PLANE3:
    case GL_CLIP_PLANE4:
    case GL_CLIP_PLANE5:
    case GL_CLIP_PLANE0+6:
    case GL_CLIP_PLANE0+7:
      raw.ver = ver.Update();
      RegalAssertArrayIndex( raw.clipPlaneEnabled, cap - GL_CLIP_PLANE0 );
      raw.clipPlaneEnabled[ cap - GL_CLIP_PLANE0 ] = enable;
      return true;
    case GL_ALPHA_TEST:
      uniform.alphaTest.alphaTestEnable = enable ? GLfloat(1) : GLfloat(0);
      uniform.alphaTest.ver = uniform.ver = raw.ver = ver.Update();
      raw.alphaTest.enable = enable;
      return true;
    default:
      return false;
  }
  int activeTex = ffn->activeTextureIndex;
  if ( activeTex >= REGAL_EMU_MAX_TEXTURE_UNITS )
  {
    // track state for all texture units, but bail before fixed function
    // texture unit tracking
    return true;
  }
  RegalAssertArrayIndex( raw.tex, activeTex );
  Texture & t = raw.tex[ activeTex ];
  GLuint v = 1 << shift;
  if ( enable )
  {
    t.enables |= v;
  }
  else
  {
    t.enables &= ~v;
  }
  Internal("Regal::Iff::State::SetEnable","activeTex=",activeTex," enables=",t.enables);
  raw.ver = ver.Update();
  return true;
}

void State::SetTexInfo( Iff::Version & ver, GLuint activeTex, Iff::TextureUnit & unit )
{
  if (activeTex >= REGAL_EMU_MAX_TEXTURE_UNITS)
    return;

  raw.tex[ activeTex ].unit = unit;
  raw.ver = ver.Update();
}

void State::SetLight( Iff * ffn, GLenum light, GLenum pname, const GLfloat * params )
{
  Internal("Regal::Iff::State::SetLight",light,Token::GLenumToString(pname));

  Iff::Version & ver = ffn->ver;
  int idx = light - GL_LIGHT0;
  if ( idx < 0 || idx >= REGAL_FIXED_FUNCTION_MAX_LIGHTS )
  {
    return;
  }
  RegalAssertArrayIndex( uniform.light, idx );
  State::LightUniform & lu = uniform.light[ idx ];
  switch( pname )
  {
    case GL_AMBIENT:
      Copy( lu.ambient, params );
      break;
    case GL_DIFFUSE:
      Copy( lu.diffuse, params );
      break;
    case GL_SPECULAR:
      Copy( lu.specular, params );
      break;
    case GL_POSITION:
    {
      Transform( lu.position, ffn->modelview.Top(), params );
      if (lu.position.w == 0.0)
      {
        lu.position.w = 1.0f/sqrtf(lu.position.x * lu.position.x +
                                   lu.position.y * lu.position.y +
                                   lu.position.z * lu.position.z);
        lu.position.x *= lu.position.w;
        lu.position.y *= lu.position.w;
        lu.position.z *= lu.position.w;
        lu.position.w = 0.0;
        RegalAssertArrayIndex( raw.light, idx );
        raw.light[ idx ].local = false;
      }
      else
      {
        lu.position.x /= lu.position.w;
        lu.position.y /= lu.position.w;
        lu.position.z /= lu.position.w;
        lu.position.w = 1.0;
        RegalAssertArrayIndex( raw.light, idx );
        raw.light[ idx ].local = true;
      }
      break;
    }
    case GL_SPOT_DIRECTION:
    {
      Float4 spdir( params[0], params[1], params[2], 1.0f );
      r3::Matrix4f & m = ffn->modelview.Top();
      float x = m.element(0,0) * spdir.x + m.element(0,1) * spdir.y + m.element(0,2) * spdir.z;
      float y = m.element(1,0) * spdir.x + m.element(1,1) * spdir.y + m.element(1,2) * spdir.z;
      float z = m.element(2,0) * spdir.x + m.element(2,1) * spdir.y + m.element(2,2) * spdir.z;
      float d = -1.0f/sqrtf(x*x + y*y + z*z);
      lu.spotDirection.x = x * d;
      lu.spotDirection.y = y * d;
      lu.spotDirection.z = z * d;
    }
    break;

    case GL_SPOT_EXPONENT:
      lu.attenuation.w = params[0];
      break;
    case GL_SPOT_CUTOFF:
      lu.spotDirection.w = params[0];
      break;
    case GL_CONSTANT_ATTENUATION:
      lu.attenuation.x = params[0];
      break;
    case GL_LINEAR_ATTENUATION:
      lu.attenuation.y = params[0];
      break;
    case GL_QUADRATIC_ATTENUATION:
      lu.attenuation.z = params[0];
      break;
    default:
      return;
  }

  // some raw state is derived from uniform state
  bool updateRawVersion = false;
  switch( pname )
  {
    case GL_POSITION:
    case GL_SPOT_CUTOFF:
    case GL_CONSTANT_ATTENUATION:
    case GL_LINEAR_ATTENUATION:
    case GL_QUADRATIC_ATTENUATION:
      updateRawVersion = true;
      break;
    default:
      break;
  }
  lu.ver = uniform.ver = ver.Update();
  if ( updateRawVersion )
  {
    raw.ver = uniform.ver;
  }
}

void State::SetMaterial( Iff * ffn, GLenum face, GLenum pname, const GLfloat * params )
{
  Internal("Regal::Iff::State::SetMaterial",face,Token::GLenumToString(pname));

  Iff::Version & ver = ffn->ver;
  for ( int i = 0; i < 2; i++ )
  {
    if ( ( i == 0 && face == GL_BACK ) || ( i == 1 && face == GL_FRONT ) )
    {
      continue;
    }
    RegalAssertArrayIndex( uniform.mat, i );
    State::MaterialUniform & m = uniform.mat[ i ];
    switch( pname )
    {
      case GL_AMBIENT:
        Copy( m.ambient, params );
        break;
      case GL_DIFFUSE:
        Copy( m.diffuse, params );
        break;
      case GL_AMBIENT_AND_DIFFUSE:
        Copy( m.ambient, params );
        m.diffuse = m.ambient;
        break;
      case GL_SPECULAR:
        Copy( m.specular, params );
        break;
      case GL_EMISSION:
        Copy( m.emission, params );
        break;
      case GL_SHININESS:
        m.shininess.x = params[0];
        break;
      default:
        return;
    }
    m.ver = uniform.ver = ver.Update();
  }
}

void State::GetMaterial( Iff * ffn, GLenum face, GLenum pname, GLfloat * params )
{
  Internal("Regal::Iff::State::GetMaterial",face,Token::GLenumToString(pname));

  UNUSED_PARAMETER(ffn);

  for ( int i = 0; i < 2; i++ )
  {
    if ( ( i == 0 && face == GL_BACK ) || ( i == 1 && face == GL_FRONT ) )
    {
      continue;
    }
    RegalAssertArrayIndex( uniform.mat, i );
    State::MaterialUniform & m = uniform.mat[ i ];
    switch( pname )
    {
      case GL_AMBIENT:
        Copy( params, m.ambient );
        break;
      case GL_DIFFUSE:
        Copy( params, m.diffuse );
        break;
      case GL_SPECULAR:
        Copy( params, m.specular );
        break;
      case GL_EMISSION:
        Copy( params, m.emission );
        break;
      case GL_SHININESS:
        params[0] = m.shininess.x;
        break;
      default:
        return;
    }
  }
}

void State::SetTexgen( Iff * ffn, int coord, GLenum space, const GLfloat * params )
{
  Internal("Regal::Iff::State::SetTexgen",ffn,coord,toString(space),boost::print::array(params,4));

  r3::Matrix4f ident;
  RegalAssertArrayIndex( uniform.tex, ffn->activeTextureIndex );
  RegalAssertArrayIndex( uniform.tex[ ffn->activeTextureIndex ].texgen, coord );
  TexgenUniform & tgu = uniform.tex[ ffn->activeTextureIndex ].texgen[ coord ];
  GLuint64 *tguver = NULL;
  switch( space )
  {
    case GL_OBJECT_PLANE:
      Transform( tgu.obj, ident, params );
      tguver = & tgu.objVer;
      break;
    case GL_EYE_PLANE:
      Transform( tgu.eye, ffn->modelview.Top().Inverse().Transpose(), params );
      tguver = & tgu.eyeVer;
      break;
    default:
      return;
  }
  *tguver = uniform.ver = ffn->ver.Update();
}

void State::GetTexgen( Iff * ffn, int coord, GLenum space, GLfloat * params )
{
  Internal("Regal::Iff::State::GetTexgen", boost::print::optional(ffn,Logging::pointers)," ",coord," ",toString(space));

  RegalAssertArrayIndex( uniform.tex, ffn->activeTextureIndex );
  RegalAssertArrayIndex( uniform.tex[ ffn->activeTextureIndex ].texgen, coord );
  TexgenUniform & tgu = uniform.tex[ ffn->activeTextureIndex ].texgen[ coord ];
  switch( space )
  {
    case GL_OBJECT_PLANE:
      params[0] = tgu.obj.x;
      params[1] = tgu.obj.y;
      params[2] = tgu.obj.z;
      params[3] = tgu.obj.w;
      break;
    case GL_EYE_PLANE:
      params[0] = tgu.eye.x;
      params[1] = tgu.eye.y;
      params[2] = tgu.eye.z;
      params[3] = tgu.eye.w;
      break;
    default:
      break;
  }
}

void State::SetAlphaFunc( Iff * ffn, Iff::CompareFunc comp, GLfloat alphaRef )
{
  raw.alphaTest.comp = comp;
  uniform.alphaTest.alphaRef = std::min( 1.0f, std::max( 0.0f, alphaRef ) );
  uniform.alphaTest.ver = uniform.ver = raw.ver = ffn->ver.Update();
}

void State::SetClip( Iff * ffn, GLenum plane, const GLfloat * equation )
{
  int idx = plane - GL_CLIP_PLANE0;
  if ( idx >= REGAL_FIXED_FUNCTION_MAX_CLIP_PLANES )
  {
    return;
  }
  RegalAssertArrayIndex( uniform.clip, idx );
  Transform( uniform.clip[ idx ].plane, ffn->modelview.Top().Inverse().Transpose(), equation );
  uniform.clip[ idx ].ver = uniform.ver = ffn->ver.Update();
}

Iff::TextureTargetBitfield State::GetTextureEnable( size_t unit ) const
{
  return TextureTargetBitfield( ( processed.tex[unit].enables ) );
}

GLuint64 State::Ver() const
{
  return uniform.ver;
}

GLuint State::HighestPriorityTextureEnable( GLuint enables )
{
  for (int i = TP_CubeMap; i >= 0; i--)
  {
    if (enables & ( 1 << i ))
      return static_cast<GLubyte>(1 << i);
  }
  return 0;
}

void Program::Init( RegalContext * ctx, const Store & sstore, GLuint vshd, GLuint fshd )
{
  Internal("Regal::Iff::Program::Init","()");

  // update emu info with the limits that this layer supports

  RegalAssert(ctx);
  RegalAssert(ctx->emuInfo);
  ctx->emuInfo->gl_max_texture_coords = REGAL_EMU_MAX_TEXTURE_COORDS;
  ctx->emuInfo->gl_max_vertex_attribs = REGAL_EMU_MAX_VERTEX_ATTRIBS;
  ctx->emuInfo->gl_max_texture_units  = REGAL_EMU_MAX_TEXTURE_UNITS;

  ver = ::std::numeric_limits<GLuint64>::max();
  progcount = 0;
  RegalAssert(ctx);
  DispatchTableGL & tbl = ctx->dispatcher.emulation;
  store = sstore;
  pg = tbl.call(&tbl.glCreateProgram)();
  vs = vshd;
  tbl.call(&tbl.glAttachShader)(pg, vs );
  fs = fshd;
  tbl.call(&tbl.glAttachShader)(pg, fs );
  Attribs( ctx );
  tbl.call(&tbl.glLinkProgram)( pg );

#ifndef NDEBUG
  GLint status = 0;
  tbl.call(&tbl.glGetProgramiv)( pg, GL_LINK_STATUS, &status );
  if (!status)
  {
    std::string log;
    if (helper::getInfoLog(log,tbl.call(&tbl.glGetProgramInfoLog),tbl.call(&tbl.glGetProgramiv),pg))
      Warning( "Regal::Program::Init", log);
  }
#endif

  tbl.call(&tbl.glUseProgram)( pg );
  Samplers( ctx, tbl );
  Uniforms( ctx, tbl );
  tbl.call(&tbl.glUseProgram)( ctx->iff->program );
}

void Program::Shader( RegalContext * ctx, DispatchTableGL & tbl, GLenum type, GLuint & shader, const GLchar *src )
{
  Internal("Regal::Iff::Program::Shader","()");

  UNUSED_PARAMETER(ctx);

  const GLchar *srcs[] = { src };
  GLint len[] = { 0 };
  len[0] = (GLint)strlen( src );
  shader = tbl.call(&tbl.glCreateShader)(type);
  tbl.call(&tbl.glShaderSource)( shader, 1, srcs, len );
  tbl.call(&tbl.glCompileShader)( shader );

#ifndef NDEBUG
  GLint status = 0;
  tbl.call(&tbl.glGetShaderiv)( shader, GL_COMPILE_STATUS, &status );
  if (!status)
  {
    std::string log;
    if (helper::getInfoLog(log,tbl.call(&tbl.glGetShaderInfoLog),tbl.call(&tbl.glGetShaderiv),shader))
      Warning("Regal::Program::Shader", log);
  }
#endif

}

void Program::Attribs( RegalContext * ctx )
{
  Internal("Regal::Iff::Program::Attribs","()");

  DispatchTableGL & tbl = ctx->dispatcher.emulation;

  RegalAssertArrayIndex( ctx->iff->ffAttrMap, RFF2A_Vertex );
  tbl.call(&tbl.glBindAttribLocation)( pg, ctx->iff->ffAttrMap[ RFF2A_Vertex ], "rglVertex" );
  //tbl.call(&tbl.glBindAttribLocation)( pg, 1, "rglWeight" );
  if ( store.lighting )
  {
    RegalAssertArrayIndex( ctx->iff->ffAttrMap, RFF2A_Normal );
    tbl.call(&tbl.glBindAttribLocation)( pg, ctx->iff->ffAttrMap[ RFF2A_Normal ], "rglNormal" );
  }
  RegalAssertArrayIndex( ctx->iff->ffAttrMap, RFF2A_Color );
  if ( store.attrArrayFlags & ( 1 << ctx->iff->ffAttrMap[ RFF2A_Color ] ) && ( store.lighting == false || store.colorMaterial ) )
  {
    RegalAssertArrayIndex( ctx->iff->ffAttrMap, RFF2A_Color );
    tbl.call(&tbl.glBindAttribLocation)( pg, ctx->iff->ffAttrMap[ RFF2A_Color ], "rglColor" );
  }
  if ( store.colorSum  && store.lighting == false )
  {
    RegalAssertArrayIndex( ctx->iff->ffAttrMap, RFF2A_SecondaryColor );
    tbl.call(&tbl.glBindAttribLocation)( pg, ctx->iff->ffAttrMap[ RFF2A_SecondaryColor ], "rglSecondaryColor" );
  }
  if ( store.fog.enable && store.fog.useDepth == false )
  {
    RegalAssertArrayIndex( ctx->iff->ffAttrMap, RFF2A_FogCoord );
    tbl.call(&tbl.glBindAttribLocation)( pg, ctx->iff->ffAttrMap[ RFF2A_FogCoord ], "rglFogCoord" );
  }
  GLuint units = std::min( (GLuint)ctx->iff->ffAttrNumTex, (GLuint)REGAL_EMU_MAX_TEXTURE_UNITS );
  for ( GLuint i = 0; i < units; i++ )
  {
#ifndef REGAL_HACK_SET_001
    RegalAssertArrayIndex( store.tex, i );
    if ( store.tex[i].enables == 0 )
    {
      continue;
    }
#endif
    string_list ss;
    ss << "rglMultiTexCoord" << i;
    tbl.call(&tbl.glBindAttribLocation)( pg, ctx->iff->ffAttrTexBegin + i, ss.str().c_str() );
  }
}

// seth: for user program mode just do all the bind attribs, aliasing is OK in GL
void Program::UserShaderModeAttribs( RegalContext * ctx )
{
  Internal("Regal::Iff::Program::UserShaderModeAttribs","()");

  DispatchTableGL & tbl = ctx->dispatcher.emulation;

  RegalAssertArrayIndex( ctx->iff->ffAttrMap, RFF2A_Vertex );
  RegalAssertArrayIndex( ctx->iff->ffAttrMap, RFF2A_Normal );
  RegalAssertArrayIndex( ctx->iff->ffAttrMap, RFF2A_Color );
  RegalAssertArrayIndex( ctx->iff->ffAttrMap, RFF2A_SecondaryColor );
  RegalAssertArrayIndex( ctx->iff->ffAttrMap, RFF2A_FogCoord );

  tbl.call(&tbl.glBindAttribLocation)( pg, ctx->iff->ffAttrMap[ RFF2A_Vertex ], "rglVertex" );
  tbl.call(&tbl.glBindAttribLocation)( pg, ctx->iff->ffAttrMap[ RFF2A_Normal ], "rglNormal" );
  tbl.call(&tbl.glBindAttribLocation)( pg, ctx->iff->ffAttrMap[ RFF2A_Color ], "rglColor" );
  if ( ctx->iff->ffAttrMap[ RFF2A_SecondaryColor ] != RFF2A_Invalid )
  {
    tbl.call(&tbl.glBindAttribLocation)( pg, ctx->iff->ffAttrMap[ RFF2A_SecondaryColor ], "rglSecondaryColor" );
  }
  tbl.call(&tbl.glBindAttribLocation)( pg, ctx->iff->ffAttrMap[ RFF2A_FogCoord ], "rglFogCoord" );
  GLuint units = std::min( /*(GLuint)ctx->iff->ffAttrNumTex*/(GLuint)16, (GLuint)REGAL_EMU_MAX_TEXTURE_UNITS );
  for ( GLuint i = 0; i < units; i++ )
  {
    RegalAssertArrayIndex( store.tex, i );
    string s( "rglMultiTexCoord" );
    const char *nums = "0123456789abcdefghijklmnop";
    s += string( nums + i, 1 );
    tbl.call(&tbl.glBindAttribLocation)( pg, ctx->iff->ffAttrTexBegin + i, s.c_str() );
  }
}


void Program::Samplers( RegalContext * ctx, DispatchTableGL & tbl )
{
  Internal("Regal::Iff::Program::Samplers","()");

  UNUSED_PARAMETER(ctx);

  for ( GLint ii = 0; ii < REGAL_EMU_MAX_TEXTURE_UNITS; ii++ )
  {
    std::string samplerName = boost::print::print_string("rglSampler",ii);
    GLint slot = tbl.call(&tbl.glGetUniformLocation)( pg, samplerName.c_str() );
    if ( slot >= 0 )
      tbl.call(&tbl.glUniform1i)( slot, ii );
  }
}

void Iff::UserProgramInstance::LocateUniforms( RegalContext * ctx, DispatchTableGL & tbl ) {
  Internal("Regal::Iff::UserProgramInstance::LocateUniforms","()");

  UNUSED_PARAMETER(ctx);

  size_t n = array_size( regalFFUniformInfo );
  for ( size_t i = 1; i < n; i++ )
  {
    RegalAssertArrayIndex( regalFFUniformInfo, i );
    const RegalFFUniformInfo & ri = regalFFUniformInfo[i];
    GLint slot = tbl.call(&tbl.glGetUniformLocation)( inst.prog, ri.name );
    if (slot > -1)
      ffuniforms[ ri.val ] = UniformInfo(~GLuint64(0), slot);
  }
}

void Program::Uniforms( RegalContext * ctx, DispatchTableGL & tbl )
{
  Internal("Regal::Iff::Program::Uniforms","()");

  UNUSED_PARAMETER(ctx);

  size_t n = array_size( regalFFUniformInfo );
  for ( size_t i = 1; i < n; i++ )
  {
    RegalAssertArrayIndex( regalFFUniformInfo, i );
    const RegalFFUniformInfo & ri = regalFFUniformInfo[i];
    GLint slot = tbl.call(&tbl.glGetUniformLocation)( pg, ri.name );
    if (slot > -1)
      uniforms[ ri.val ] = UniformInfo(~GLuint64(0), slot);
  }
}

Iff::Iff()
: progcount(0)
, catIndex(0)
, ffAttrTexBegin(0)
, ffAttrTexEnd(0)
, ffAttrNumTex(0)
, max_vertex_attribs(0)
, immActive(false)
, immProvoking(0)
, immCurrent(0)
, immPrim(GL_POINTS)
, immVbo(0)
, immVboElement(0)
, immVao(0)
, immShadowVao(0)
, shadowMatrixMode(GL_MODELVIEW)
, shadowActiveTextureIndex(0)
, activeTextureIndex(0)
, programPipeline(0)
, program(0)
, currprog(NULL)
, currMatrixStack(&modelview)
, currVao(0)
, gles(false)
, legacy(false)
{
  memset(immArray,0,sizeof(immArray));

  size_t n = array_size( ffAttrMap );
  RegalAssert( array_size( ffAttrInvMap ) == n);
  for (size_t i = 0; i < n; i++)
  {
    ffAttrMap[ i ] = 0;
    ffAttrInvMap[ i ] = 0;
  }

  n = array_size( texture );
  RegalAssert( array_size( textureUnit ) == n);
  RegalAssert( array_size( textureEnvColor ) == n);
  RegalAssert( array_size( textureEnvColorVer ) == n);
  RegalAssert( array_size( textureBinding ) >= n);
  for (size_t i = 0; i < n; i++)
  {
    textureEnvColor[ i ] = Float4( 0.0f, 0.0f, 0.0f, 0.0f );
    textureEnvColorVer[ i ] = 0;
    textureBinding[ i ] = 0;
  }
  n = array_size( ffprogs );
  for (size_t i = 0; i < n; ++i)
    ffprogs[ i ] = Program();
}

void Iff::Cleanup( RegalContext &ctx )
{
  Internal("Regal::Iff::Cleanup","()");

  RestoreVao(&ctx);
  DispatchTableGL &tbl = ctx.dispatcher.emulation;

  tbl.call(&tbl.glDeleteBuffers)(1, &immVbo);
  tbl.call(&tbl.glDeleteBuffers)(1, &immVboElement);
  tbl.call(&tbl.glDeleteVertexArrays)(1, &immVao);

  size_t n = array_size( ffprogs );
  for (size_t i = 0; i < n; ++i)
  {
    RegalAssertArrayIndex( ffprogs, i );
    const Program &pgm = ffprogs[i];
    if (pgm.pg)
    {
      if (&pgm == currprog)
      {
        tbl.call(&tbl.glUseProgram)(0);
        currprog = NULL;
      }
      tbl.call(&tbl.glDeleteShader)(pgm.vs);
      tbl.call(&tbl.glDeleteShader)(pgm.fs);
      tbl.call(&tbl.glDeleteProgram)(pgm.pg);
    }
  }

  tbl.glBindBuffer(GL_ARRAY_BUFFER, 0);
  tbl.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  for (GLuint i=0; i<ctx.emuInfo->gl_max_vertex_attribs; ++i)
  {
    // Chromium/PepperAPI GLES generates an error (visible through glGetError)
    // and logs a message if a call is made to glVertexAttribPointer and no
    // GL_ARRAY_BUFFER is bound.
#if !REGAL_SYS_PPAPI
    tbl.glVertexAttribPointer(i, 4, GL_FLOAT, GL_FALSE, 0, NULL);
#endif
    tbl.glDisableVertexAttribArray(i);
  }
}

void Iff::InitVertexArray(RegalContext &ctx)
{
  Internal("Regal::Iff::InitVertexArray","()");
  max_vertex_attribs = ctx.emuInfo->gl_max_vertex_attribs;

  if (max_vertex_attribs >= 16)
  {
    RegalAssert( REGAL_EMU_MAX_VERTEX_ATTRIBS == 16);
    //RegalOutput( "Setting up for %d Vertex Attribs\n", max_vertex_attribs );
    for (size_t i = 0; i < 16; i++)
    {
      ffAttrMap[i] = RFF2AMap16[i];
      ffAttrInvMap[i] = RFF2AInvMap16[i];
    }
    ffAttrTexBegin = RFF2ATexBegin16;
    ffAttrTexEnd = RFF2ATexEnd16;
  }
  else
  {
    RegalAssert( max_vertex_attribs >= 8 );
    //RegalOutput( "Setting up for 8 Vertex Attribs" );
    for (size_t i = 0; i < 8; i++)
    {
      ffAttrMap[i] = RFF2AMap8[i];
      ffAttrInvMap[i] = RFF2AInvMap8[i];
    }
    for (size_t i = 8; i < REGAL_EMU_MAX_VERTEX_ATTRIBS; i++)
    {
      ffAttrMap[i] = GLuint(-1);
      ffAttrInvMap[i] = GLuint(-1);
    }
    ffAttrTexBegin = RFF2ATexBegin8;
    ffAttrTexEnd = RFF2ATexEnd8;
  }
  ffAttrNumTex = ffAttrTexEnd - ffAttrTexBegin;
  catIndex = 0;
}

GLuint Iff::ClientStateToIndex( GLenum state )
{
  switch( state )
  {
    case GL_VERTEX_ARRAY:
      return ffAttrMap[ RFF2A_Vertex ];
    case GL_NORMAL_ARRAY:
      return ffAttrMap[ RFF2A_Normal ];
    case GL_COLOR_ARRAY:
      return ffAttrMap[ RFF2A_Color ];
    case GL_SECONDARY_COLOR_ARRAY:
      return ffAttrMap[ RFF2A_SecondaryColor ];
    case GL_FOG_COORD_ARRAY:
      return ffAttrMap[ RFF2A_FogCoord ];
    case GL_EDGE_FLAG_ARRAY:
      return ffAttrMap[ RFF2A_EdgeFlag ];
    case GL_TEXTURE_COORD_ARRAY:
      if (catIndex < ffAttrNumTex)
        return ffAttrTexBegin + catIndex;
      break;
    default:
      break;
  }
  return ~0u;
}

void Iff::EnableClientState( RegalContext * ctx, GLenum state )
{
  const GLuint idx = ClientStateToIndex( state );
  if (idx == GLuint(~0))
    return;
  RestoreVao( ctx );
  RegalAssert( idx < max_vertex_attribs );
  if ( idx < max_vertex_attribs )
  {
    ctx->dispatcher.emulation.glEnableVertexAttribArray( idx );
    EnableArray( ctx, idx ); // keep ffn up to date
  }
}

void Iff::DisableClientState( RegalContext * ctx, GLenum state )
{
  const GLuint idx = ClientStateToIndex( state );
  if (idx == GLuint(~0))
    return;
  RestoreVao( ctx );
  RegalAssert( idx < max_vertex_attribs );
  if ( idx < max_vertex_attribs )
  {
    ctx->dispatcher.emulation.glDisableVertexAttribArray( idx );
    DisableArray( ctx, idx ); // keep ffn up to date
  }
}

void Iff::VertexPointer( RegalContext * ctx, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer )
{
  switch (size)
  {
    case 2:
    case 3:
    case 4:
      break;
    default:
      return;
  }

  switch (type)
  {
    case GL_SHORT:
    case GL_INT:
    case GL_FLOAT:
    case GL_DOUBLE:
      break;
    default:
      return;
  }

  if (stride < 0)
    return;

  RestoreVao( ctx );
  ctx->dispatcher.emulation.glVertexAttribPointer( ffAttrMap[ RFF2A_Vertex ], size, type, GL_FALSE, stride, pointer );
}

void Iff::NormalPointer( RegalContext * ctx, GLenum type, GLsizei stride, const GLvoid *pointer )
{
  RestoreVao( ctx );
  GLboolean n = type == GL_FLOAT ? GL_FALSE : GL_TRUE;
  ctx->dispatcher.emulation.glVertexAttribPointer( ffAttrMap[ RFF2A_Normal ], 3, type, n, stride, pointer );
}

void Iff::ColorPointer( RegalContext * ctx, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer )
{
  RestoreVao( ctx );
  GLboolean n = type == GL_FLOAT ? GL_FALSE : GL_TRUE;
  ctx->dispatcher.emulation.glVertexAttribPointer( ffAttrMap[ RFF2A_Color ], size, type, n, stride, pointer );
}

void Iff::SecondaryColorPointer( RegalContext * ctx, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer )
{
  RestoreVao( ctx );
  GLboolean n = type == GL_FLOAT ? GL_FALSE : GL_TRUE;
  ctx->dispatcher.emulation.glVertexAttribPointer( ffAttrMap[ RFF2A_SecondaryColor ], size, type, n, stride, pointer );
}

void Iff::FogCoordPointer( RegalContext * ctx, GLenum type, GLsizei stride, const GLvoid *pointer )
{
  RestoreVao( ctx );
  ctx->dispatcher.emulation.glVertexAttribPointer( ffAttrMap[ RFF2A_FogCoord ], 1, type, GL_FALSE, stride, pointer );
}

void Iff::EdgeFlagPointer( RegalContext * ctx, GLsizei stride, const GLvoid *pointer )
{
  RestoreVao( ctx );
  GLuint index = ffAttrMap[ RFF2A_EdgeFlag ];
  if (index == RFF2A_Invalid)
    return;
  ctx->dispatcher.emulation.glVertexAttribPointer( index, 1, GL_UNSIGNED_BYTE, GL_FALSE, stride, pointer );
}

void Iff::TexCoordPointer( RegalContext * ctx, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer )
{
  if (catIndex >= ffAttrNumTex)
  {
    // FIXME: set an error here!
    return;
  }
  RestoreVao( ctx );
  ctx->dispatcher.emulation.glVertexAttribPointer( ffAttrTexBegin + catIndex, size, type, GL_FALSE, stride, pointer );
}

void Iff::GetAttrib( RegalContext * ctx, GLuint index, GLenum pname, GLdouble * d )
{
  ctx->dispatcher.emulation.glGetVertexAttribdv( index, pname, d );
}

void Iff::GetAttrib( RegalContext * ctx, GLuint index, GLenum pname, GLfloat * f )
{
  ctx->dispatcher.emulation.glGetVertexAttribfv( index, pname, f );
}

void Iff::GetAttrib( RegalContext * ctx, GLuint index, GLenum pname, GLint * i )
{
  ctx->dispatcher.emulation.glGetVertexAttribiv( index, pname, i );
}

bool Iff::IsEnabled( RegalContext * ctx, GLenum pname, GLboolean &enabled )
{
  if (activeTextureIndex >= ctx->emuInfo->gl_max_texture_units)
    return false;

  State::Store & st = ffstate.raw;
  int shift = 0;
  switch (pname)
  {
    case GL_TEXTURE_1D:
      shift = TP_1D;
      break;
    case GL_TEXTURE_2D:
      shift = TP_2D;
      break;
    case GL_TEXTURE_RECTANGLE:
      shift = TP_Rect;
      break;
    case GL_TEXTURE_3D:
      shift = TP_3D;
      break;
    case GL_TEXTURE_CUBE_MAP:
      shift = TP_CubeMap;
      break;
    case GL_COLOR_SUM:
      enabled = (st.colorSum ? GL_TRUE : GL_FALSE);
      return true;
    case GL_FOG:
      enabled = (st.fog.enable ? GL_TRUE : GL_FALSE);
      return true;
    case GL_LIGHTING:
      enabled = (st.lighting ? GL_TRUE : GL_FALSE);
      return true;
    case GL_LIGHT0:
    case GL_LIGHT1:
    case GL_LIGHT2:
    case GL_LIGHT3:
    case GL_LIGHT4:
    case GL_LIGHT5:
    case GL_LIGHT6:
    case GL_LIGHT7:
      RegalAssertArrayIndex( st.light, pname - GL_LIGHT0 );
      enabled = (st.light[ pname - GL_LIGHT0 ].enable ? GL_TRUE : GL_FALSE);
      return true;
    case GL_COLOR_MATERIAL:
      enabled = (st.colorMaterial ? GL_TRUE : GL_FALSE);
      return true;
    case GL_RESCALE_NORMAL:
      enabled = (st.rescaleNormal ? GL_TRUE : GL_FALSE);
      return true;
    case GL_NORMALIZE:
      enabled = (st.normalize ? GL_TRUE : GL_FALSE);
      return true;
    case GL_TEXTURE_GEN_S:
    case GL_TEXTURE_GEN_T:
    case GL_TEXTURE_GEN_R:
    case GL_TEXTURE_GEN_Q:
      RegalAssertArrayIndex( st.tex, activeTextureIndex );
      RegalAssertArrayIndex( st.tex[ activeTextureIndex ].texgen, pname - GL_TEXTURE_GEN_S );
      enabled = (st.tex[ activeTextureIndex ].texgen[ pname - GL_TEXTURE_GEN_S ].enable ? GL_TRUE : GL_FALSE);
      return true;
    case GL_CLIP_PLANE0:
    case GL_CLIP_PLANE1:
    case GL_CLIP_PLANE2:
    case GL_CLIP_PLANE3:
    case GL_CLIP_PLANE4:
    case GL_CLIP_PLANE5:
    case GL_CLIP_PLANE0+6:
    case GL_CLIP_PLANE0+7:
      RegalAssertArrayIndex( st.clipPlaneEnabled, pname - GL_CLIP_PLANE0 );
      enabled = (st.clipPlaneEnabled[ pname - GL_CLIP_PLANE0 ] ? GL_TRUE : GL_FALSE);
      return true;
    case GL_ALPHA_TEST:
      enabled = (st.alphaTest.enable ? GL_TRUE : GL_FALSE);
      return true;
    default:
      return false;
  }

  RegalAssertArrayIndex( st.tex, activeTextureIndex );
  Texture & t = st.tex[ activeTextureIndex ];
  GLuint v = 1 << shift;
  enabled = ((t.enables & v) ? GL_TRUE : GL_FALSE);
  return true;
}

void Iff::InitImmediate(RegalContext &ctx)
{
  DispatchTableGL &tbl = ctx.dispatcher.emulation;
  tbl.glGenVertexArrays( 1, & immVao );
  tbl.glBindVertexArray( immVao );
  BindVertexArray( &ctx, immVao ); // to keep ffn current
  tbl.glGenBuffers( 1, & immVbo );
  tbl.glBindBuffer( GL_ARRAY_BUFFER, immVbo );
  tbl.glGenBuffers( 1, & immVboElement );
  tbl.glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, immVboElement );

//#if REGAL_SYS_EMSCRIPTEN
  // GAB NOTE Dec 2018: the following does not seem necessary anymore. Tested with latest version of Chrome and Firefox.

  // We need this to be an allocated buffer for WebGL, because a dangling VertexAttribPointer
  // doesn't work.  XXX -- this might be a Firefox bug, check?
  tbl.glBufferData( GL_ARRAY_BUFFER, sizeof( immArray ), NULL, GL_STATIC_DRAW );
  tbl.glBufferData( GL_ELEMENT_ARRAY_BUFFER, sizeof( immArrayElement ), NULL, GL_STATIC_DRAW );
//#endif

  for (GLuint i = 0; i < max_vertex_attribs; i++)
  {
    EnableArray( &ctx, i ); // to keep ffn current
    tbl.glEnableVertexAttribArray( i );
    tbl.glVertexAttribPointer( i, 4, GL_FLOAT, GL_FALSE, max_vertex_attribs * sizeof(Float4), (GLubyte *)NULL + i * sizeof(Float4) );
  }
  tbl.glBindVertexArray( 0 );
  BindVertexArray( &ctx, 0 ); // to keep ffn current

  // The initial texture coordinates are (s; t; r; q) = (0; 0; 0; 1)
  // for each texture coordinate set.

  memset(&immVab, 0, sizeof(immVab));
  for (catIndex = 0; catIndex < REGAL_EMU_MAX_TEXTURE_UNITS; catIndex++)
    Attr<4>( &ctx, AttrIndex( RFF2A_TexCoord ), 0, 0, 0, 1 );

  catIndex = 0;

  // The initial current normal has coordinates (0; 0; 1).

  Attr<3>( &ctx, AttrIndex( RFF2A_Normal ), 0, 0, 1 );

  // The initial RGBA color is (R;G;B;A) = (1; 1; 1; 1) and
  // the initial RGBA secondary color is (0; 0; 0; 1).

  Attr<4>( &ctx, AttrIndex( RFF2A_Color ), 1, 1, 1, 1 );
  Attr<4>( &ctx, AttrIndex( RFF2A_SecondaryColor ), 0, 0, 0, 1 );

  // The initial fog coordinate is zero.

  // ... so nothing to do for fog coordinate

  // The initial color index is 1.
  // The initial values for all generic vertex attributes are (0:0; 0:0; 0:0; 1:0).
}

void Iff::glDeleteVertexArrays( RegalContext * ctx, GLsizei n, const GLuint * arrays )
{
  RegalAssert( ctx != NULL );
  for (GLsizei i = 0; i < n; i++)
  {
    GLuint name = arrays[ i ];
    if (name != immVao)
      ctx->dispatcher.emulation.glDeleteVertexArrays( 1, &name );
  }
}

void Iff::glDeleteBuffers( RegalContext * ctx, GLsizei n, const GLuint * buffers )
{
  RegalAssert( ctx != NULL );
  for (GLsizei i = 0; i < n; i++)
  {
    GLuint name = buffers[ i ];
    if (name != immVbo && name != immVboElement)
      ctx->dispatcher.emulation.glDeleteBuffers( 1, &name );
  }
}

GLboolean Iff::IsVertexArray( RegalContext * ctx, GLuint name )
{
  RegalAssert( ctx != NULL );
  if (name == immVao )
    return GL_FALSE;
  return ctx->dispatcher.emulation.glIsVertexArray( name );
}

void Iff::glBindVertexArray( RegalContext *ctx, GLuint vao )
{
  //<> glBindVertexArray is not allowed between a glBegin/glEnd pair so if immActive
  //<> is true then I don't believe the shadow vao shouldn't be updated.
  if (immActive == false)
  {
    immShadowVao = vao;
    BindVertexArray( ctx, vao );
  }
}

void Iff::ShadowClientActiveTexture( GLenum texture )
{
  if ( (texture - GL_TEXTURE0) < REGAL_EMU_MAX_TEXTURE_COORDS)
    catIndex = texture - GL_TEXTURE0;
  else
    Warning( "Client active texture out of range: ", Token::GLtextureToString(texture), " > ", Token::GLtextureToString(GL_TEXTURE0 + REGAL_EMU_MAX_TEXTURE_COORDS - 1));
}

void Iff::Begin( RegalContext * ctx, GLenum mode )
{
  if (immActive == false)
  {
    immActive = true;
    ctx->dispatcher.emulation.glBindVertexArray( immVao );
    BindVertexArray( ctx, immVao );  // keep ffn current
  }
  PreDraw( ctx );
  immCurrent = 0;
  immCurrentElement = 0;
  immPrim = mode;
}

void Iff::End( RegalContext * ctx )
{
  Flush( ctx );
  RestoreVao( ctx );
}

void Iff::RestoreVao( RegalContext * ctx )
{
  if (immActive)
  {
    ctx->dispatcher.emulation.glBindVertexArray( immShadowVao );
    BindVertexArray( ctx, immShadowVao );
    immActive = false;
  }
}

void Iff::Flush( RegalContext * ctx )
{
  if (immCurrent>0)   // Do nothing for empty buffer
  {
    DispatchTableGL &tbl = ctx->dispatcher.emulation;
    tbl.glBufferData( GL_ARRAY_BUFFER, immCurrent * max_vertex_attribs * sizeof(Float4), immArray, GL_DYNAMIC_DRAW );

    GLenum derivedPrim = immPrim;
    if (( immPrim == GL_POLYGON ) && ( ctx->info->core == true || ctx->info->es2 ))
      derivedPrim = GL_TRIANGLE_FAN;
    tbl.glDrawArrays( derivedPrim, 0, immCurrent );
  }

  if (immCurrentElement > 0)
  {
    DispatchTableGL &tbl = ctx->dispatcher.emulation;

    if(immShadowVao != immVao)
    {
      tbl.glBindVertexArray( immShadowVao );
      BindVertexArray( ctx, immShadowVao );
    }

    GLuint immShadowVboElement = 0;
    ctx->dispatcher.emulation.glGetIntegerv( GL_ELEMENT_ARRAY_BUFFER_BINDING, reinterpret_cast<GLint*>(&immShadowVboElement) );
    if(immShadowVboElement != immVboElement) tbl.glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, immVboElement );
    tbl.glBufferData( GL_ELEMENT_ARRAY_BUFFER, immCurrentElement * sizeof(GLint), immArrayElement, GL_DYNAMIC_DRAW );

    GLenum derivedPrim = immPrim;
    if (( immPrim == GL_POLYGON ) && ( ctx->info->core == true || ctx->info->es2 ))
      derivedPrim = GL_TRIANGLE_FAN;
    tbl.glDrawElements( derivedPrim, immCurrentElement, GL_UNSIGNED_INT, 0 );

    if(immShadowVboElement != immVboElement) tbl.glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, immShadowVboElement );

    if(immShadowVao != immVao)
    {
      tbl.glBindVertexArray( immVao );
      BindVertexArray( ctx, immVao );
    }
  }
}

void Iff::Provoke( RegalContext * ctx )
{
  memcpy( immArray + immCurrent * max_vertex_attribs * sizeof(Float4), &immVab[0].x, max_vertex_attribs * sizeof(Float4) );
  immCurrent++;

  if ( immCurrent >= ((REGAL_IMMEDIATE_BUFFER_SIZE * REGAL_EMU_MAX_VERTEX_ATTRIBS) / max_vertex_attribs) )
  {
    Flush( ctx );
    int restartVerts = 0;
    switch( immPrim )
    {
      case GL_QUADS:
        restartVerts = REGAL_IMMEDIATE_BUFFER_SIZE % 4;
        break;
      case GL_TRIANGLES:
        restartVerts = REGAL_IMMEDIATE_BUFFER_SIZE % 3;
        break;
      case GL_LINES:
        restartVerts = REGAL_IMMEDIATE_BUFFER_SIZE % 2;
        break;
      case GL_QUAD_STRIP:
        restartVerts = 2;
        break;
      case GL_TRIANGLE_STRIP:
        restartVerts = 2;
        break;
      case GL_LINE_STRIP:
        restartVerts = 1;
        break;
      default:
        break;
    }

    // For triangle fan we need the first and last vertices
    // for restarting.  All others concern the most recent n.

    if (immPrim==GL_TRIANGLE_FAN)
    {
      memcpy( immArray + max_vertex_attribs * sizeof(Float4), immArray + (REGAL_IMMEDIATE_BUFFER_SIZE - 1) * max_vertex_attribs * sizeof(Float4), max_vertex_attribs * sizeof(Float4));
      immCurrent = 2;
    }
    else
    {
      int offset = REGAL_IMMEDIATE_BUFFER_SIZE - restartVerts;
      memcpy( immArray, immArray + offset * max_vertex_attribs * sizeof(Float4), restartVerts * max_vertex_attribs * sizeof(Float4));
      immCurrent = restartVerts;
    }
  }
}

void Iff::ProvokeElement( RegalContext * ctx, GLint i )
{
  immArrayElement[immCurrentElement++] = i;

  if ( immCurrentElement >= REGAL_IMMEDIATE_BUFFER_SIZE )
  {
    Flush( ctx );
    int restartVerts = 0;
    switch( immPrim )
    {
      case GL_QUADS:
        restartVerts = REGAL_IMMEDIATE_BUFFER_SIZE % 4;
        break;
      case GL_TRIANGLES:
        restartVerts = REGAL_IMMEDIATE_BUFFER_SIZE % 3;
        break;
      case GL_LINES:
        restartVerts = REGAL_IMMEDIATE_BUFFER_SIZE % 2;
        break;
      case GL_QUAD_STRIP:
        restartVerts = 2;
        break;
      case GL_TRIANGLE_STRIP:
        restartVerts = 2;
        break;
      case GL_LINE_STRIP:
        restartVerts = 1;
        break;
      default:
        break;
    }

    // For triangle fan we need the first and last vertices
    // for restarting.  All others concern the most recent n.

    if (immPrim==GL_TRIANGLE_FAN)
    {
      immArrayElement[1] = immArrayElement[REGAL_IMMEDIATE_BUFFER_SIZE - 1];
      immCurrentElement = 2;
    }
    else
    {
      int offset = REGAL_IMMEDIATE_BUFFER_SIZE - restartVerts;
      memcpy( immArrayElement, immArrayElement + offset * sizeof(GLint), restartVerts * sizeof(GLint));
      immCurrentElement = restartVerts;
    }
  }
}

GLuint Iff::AttrIndex( RegalFixedFunctionAttrib attr, int cat ) const
{
  if (attr < RFF2A_TexCoord)
  {
    RegalAssertArrayIndex( ffAttrMap, attr );
    return ffAttrMap[ attr ];
  }
  if (cat < 0)
    cat = catIndex;
  if (attr == RFF2A_TexCoord && GLuint(cat) < ffAttrNumTex)
    return ffAttrTexBegin + cat;
  return ~0u;
}

void Iff::InitFixedFunction(RegalContext &ctx)
{
  Internal("Regal::Iff::InitFixedFunction","()");

  RegalAssert(ctx.info);

  gles   = ctx.info->es2;
  legacy = ctx.info->compat && ctx.info->gl_version_major<=2;

  vaoAttrMap[0] = 0;

  fmtmap[ 1 ] = GL_LUMINANCE;
  fmtmap[ 2 ] = GL_LUMINANCE_ALPHA;
  fmtmap[ 3 ] = GL_RGB;
  fmtmap[ 4 ] = GL_RGBA;
  fmtmap[ GL_ALPHA ] = GL_ALPHA;
  fmtmap[ GL_ALPHA4 ] = GL_ALPHA;
  fmtmap[ GL_ALPHA8 ] = GL_ALPHA;
  fmtmap[ GL_ALPHA12 ] = GL_ALPHA;
  fmtmap[ GL_ALPHA16 ] = GL_ALPHA;
  fmtmap[ GL_LUMINANCE ] = GL_LUMINANCE;
  fmtmap[ GL_LUMINANCE4 ] = GL_LUMINANCE;
  fmtmap[ GL_LUMINANCE8 ] = GL_LUMINANCE;
  fmtmap[ GL_LUMINANCE12 ] = GL_LUMINANCE;
  fmtmap[ GL_LUMINANCE16 ] = GL_LUMINANCE;
  fmtmap[ GL_LUMINANCE_ALPHA ] = GL_LUMINANCE_ALPHA;
  fmtmap[ GL_LUMINANCE4_ALPHA4 ] = GL_LUMINANCE_ALPHA;
  fmtmap[ GL_LUMINANCE6_ALPHA2 ] = GL_LUMINANCE_ALPHA;
  fmtmap[ GL_LUMINANCE8_ALPHA8 ] = GL_LUMINANCE_ALPHA;
  fmtmap[ GL_LUMINANCE12_ALPHA4 ] = GL_LUMINANCE_ALPHA;
  fmtmap[ GL_LUMINANCE12_ALPHA12 ] = GL_LUMINANCE_ALPHA;
  fmtmap[ GL_LUMINANCE16_ALPHA16 ] = GL_LUMINANCE_ALPHA;
  fmtmap[ GL_INTENSITY ] = GL_INTENSITY;
  fmtmap[ GL_INTENSITY4 ] = GL_INTENSITY;
  fmtmap[ GL_INTENSITY8 ] = GL_INTENSITY;
  fmtmap[ GL_INTENSITY12 ] = GL_INTENSITY;
  fmtmap[ GL_INTENSITY12 ] = GL_INTENSITY;
  fmtmap[ GL_INTENSITY16 ] = GL_INTENSITY;
  fmtmap[ GL_RGB ] = GL_RGB;
  fmtmap[ GL_RGB4 ] = GL_RGB;
  fmtmap[ GL_RGB5 ] = GL_RGB;
  fmtmap[ GL_RGB8 ] = GL_RGB;
  fmtmap[ GL_RGB10 ] = GL_RGB;
  fmtmap[ GL_RGB12 ] = GL_RGB;
  fmtmap[ GL_RGB16 ] = GL_RGB;
  fmtmap[ GL_RGBA ] = GL_RGBA;
  fmtmap[ GL_RGBA4 ] = GL_RGBA;
  fmtmap[ GL_RGB5_A1 ] = GL_RGBA;
  fmtmap[ GL_RGBA8 ] = GL_RGBA;
  fmtmap[ GL_RGB10_A2 ] = GL_RGBA;
  fmtmap[ GL_RGBA12 ] = GL_RGBA;
  fmtmap[ GL_RGBA16 ] = GL_RGBA;

  fmtmap[ GL_RGBA16F ] = GL_RGBA16F;
  fmtmap[ GL_SRGB ] = GL_SRGB;
  fmtmap[ GL_SRGB8 ] = GL_SRGB;
  fmtmap[ GL_SRGB8_ALPHA8 ] = GL_SRGB8_ALPHA8;
  fmtmap[ GL_SLUMINANCE ] = GL_SLUMINANCE;
  fmtmap[ GL_SLUMINANCE8 ] = GL_SLUMINANCE;

  fmtmap[ GL_RGB16F_ARB ]       = GL_RGB;
  fmtmap[ GL_RGBA32F_ARB ]      = GL_RGB;

  // GAB Note Dec 2018: This was missing
  fmtmap[ GL_DEPTH_COMPONENT ]   = GL_DEPTH_COMPONENT;

  fmtmap[ GL_DEPTH_COMPONENT16 ] = GL_DEPTH_COMPONENT16;

  fmtmap[ GL_INTENSITY16F_ARB ] = GL_INTENSITY;

  // GL_ARB_ES2_compatibility

  fmtmap[ GL_RGB565      ] = GL_RGB;

  // ARB_texture_rg and EXT_texture_rg

  fmtmap[ GL_RED         ] = GL_RED;
  fmtmap[ GL_RED_INTEGER ] = GL_RED;
  fmtmap[ GL_R8          ] = GL_RED;
  fmtmap[ GL_R16         ] = GL_RED;
  fmtmap[ GL_R16F        ] = GL_RED;
  fmtmap[ GL_R32F        ] = GL_RED;
  fmtmap[ GL_R8I         ] = GL_RED;
  fmtmap[ GL_R8UI        ] = GL_RED;
  fmtmap[ GL_R16I        ] = GL_RED;
  fmtmap[ GL_R16UI       ] = GL_RED;
  fmtmap[ GL_R32I        ] = GL_RED;
  fmtmap[ GL_R32UI       ] = GL_RED;

  fmtmap[ GL_RG          ] = GL_RG;
  fmtmap[ GL_RG_INTEGER  ] = GL_RG;
  fmtmap[ GL_RG8         ] = GL_RG;
  fmtmap[ GL_RG16        ] = GL_RG;
  fmtmap[ GL_RG16F       ] = GL_RG;
  fmtmap[ GL_RG32F       ] = GL_RG;
  fmtmap[ GL_RG8I        ] = GL_RG;
  fmtmap[ GL_RG8UI       ] = GL_RG;
  fmtmap[ GL_RG16I       ] = GL_RG;
  fmtmap[ GL_RG16UI      ] = GL_RG;
  fmtmap[ GL_RG32I       ] = GL_RG;
  fmtmap[ GL_RG32UI      ] = GL_RG;
}

void Iff::PreDraw( RegalContext * ctx )
{
  if (programPipeline)
    return;    // FIXME: Eventually will need to handle empty or partially populated PPO

  ver.Reset();
  if  (program)
    UseShaderProgram( ctx );
  else
    UseFixedFunctionProgram( ctx );
}

void Iff::SetCurrentMatrixStack( GLenum mode )
{
  switch( mode )
  {
    case GL_MODELVIEW:
      currMatrixStack = &modelview;
      break;
    case GL_PROJECTION:
      currMatrixStack = &projection;
      break;
    case GL_TEXTURE:
      if (activeTextureIndex < GLuint( REGAL_EMU_MAX_TEXTURE_UNITS ))
        currMatrixStack = &texture[ activeTextureIndex ];
      break;
    case GL_TEXTURE0:
    case GL_TEXTURE1:
    case GL_TEXTURE2:
    case GL_TEXTURE3:
    {
      GLuint idx = mode - GL_TEXTURE0;
      if (idx > GLuint( REGAL_EMU_MAX_TEXTURE_UNITS - 1 ))
        break;
      currMatrixStack = &texture[ idx ];
    }
    break;
    default:
      RegalAssert( true && "WTF?" );
      break;
  }
}

bool Iff::ShadowMatrixMode( GLenum mode )
{
  shadowMatrixMode = mode;
  return true;
}

void Iff::ShadowActiveTexture( GLenum texture )
{
  if (validTextureEnum(texture))
    shadowActiveTextureIndex = texture - GL_TEXTURE0;
}

bool Iff::ShadowEnable( GLenum cap )
{
  return EnableIndexed( cap, shadowActiveTextureIndex );
}

bool Iff::ShadowDisable( GLenum cap )
{
  return DisableIndexed( cap, shadowActiveTextureIndex );
}

bool Iff::EnableIndexed( GLenum cap, GLuint index )
{
  activeTextureIndex = index;
  bool ret = ffstate.SetEnable( this, true, cap );
  return ret;
}

bool Iff::DisableIndexed( GLenum cap, GLuint index )
{
  activeTextureIndex = index;
  bool ret = ffstate.SetEnable( this, false, cap );
  return ret;
}

bool Iff::ShadowUseProgram( GLuint prog )
{
  program = prog;

  // ensure program bind when program changes
  if( currinst ) {
    currinst->prevInstance = NULL;
  }

  if( prog && inst.count( prog ) ) {
    currinst = & inst[prog];
  } else {
    currinst = NULL;
  }
  return prog == 0 || currinst != NULL;  // pass the call along only if it's non-zero and not instanced
}

bool Iff::ShadowBindProgramPipeline( GLuint progPipeline )
{
  programPipeline = progPipeline;
  return false;  // always pass this through since we're not emulating it
}

void Iff::ShadowMultiTexBinding( GLenum texunit, GLenum target, GLuint obj )
{
  Internal("Regal::Iff::ShadowMultiTexBinding",toString(texunit)," ",toString(target)," ",obj);

  // texture unit state is only set when the active texture index is <4 (for fixed function)
  if ( texunit - GL_TEXTURE0 >= REGAL_EMU_MAX_TEXTURE_COORDS
    || texunit - GL_TEXTURE0 >= REGAL_EMU_MAX_TEXTURE_UNITS )
    return;

  activeTextureIndex = texunit - GL_TEXTURE0;

  GLenum fmt = textureObjToFmt[ obj ];
  RegalAssertArrayIndex( textureUnit, activeTextureIndex );
  RegalAssertArrayIndex( textureBinding, activeTextureIndex );
  TextureUnit & tu = textureUnit[ activeTextureIndex ];
  textureBinding[ activeTextureIndex ] = obj;
  tu.fmt = fmt;
  tu.ttb = TargetToBitfield( target );
  ffstate.raw.ver = ver.Update();
}

void Iff::ShadowTexBinding( GLenum target, GLuint obj )
{
  ShadowMultiTexBinding( GL_TEXTURE0 + shadowActiveTextureIndex, target, obj );
}

void Iff::ShadowTextureInfo( GLuint obj, GLenum target, GLint internalFormat )
{
  Internal("Regal::Iff::ShadowTextureInfo",obj," ",GLenumToString(target)," ",GLenumToString(internalFormat));

  UNUSED_PARAMETER(target);

  if ( fmtmap.count( internalFormat ) == 0 )
  {
    Warning( "Unknown internal format: ", GLenumToString(internalFormat) );
  }
  GLenum fmt = fmtmap[ internalFormat ];
  textureObjToFmt[ obj ] = fmt;
  ffstate.raw.ver = ver.Update();
}

void Iff::ShadowMultiTexInfo( GLenum texunit, GLenum target, GLint internalFormat )
{
  if ( (texunit - GL_TEXTURE0) < REGAL_EMU_MAX_TEXTURE_COORDS)
  {
    activeTextureIndex = texunit - GL_TEXTURE0;
    ShadowTexInfo( target, internalFormat );
  }
  else
    Warning( "Texture unit out of range: ", Token::GLtextureToString(texunit), " > ", Token::GLtextureToString(GL_TEXTURE0 + REGAL_EMU_MAX_TEXTURE_COORDS - 1));
}


void Iff::ShadowTexInfo( GLenum target, GLint internalFormat )
{
  // texture unit state is only set when the active texture index <4 (for fixed function)
  if ( shadowActiveTextureIndex > ( REGAL_EMU_MAX_TEXTURE_UNITS - 1 ) )
  {
    return;
  }
  RegalAssertArrayIndex( textureBinding, shadowActiveTextureIndex );
  RegalAssertArrayIndex( textureUnit, shadowActiveTextureIndex );
  ShadowTextureInfo( textureBinding[ shadowActiveTextureIndex ], target, internalFormat );
  textureUnit[ shadowActiveTextureIndex ].fmt = fmtmap[ internalFormat ];
}

void Iff::TexEnv( GLenum texunit, GLenum target, GLenum pname, const GLfloat *v )
{
  activeTextureIndex = texunit - GL_TEXTURE0;
  switch( target )
  {
    case GL_TEXTURE_ENV:
      switch( pname )
      {
        case GL_TEXTURE_ENV_COLOR:
        {
          RegalAssertArrayIndex( textureUnit, activeTextureIndex );
          RegalAssertArrayIndex( textureEnvColor, activeTextureIndex );
          RegalAssertArrayIndex( textureEnvColorVer, activeTextureIndex );
          TextureUnit *tup = & textureUnit[ activeTextureIndex ];
          Copy( textureEnvColor[ activeTextureIndex ], v );
          ffstate.SetTexInfo( ver, activeTextureIndex, *tup );
          textureEnvColorVer[ activeTextureIndex ] = ffstate.uniform.ver = ver.Update();
          return;
        }
      }
  }
  GLint iv[4];
  iv[0] = static_cast<GLint>(v[0]);
  iv[1] = static_cast<GLint>(v[1]);
  iv[2] = static_cast<GLint>(v[2]);
  iv[3] = static_cast<GLint>(v[3]);
  TexEnv( texunit, target, pname, iv );
}

void Iff::TexEnv( GLenum texunit, GLenum target, GLenum pname, const GLint *v )
{
  Internal("Regal::Iff::TexEnv",GLenumToString(texunit)," ",GLenumToString(target)," ",GLenumToString(pname));

  activeTextureIndex = texunit - GL_TEXTURE0;
  switch( target )
  {
    case GL_TEXTURE_ENV:
      switch( pname )
      {
        case GL_TEXTURE_ENV_MODE:
        {
          if ( activeTextureIndex >= REGAL_EMU_MAX_TEXTURE_UNITS )
          {
            return;
          }
          RegalAssertArrayIndex( textureUnit, activeTextureIndex );
          TextureUnit *tup = & textureUnit[ activeTextureIndex ];
          TexenvMode & m = tup->env.mode;
          switch( v[0] )
          {
            case GL_REPLACE:
              m = TEM_Replace;
              break;
            case GL_MODULATE:
              m = TEM_Modulate;
              break;
            case GL_ADD:
              m = TEM_Add;
              break;
            case GL_DECAL:
              m = TEM_Decal;
              break;
            case GL_BLEND:
              m = TEM_Blend;
              break;
            case GL_COMBINE:
              m = TEM_Combine;
              break;
            default:
              return; // error?
          }
          // assert( target == tip->tgt );
          ffstate.SetTexInfo( ver, activeTextureIndex, *tup );
          break;
        }
        case GL_COMBINE_RGB:
        {
          RegalAssertArrayIndex( textureUnit, activeTextureIndex );
          TextureUnit *tup = & textureUnit[ activeTextureIndex ];
          TexenvCombineState & s = tup->env.rgb;
          switch( v[0] )
          {
            case GL_REPLACE:
              s.mode = TEC_Replace;
              break;
            case GL_MODULATE:
              s.mode = TEC_Modulate;
              break;
            case GL_ADD:
              s.mode = TEC_Add;
              break;
            case GL_ADD_SIGNED:
              s.mode = TEC_AddSigned;
              break;
            case GL_SUBTRACT:
              s.mode = TEC_Subtract;
              break;
            case GL_DOT3_RGB:
              s.mode = TEC_Dot3Rgb;
              break;
            case GL_DOT3_RGBA:
              s.mode = TEC_Dot3Rgba;
              break;
            case GL_INTERPOLATE:
              s.mode = TEC_Interpolate;
              break;
            default:
              return; // error?
          }
          // assert( target == tip->tgt );
          ffstate.SetTexInfo( ver, activeTextureIndex, *tup );
          break;
        }
        case GL_COMBINE_ALPHA:
        {
          RegalAssertArrayIndex( textureUnit, activeTextureIndex );
          TextureUnit *tup = & textureUnit[ activeTextureIndex ];
          TexenvCombineState & s = tup->env.a;
          switch( v[0] )
          {
            case GL_REPLACE:
              s.mode = TEC_Replace;
              break;
            case GL_MODULATE:
              s.mode = TEC_Modulate;
              break;
            case GL_ADD:
              s.mode = TEC_Add;
              break;
            case GL_ADD_SIGNED:
              s.mode = TEC_AddSigned;
              break;
            case GL_SUBTRACT:
              s.mode = TEC_Subtract;
              break;
            case GL_INTERPOLATE:
              s.mode = TEC_Interpolate;
              break;
            default:
              return; // error?
          }
          // assert( target == tip->tgt );
          ffstate.SetTexInfo( ver, activeTextureIndex, *tup );
          break;
        }
        case GL_SRC0_RGB:
        case GL_SRC1_RGB:
        case GL_SRC2_RGB:
        case GL_SRC0_ALPHA:
        case GL_SRC1_ALPHA:
        case GL_SRC2_ALPHA:
        {
          int idx = pname - GL_SRC0_RGB;
          bool isRgb = true;
          if ( idx > 3 )
          {
            isRgb = false;
            idx = pname - GL_SRC0_ALPHA;
          }
          RegalAssertArrayIndex( textureUnit, activeTextureIndex );
          TextureUnit *tup = & textureUnit[ activeTextureIndex ];
          TexenvCombineState & c = isRgb ? tup->env.rgb : tup->env.a;
          if ( idx  == 0 )
          {
            switch( v[0] )
            {
              case GL_PREVIOUS:
                c.src0 = TCS_Previous;
                break;
              case GL_CONSTANT:
                c.src0 = TCS_Constant;
                break;
              case GL_TEXTURE:
                c.src0 = TCS_Texture;
                break;
              case GL_PRIMARY_COLOR:
                c.src0 = TCS_PrimaryColor;
                break;
              default:
                return; // error?
            }
          }
          else if( idx  == 1 )
          {
            switch( v[0] )
            {
              case GL_PREVIOUS:
                c.src1 = TCS_Previous;
                break;
              case GL_CONSTANT:
                c.src1 = TCS_Constant;
                break;
              case GL_TEXTURE:
                c.src1 = TCS_Texture;
                break;
              case GL_PRIMARY_COLOR:
                c.src1 = TCS_PrimaryColor;
                break;
              default:
                return; // error?
            }
          }
          else if( idx  == 2 )
          {
            switch( v[0] )
            {
              case GL_PREVIOUS:
                c.src2 = TCS_Previous;
                break;
              case GL_CONSTANT:
                c.src2 = TCS_Constant;
                break;
              case GL_TEXTURE:
                c.src2 = TCS_Texture;
                break;
              case GL_PRIMARY_COLOR:
                c.src2 = TCS_PrimaryColor;
                break;
              default:
                return; // error?
            }
          }
          ffstate.SetTexInfo( ver, activeTextureIndex, *tup );
          break;
        }
        case GL_OPERAND0_RGB:
        case GL_OPERAND1_RGB:
        case GL_OPERAND2_RGB:
        case GL_OPERAND0_ALPHA:
        case GL_OPERAND1_ALPHA:
        case GL_OPERAND2_ALPHA:
        {
          int idx = pname - GL_OPERAND0_RGB;
          bool isRgb = true;
          if ( idx > 3 )
          {
            isRgb = false;
            idx = pname - GL_OPERAND0_ALPHA;
          }
          RegalAssertArrayIndex( textureUnit, activeTextureIndex );
          TextureUnit *tup = & textureUnit[ activeTextureIndex ];
          TexenvCombineState & c = isRgb ? tup->env.rgb : tup->env.a;
          if ( idx == 0 )
          {
            switch( v[0] )
            {
              case GL_SRC_COLOR:
                c.op0 = TCO_Color;
                break;
              case GL_SRC_ALPHA:
                c.op0 = TCO_Alpha;
                break;
              case GL_ONE_MINUS_SRC_COLOR:
                c.op0 = TCO_OneMinusColor;
                break;
              case GL_ONE_MINUS_SRC_ALPHA:
                c.op0 = TCO_OneMinusAlpha;
                break;
              default:
                return; // error?
            }
          }
          else if( idx == 1 )
          {
            switch( v[0] )
            {
              case GL_SRC_COLOR:
                c.op1 = TCO_Color;
                break;
              case GL_SRC_ALPHA:
                c.op1 = TCO_Alpha;
                break;
              case GL_ONE_MINUS_SRC_COLOR:
                c.op1 = TCO_OneMinusColor;
                break;
              case GL_ONE_MINUS_SRC_ALPHA:
                c.op1 = TCO_OneMinusAlpha;
                break;
              default:
                return; // error?
            }
          }
          else if( idx == 2 )
          {
            switch( v[0] )
            {
              case GL_SRC_COLOR:
                c.op2 = TCO_Color;
                break;
              case GL_SRC_ALPHA:
                c.op2 = TCO_Alpha;
                break;
              case GL_ONE_MINUS_SRC_COLOR:
                c.op2 = TCO_OneMinusColor;
                break;
              case GL_ONE_MINUS_SRC_ALPHA:
                c.op2 = TCO_OneMinusAlpha;
                break;
              default:
                return; // error?
            }
          }
          ffstate.SetTexInfo( ver, activeTextureIndex, *tup );
          break;
        }
        case GL_RGB_SCALE:
        case GL_ALPHA_SCALE:
        {
          RegalAssertArrayIndex( textureUnit, activeTextureIndex );
          TextureUnit *tup = & textureUnit[ activeTextureIndex ];
          TexenvCombineState & c = (pname == GL_RGB_SCALE ? tup->env.rgb : tup->env.a);
          GLfloat pscale = static_cast<GLfloat>(v[0]);
          if (pscale != 1.0 && pscale != 2.0 && pscale != 4.0)
          {
            Warning("Invalid value set for TexEnv Scale %f - must be 1.0, 2.0, or 4.0\n", pscale);
            return; // error
          }
          c.scale = pscale;
          ffstate.SetTexInfo( ver, activeTextureIndex, *tup );
          break;
        }
        default:
          break;
      }
      break;
    default:
      break;
  }
}

void Iff::TexEnv( GLenum texunit, GLenum target, GLenum pname, GLfloat v )
{
  TexEnv( texunit, target, pname, &v );
}

void Iff::TexEnv( GLenum texunit, GLenum target, GLenum pname, GLint v )
{
  TexEnv( texunit, target, pname, &v );
}

void Iff::ShadeModel( GLenum mode )
{
  State::Store & r = ffstate.raw;
  switch( mode )
  {
    case GL_FLAT:
      if (!r.shadeModelFlat)
      {
        r.shadeModelFlat = true;
        r.ver = ver.Update();
      }
      break;
    case GL_SMOOTH:
      if (r.shadeModelFlat)
      {
        r.shadeModelFlat = false;
        r.ver = ver.Update();
      }
      break;
  }
}

void Iff::ColorMaterial( GLenum face, GLenum mode )
{
  ColorMaterialMode m;
  switch( mode )
  {
    case GL_EMISSION:
      m = CM_Emission;
      break;
    case GL_AMBIENT:
      m = CM_Ambient;
      break;
    case GL_DIFFUSE:
      m = CM_Diffuse;
      break;
    case GL_SPECULAR:
      m = CM_Specular;
      break;
    case GL_AMBIENT_AND_DIFFUSE:
      m = CM_AmbientAndDiffuse;
      break;
    default:
      return;
  }
  switch( face )
  {
    case GL_FRONT:
      ffstate.raw.colorMaterialTarget0 = m;
      ffstate.raw.colorMaterialTarget1 = CM_None;
      break;
    case GL_BACK:
      ffstate.raw.colorMaterialTarget0 = CM_None;
      ffstate.raw.colorMaterialTarget1 = m;
      break;
    case GL_FRONT_AND_BACK:
      ffstate.raw.colorMaterialTarget0 = m;
      ffstate.raw.colorMaterialTarget1 = m;
      break;
    default:
      return;
  }
  ffstate.raw.ver = ver.Update();
}

void Iff::AlphaFunc( GLenum comp, const GLfloat ref )
{
  CompareFunc cf = CF_Always;
  switch( comp )
  {
    case GL_NEVER:
      cf = CF_Never;
      break;
    case GL_LESS:
      cf = CF_Less;
      break;
    case GL_EQUAL:
      cf = CF_Equal;
      break;
    case GL_LEQUAL:
      cf = CF_Lequal;
      break;
    case GL_NOTEQUAL:
      cf = CF_NotEqual;
      break;
    case GL_GREATER:
      cf = CF_Greater;
      break;
    case GL_GEQUAL:
      cf = CF_Gequal;
      break;
    case GL_ALWAYS:
      cf = CF_Always;
      break;
    default:
      break; // should be an error...
  }
  ffstate.SetAlphaFunc( this, cf, ref );
}

void Iff::ClipPlane( GLenum plane, const GLdouble * equation )
{
  Float4 eqn( equation[0], equation[1], equation[2], equation[3] );
  ffstate.SetClip( this, plane, & eqn.x );
}

void Iff::MatrixPush( GLenum mode )
{
  SetCurrentMatrixStack( mode );
  currMatrixStack->Push();
}

void Iff::MatrixPop( GLenum mode )
{
  SetCurrentMatrixStack( mode );
  currMatrixStack->Pop();
  UpdateMatrixVer();
}

void Iff::UpdateMatrixVer()
{
  currMatrixStack->Ver() = ffstate.uniform.ver = ver.Update();
  if (currMatrixStack != &modelview && currMatrixStack != &projection)
    ffstate.raw.ver = ffstate.uniform.ver;
}

void Iff::MatrixLoadIdentity( GLenum mode )
{
  SetCurrentMatrixStack( mode );
  currMatrixStack->Top().MakeIdentity();
  UpdateMatrixVer();
}

void Iff::MatrixLoad( GLenum mode, const r3::Matrix4f & m )
{
  SetCurrentMatrixStack( mode );
  currMatrixStack->Top() = m;
  UpdateMatrixVer();
}

void Iff::MatrixLoadTranspose( GLenum mode, const r3::Matrix4f & m )
{
  SetCurrentMatrixStack( mode );
  currMatrixStack->Top() = m.Transpose();
  UpdateMatrixVer();
}

void Iff::MatrixMult( GLenum mode, const r3::Matrix4f & m )
{
  SetCurrentMatrixStack( mode );
  currMatrixStack->Top().MultRight( m );
  UpdateMatrixVer();
}

void Iff::MatrixMultTranspose( GLenum mode, const r3::Matrix4f & m )
{
  SetCurrentMatrixStack( mode );
  currMatrixStack->Top().MultRight( m.Transpose() );
  UpdateMatrixVer();
}

void Iff::PushMatrix()
{
  MatrixPush( shadowMatrixMode );
}

void Iff::PopMatrix()
{
  MatrixPop( shadowMatrixMode );
}

void Iff::LoadIdentity()
{
  MatrixLoadIdentity( shadowMatrixMode );
}

void Iff::LoadMatrix( const r3::Matrix4f & m )
{
  MatrixLoad( shadowMatrixMode, m );
}

void Iff::LoadTransposeMatrix( const r3::Matrix4f & m )
{
  MatrixLoadTranspose( shadowMatrixMode, m );
}

void Iff::MultMatrix( const r3::Matrix4f & m )
{
  MatrixMult( shadowMatrixMode, m );
}

void Iff::MultTransposeMatrix( const r3::Matrix4f & m )
{
  MatrixMultTranspose( shadowMatrixMode, m );
}

void Iff::Viewport( GLint x, GLint y, GLsizei w, GLsizei h )
{
  viewport.x = x;
  viewport.y = y;
  viewport.w = w;
  viewport.h = h;
}

void Iff::DepthRange( GLfloat znear, GLfloat zfar )
{
  viewport.zn = znear;
  viewport.zf = zfar;
}

void Iff::RasterPos( RegalContext * ctx, GLdouble x, GLdouble y, GLdouble z )
{
  r3::Vec3f pos( x, y, z );
  r3::Vec3f s( 0.5f * GLfloat(viewport.w), 0.5f * GLfloat(viewport.h), 0.5f * GLfloat( viewport.zf - viewport.zn ) );
  r3::Vec3f b( GLfloat(viewport.x), GLfloat(viewport.y), 0.5f + GLfloat(viewport.zn) );
  r3::Matrix4f sb;
  sb.SetScale( s );
  sb.SetTranslate( s + b );
  r3::Matrix4f m = sb * projection.Top() * modelview.Top();
  m.MultMatrixVec( pos );
  WindowPos( ctx, pos.x, pos.y, pos.z );
}

void Iff::WindowPos( RegalContext * ctx, GLdouble x, GLdouble y, GLdouble z )
{
  if (ctx->isCore() || ctx->isCompat())
  {
    // todo - cache rasterpos and implement glDrawPixels and glBitmap
    return;
  }
  ctx->dispatcher.emulation.glWindowPos3d( x, y, z );
}

void Iff::BindVertexArray( RegalContext * ctx, GLuint vao )
{
  UNUSED_PARAMETER(ctx);
  vaoAttrMap[ currVao ] = ffstate.raw.attrArrayFlags;
  currVao = vao;
  ffstate.raw.attrArrayFlags = vaoAttrMap[ currVao ];
  ffstate.uniform.vabVer = ver.Update();
}

void Iff::EnableArray( RegalContext * ctx, GLuint index )
{
  RestoreVao( ctx );
  ffstate.raw.attrArrayFlags |= 1 << index;
  ffstate.raw.ver = ffstate.uniform.vabVer = ver.Update();
}

void Iff::DisableArray( RegalContext * ctx, GLuint index )
{
  RestoreVao( ctx );
  ffstate.raw.attrArrayFlags &= ~( 1 << index );
  ffstate.raw.ver = ffstate.uniform.vabVer = ver.Update();
}

GLuint Iff::CreateShader( RegalContext *ctx, GLenum shaderType )
{
  GLuint sh = ctx->dispatcher.emulation.glCreateShader( shaderType );
  shaderTypeMap[ sh ] = shaderType;
  return sh;
}

void Iff::Init( RegalContext &ctx )
{
  shadowMatrixMode = GL_MODELVIEW;
  shadowActiveTextureIndex = 0;
  activeTextureIndex = 0;
  programPipeline = 0;
  program = 0;
  currprog = NULL;
  currinst = NULL;
  currMatrixStack = &modelview;
  currVao = 0;
  gles = false;
  legacy = false;

  RegalContext *sharingWith = ctx.shareGroup->front();
  if (sharingWith)
    textureObjToFmt = sharingWith->iff->textureObjToFmt;

  InitVertexArray(ctx);
  InitFixedFunction(ctx);
  InitImmediate(ctx);
}

void Iff::State::Process( Iff * ffn )
{
  Internal("Iff::State::Process","()");

  const Store & r = raw;
  Store & p = processed;
  StoreUniform & u = uniform;

  if ( r.ver > 0 && r.ver == p.ver )
  {
    return;
  }

  p = r;

  r3::Matrix4f identity;
  if ( p.fog.enable == false )
  {
    p.fog.useDepth = true;
    p.fog.mode = FG_Exp;
  }

  // alpha testing is done at the precision of the color buffer so adjust
  // alpharef to an 8-bit range

  u.alphaTest.alphaRef = floor( u.alphaTest.alphaRef * 255.0f + 0.5f ) / 255.0f;

  size_t n = array_size( ffn->textureUnit );
  for ( size_t i = 0; i < n; i++ )
  {
    RegalAssertArrayIndex( ffn->textureUnit, i );
    TextureUnit & ti = ffn->textureUnit[ i ];
    RegalAssertArrayIndex( raw.tex, i );
    raw.tex[i].unit = ti;
  }

  n = array_size( p.tex );
  for ( size_t i = 0; i < n; i++ )
  {
    RegalAssertArrayIndex( p.tex, i );
    RegalAssertArrayIndex( r.tex, i );
    Texture & pt = p.tex[i];
    const Texture & rt= r.tex[i];
    // for processed state, only the highest priority texture enable set (others are dont-care)
    //<> dsn:
    //<> but do we need to track and check against ttb?  If no texture was
    //<> explicitly bound to a texture target then the default texture (which
    //<> has the name 0) for that target will be used, right?
    //<> pt.enables = HighestPriorityTextureEnable( rt.enables & rt.unit.ttb );
    pt.enables = HighestPriorityTextureEnable( rt.enables );
    pt.unit = rt.unit;
    RegalAssertArrayIndex( ffn->texture, i );
    pt.useMatrix = pt.enables != 0 && ffn->texture[i].Top() != identity;
    if ( pt.unit.env.mode != TEM_Combine )
    {
      pt.unit.env.rgb = TexenvCombineState(true);
      pt.unit.env.a = TexenvCombineState(false);
    }
  }
  n = array_size( p.light );
  if ( p.lighting == false )
  {
    p.rescaleNormal = false;
    p.normalize = false;
    for ( size_t i = 0; i <n; i++ )
    {
      RegalAssertArrayIndex( p.light, i );
      Light & l = p.light[i];
      l.enable = l.spotlight = l.attenuate = false;
    }
  }
  else
  {
    for ( size_t i = 0; i < n; i++ )
    {
      RegalAssertArrayIndex( p.light, i );
      RegalAssertArrayIndex( u.light, i );
      Light & l = p.light[i];
      LightUniform & lu = u.light[i];
      if ( l.enable == true )
      {
        l.spotlight = lu.spotDirection.w != 180.0f;
        l.attenuate = lu.attenuation.x != 1.0f || lu.attenuation.y != 0.0f || lu.attenuation.z != 0.0f;
      }
    }
  }
  p.hash = Lookup3::hashlittle(reinterpret_cast<const char *>(&p.hash) + sizeof( p.hash ),
                               reinterpret_cast<const char *>((&p)+1) - reinterpret_cast<const char *>(&p.hash) - sizeof( p.hash ), 0);
}


void Iff::UpdateUniforms( RegalContext * ctx )
{
  Internal("Regal::Iff::UpdateUniforms", boost::print::optional(ctx,Logging::pointers));

  DispatchTableGL & tbl = ctx->dispatcher.emulation;
  UniformMap * umap = NULL;
  if( currinst ) {
    if( currinst->prevInstance == NULL ) {
      return;
    }

    UserProgramInstance & upi = *currinst->prevInstance;

    if ( upi.ffver == ffstate.Ver() ) {
      return;
    }
    upi.ffver = ffstate.Ver();
    umap = & upi.ffuniforms;
  } else {
    Program & pgm = *currprog;
    if ( pgm.ver == ffstate.Ver() ) {
      return;
    }
    pgm.ver = ffstate.Ver();
    umap = & pgm.uniforms;
  }

  const State::Store & p = ffstate.processed;
  const State::StoreUniform & u = ffstate.uniform;
  for ( UniformMap::iterator i = umap->begin(); i != umap->end(); ++i )
  {
    UniformInfo & ui = (*i).second;
    bool inverse = false;
    bool transpose = false;

    RegalFFUniformEnum ue = (*i).first;

    if( FFU_ModelViewMatrixInverse <= ue && ue <= FFU_TextureMatrix15Inverse ) {
      inverse = true;
      ue = RegalFFUniformEnum( FFU_ModelViewMatrix + ( ue - FFU_ModelViewMatrixInverse ) );
    } else if( FFU_ModelViewMatrixTranspose <= ue && ue <= FFU_TextureMatrix15Transpose ) {
      transpose = true;
      ue = RegalFFUniformEnum( FFU_ModelViewMatrix + ( ue - FFU_ModelViewMatrixTranspose ) );
    } else if( FFU_ModelViewMatrixInverseTranspose <= ue && ue <= FFU_TextureMatrix15InverseTranspose ) {
      inverse = true;
      transpose = true;
      ue = RegalFFUniformEnum( FFU_ModelViewMatrix + ( ue - FFU_ModelViewMatrixInverseTranspose ) );
    }

    switch( ue )
    {
      case FFU_NormalMatrix:
      {
        if( ui.ver != modelview.Ver() ) {
          ui.ver = modelview.Ver();
          r3::Matrix4f m = modelview.Top().Inverse().Transpose();
          if ( p.rescaleNormal )
          {
            m = RescaleNormal( m );
          }
          r3::Matrix3f m3 = r3::ToMatrix3( m ).Transpose(); // FIXME: r3::Matrix3f should be column major like Matrix4...
          tbl.glUniformMatrix3fv( ui.slot, 1, GL_FALSE, m3.Ptr() );
        }
        break;
      }
      case FFU_ModelViewMatrix:
      {
        if ( ui.ver != modelview.Ver() )
        {
          ui.ver = modelview.Ver();
          r3::Matrix4f m = modelview.Top();
          if( inverse ) {
            m = m.Inverse();
          }
          if( transpose ) {
            m = m.Transpose();
          }
          tbl.glUniformMatrix4fv( ui.slot, 1, GL_FALSE, m.Ptr() );
        }
        break;
      }
      case FFU_ProjectionMatrix:
      {
        if ( ui.ver != projection.Ver() )
        {
          ui.ver = projection.Ver();
          r3::Matrix4f m = projection.Top();
          if( inverse ) {
            m = m.Inverse();
          }
          if( transpose ) {
            m = m.Transpose();
          }
          tbl.glUniformMatrix4fv( ui.slot, 1, GL_FALSE, m.Ptr() );
        }
        break;
      }
      case FFU_ModelViewProjectionMatrix:
      {
        GLuint64 mvpVer = std::max( modelview.Ver(), projection.Ver() );
        if ( ui.ver != mvpVer )
        {
          ui.ver = mvpVer;
          r3::Matrix4f m = projection.Top() * modelview.Top();
          if( inverse ) {
            m = m.Inverse();
          }
          if( transpose ) {
            m = m.Transpose();
          }
          tbl.glUniformMatrix4fv( ui.slot, 1, GL_FALSE, m.Ptr() );
        }
        break;
      }
      case FFU_TextureMatrix0:
      case FFU_TextureMatrix1:
      case FFU_TextureMatrix2:
      case FFU_TextureMatrix3:
      case FFU_TextureMatrix4:
      case FFU_TextureMatrix5:
      case FFU_TextureMatrix6:
      case FFU_TextureMatrix7:
      case FFU_TextureMatrix8:
      case FFU_TextureMatrix9:
      case FFU_TextureMatrix10:
      case FFU_TextureMatrix11:
      case FFU_TextureMatrix12:
      case FFU_TextureMatrix13:
      case FFU_TextureMatrix14:
      case FFU_TextureMatrix15:
      {
        int idx = ( ue - FFU_TextureMatrix0 );
        RegalAssertArrayIndex( texture, idx );
        if ( ui.ver != texture[ idx ].Ver() )
        {
          ui.ver = texture[ idx ].Ver();
          r3::Matrix4f m = texture[ idx ].Top();
          if( inverse ) {
            m = m.Inverse();
          }
          if( transpose ) {
            m = m.Transpose();
          }
          tbl.glUniformMatrix4fv( ui.slot, 1, GL_FALSE, m.Ptr() );
        }
        break;
      }
      case FFU_TextureEnvColor0:
      case FFU_TextureEnvColor1:
      case FFU_TextureEnvColor2:
      case FFU_TextureEnvColor3:
      case FFU_TextureEnvColor4:
      case FFU_TextureEnvColor5:
      case FFU_TextureEnvColor6:
      case FFU_TextureEnvColor7:
      case FFU_TextureEnvColor8:
      case FFU_TextureEnvColor9:
      case FFU_TextureEnvColor10:
      case FFU_TextureEnvColor11:
      case FFU_TextureEnvColor12:
      case FFU_TextureEnvColor13:
      case FFU_TextureEnvColor14:
      case FFU_TextureEnvColor15:
      {
        int idx = ( ((*i).first) - FFU_TextureEnvColor0 );
        RegalAssertArrayIndex( textureEnvColorVer, idx );
        if ( ui.ver != textureEnvColorVer[ idx ] )
        {
          ui.ver = textureEnvColorVer[ idx ];
          tbl.glUniform4fv( ui.slot, 1, &textureEnvColor[ idx ].x);
        }
        break;
      }
      case FFU_Light0:
      case FFU_Light1:
      case FFU_Light2:
      case FFU_Light3:
      case FFU_Light4:
      case FFU_Light5:
      case FFU_Light6:
      case FFU_Light7:
      {
        int idx = ( ((*i).first) - FFU_Light0 );
        RegalAssertArrayIndex( u.light, idx );
        if ( ui.ver != u.light[ idx ].ver )
        {
          ui.ver = u.light[ idx ].ver;
          tbl.glUniform4fv( ui.slot, LE_Elements, &u.light[ idx ].ambient.x);
        }
        break;
      }
      case FFU_MaterialFront:
      case FFU_MaterialBack:
      {
        int idx = ( ((*i).first) - FFU_MaterialFront );
        RegalAssertArrayIndex( u.mat, idx );
        if ( ui.ver != u.mat[ idx ].ver )
        {
          ui.ver = u.mat[ idx ].ver;
          tbl.glUniform4fv( ui.slot, ME_Elements, &u.mat[ idx ].ambient.x);
        }
        break;
      }
      case FFU_LightModelAmbient:
      {
        tbl.glUniform4fv( ui.slot, 1, &u.lightModelAmbient.x);
        break;
      }
      case FFU_Texgen0ObjS:
      case FFU_Texgen0ObjT:
      case FFU_Texgen0ObjR:
      case FFU_Texgen0ObjQ:
      case FFU_Texgen0EyeS:
      case FFU_Texgen0EyeT:
      case FFU_Texgen0EyeR:
      case FFU_Texgen0EyeQ:
      case FFU_Texgen1ObjS:
      case FFU_Texgen1ObjT:
      case FFU_Texgen1ObjR:
      case FFU_Texgen1ObjQ:
      case FFU_Texgen1EyeS:
      case FFU_Texgen1EyeT:
      case FFU_Texgen1EyeR:
      case FFU_Texgen1EyeQ:
      case FFU_Texgen2ObjS:
      case FFU_Texgen2ObjT:
      case FFU_Texgen2ObjR:
      case FFU_Texgen2ObjQ:
      case FFU_Texgen2EyeS:
      case FFU_Texgen2EyeT:
      case FFU_Texgen2EyeR:
      case FFU_Texgen2EyeQ:
      case FFU_Texgen3ObjS:
      case FFU_Texgen3ObjT:
      case FFU_Texgen3ObjR:
      case FFU_Texgen3ObjQ:
      case FFU_Texgen3EyeS:
      case FFU_Texgen3EyeT:
      case FFU_Texgen3EyeR:
      case FFU_Texgen3EyeQ:
      {
        int idx = ( (*i).first ) - FFU_Texgen0ObjS;
        int comp = idx % 4;
        int unit = idx >> 3;
        RegalAssertArrayIndex( u.tex, unit );
        RegalAssertArrayIndex( u.tex[unit].texgen, comp );
        const State::TexgenUniform & tg = u.tex[unit].texgen[comp];
        if ( idx & 4 )
        {
          if ( ui.ver != tg.eyeVer )
          {
            ui.ver = tg.eyeVer;
            tbl.glUniform4fv( ui.slot, 1, & tg.eye.x );
          }
        }
        else
        {
          if ( ui.ver != tg.objVer )
          {
            ui.ver = tg.objVer;
            tbl.glUniform4fv( ui.slot, 1, & tg.obj.x );
          }
        }
        break;
      }
      case FFU_ClipPlane0:
      case FFU_ClipPlane1:
      case FFU_ClipPlane2:
      case FFU_ClipPlane3:
      case FFU_ClipPlane4:
      case FFU_ClipPlane5:
      case FFU_ClipPlane6:
      case FFU_ClipPlane7:
      {
        int idx = ( (*i).first ) - FFU_ClipPlane0;
        RegalAssertArrayIndex( u.clip, idx );
        if ( ui.ver != u.clip[idx].ver )
        {
          ui.ver = u.clip[idx].ver;
          tbl.glUniform4fv( ui.slot, 1, & u.clip[idx].plane.x );
        }
        break;
      }
      case FFU_Fog:
      {
        if ( ui.ver != u.fog.ver )
        {
          ui.ver = u.fog.ver;
          tbl.glUniform4fv( ui.slot, 2, &u.fog.params[0].x);
        }
        break;
      }
      case FFU_AlphaRef:
      {
        if ( ui.ver != u.alphaTest.ver )
        {
          ui.ver = u.alphaTest.ver;
          tbl.glUniform2f( ui.slot, u.alphaTest.alphaRef, u.alphaTest.alphaTestEnable );
        }
        break;
      }
      case FFU_Attrib:
      {
        if ( ui.ver != u.vabVer )
        {
          ui.ver = u.vabVer;
          tbl.glUniform4fv( ui.slot, REGAL_EMU_MAX_VERTEX_ATTRIBS, & immVab[0].x );
        }
        break;
      }
      default:
        break;
    }
  }
}

// a debug routine for forcing instanced program's uniforms to be updated
void Iff::ClearVersionsForProgram( RegalContext * ctx )
{
  Internal("Regal::Iff::UpdateUniforms", boost::print::optional(ctx,Logging::pointers));

  UniformMap * umap = NULL;
  if( currinst && currinst->prevInstance) {
    UserProgramInstanceInfo & ii = *currinst;
    UserProgramInstance & upi = *ii.prevInstance;

    upi.ffver = ~GLuint64(0);
    umap = & upi.ffuniforms;
  } else {
    if( currprog == NULL ) {
      return;
    }
    Program & pgm = *currprog;
    pgm.ver = ~GLuint64(0);
    umap = & pgm.uniforms;
  }

  for ( UniformMap::iterator i = umap->begin(); i != umap->end(); ++i )
  {
    UniformInfo & ui = (*i).second;
    ui.ver = ~GLuint64(0);
  }
}

inline bool operator == ( const Iff::State::Light & lhs, const Iff::State::Light & rhs )
{
  if ( !(lhs.enable == rhs.enable) )
    return false;
  if ( !(lhs.spotlight == rhs.spotlight) )
    return false;
  if ( !(lhs.attenuate == rhs.attenuate) )
    return false;
  if ( !(lhs.local == rhs.local) )
    return false;

  return true;
}

inline bool operator == ( const Iff::TexenvCombineState & lhs, const Iff::TexenvCombineState & rhs )
{
  if ( !(lhs.mode == rhs.mode) )
    return false;
  if ( !(lhs.src0 == rhs.src0) )
    return false;
  if ( !(lhs.src1 == rhs.src1) )
    return false;
  if ( !(lhs.src2 == rhs.src2) )
    return false;
  if ( !(lhs.op0 == rhs.op0) )
    return false;
  if ( !(lhs.op1 == rhs.op1) )
    return false;
  if ( !(lhs.op2 == rhs.op2) )
    return false;
  if ( !(lhs.mode == rhs.mode) )
    return false;
  if (lhs.scale != rhs.scale)
    return false;

  return true;
}

inline bool operator == ( const Iff::TextureEnv & lhs, const Iff::TextureEnv & rhs )
{
  if ( !(lhs.mode == rhs.mode) )
    return false;
  if ( !(lhs.rgb == rhs.rgb) )
    return false;
  if ( !(lhs.a == rhs.a) )
    return false;

  return true;
}

inline bool operator == ( const Iff::TextureUnit & lhs, const Iff::TextureUnit & rhs )
{
  if (lhs.ttb != rhs.ttb)
    return false;
  if (lhs.fmt != rhs.fmt)
    return false;
  if ( !(lhs.env == rhs.env) )
    return false;

  return true;
}

inline bool operator == ( const Iff::State::Texgen & lhs, const Iff::State::Texgen & rhs )
{
  if (lhs.enable != rhs.enable)
    return false;
  if ( !(lhs.mode == rhs.mode) )
    return false;

  return true;
}

inline bool operator == ( const Iff::State::Texture & lhs, const Iff::State::Texture & rhs )
{
  if (lhs.enables != rhs.enables)
    return false;
  if (lhs.useMatrix != rhs.useMatrix)
    return false;
  if ( !(lhs.unit == rhs.unit) )
    return false;

  size_t n = array_size( lhs.texgen );
  for (size_t ii=0; ii<n; ii++)
  {
    RegalAssertArrayIndex( lhs.texgen, ii );
    if ( !(lhs.texgen[ii] == rhs.texgen[ii]) )
      return false;
  }

  return true;
}

inline bool operator == ( const Iff::State::Store & lhs, const Iff::State::Store & rhs )
{
  if (lhs.hash != rhs.hash)
    return false;
  if (lhs.colorSum != rhs.colorSum)
    return false;
  if (lhs.rescaleNormal != rhs.rescaleNormal)
    return false;
  if (lhs.normalize != rhs.normalize)
    return false;
  if (lhs.shadeModelFlat != rhs.shadeModelFlat)
    return false;
  if (lhs.lighting != rhs.lighting)
    return false;
  if (lhs.lightModelLocalViewer != rhs.lightModelLocalViewer)
    return false;
  if (lhs.lightModelTwoSide != rhs.lightModelTwoSide)
    return false;
  if (lhs.lightModelSeparateSpecular != rhs.lightModelSeparateSpecular)
    return false;
  if (lhs.colorMaterial != rhs.colorMaterial)
    return false;
  if (lhs.attrArrayFlags != rhs.attrArrayFlags)
    return false;
  if (lhs.colorMaterialTarget0 != rhs.colorMaterialTarget0)
    return false;
  if (lhs.colorMaterialTarget1 != rhs.colorMaterialTarget1)
    return false;

  size_t n = array_size( lhs.light );
  for (size_t ii=0; ii<n; ii++)
  {
    RegalAssertArrayIndex( lhs.light, ii );
    if ( !(lhs.light[ii] == rhs.light[ii]) )
      return false;
  }
  n = array_size( lhs.tex );
  for (size_t ii=0; ii<n; ii++)
  {
    RegalAssertArrayIndex( lhs.tex, ii );
    if ( !(lhs.tex[ii] == rhs.tex[ii]) )
      return false;
  }
  n = array_size( lhs.clipPlaneEnabled );
  for (size_t ii=0; ii<n; ii++)
  {
    RegalAssertArrayIndex( lhs.clipPlaneEnabled, ii );
    if ( !(lhs.clipPlaneEnabled[ii] == rhs.clipPlaneEnabled[ii]) )
      return false;
  }
  return true;
}


GLuint Iff::GetFixedFunctionStateHash() {
  ffstate.Process( this );  // this early outs if version numbers match between raw and processed
  return ffstate.processed.hash;
}

void Iff::UseFixedFunctionProgram( RegalContext * ctx )
{
  Internal("Regal::Iff::UseFixedFunctionProgram", boost::print::optional(ctx,Logging::pointers));

  if ( currprog != NULL && currprog->ver == ver.Current() )
  {
    return;
  }
  ffstate.Process( this );
  uint32_t base = ( static_cast<uint32_t>(ffstate.processed.hash) & REGAL_FIXED_FUNCTION_PROGRAM_CACHE_MASK ) << REGAL_FIXED_FUNCTION_PROGRAM_CACHE_SET_BITS;
  int match = -1;
  const uint32_t n = static_cast<int>(array_size( ffprogs ));
  for ( uint32_t i = 0; i < REGAL_FIXED_FUNCTION_PROGRAM_CACHE_SET; i++ )
  {
    RegalAssertArrayIndex( ffprogs, base + i );
    if ( base + i >= n )
      continue;
    if ( ffprogs[ base + i ].store == ffstate.processed )
    {
      match = i;
      break;
    }
  }
  Program * p = NULL;
  if ( match < 0 )
  {
    match = 0;
    progcount++;
    for ( int i = 1; i < REGAL_FIXED_FUNCTION_PROGRAM_CACHE_SET; i++ )
    {
      RegalAssertArrayIndex( ffprogs, base + i );
      RegalAssertArrayIndex( ffprogs, base + match );
      if ( ffprogs[ base + i ].ver < ffprogs[ base + match ].ver )
      {
        match = i;
      }
    }
    RegalAssertArrayIndex( ffprogs, base + match );
    p = & ffprogs[ base + match ];
    // delete this program
    if ( p->pg != 0 )
    {
      DispatchTableGL & tbl = ctx->dispatcher.emulation;
      tbl.call(&tbl.glDeleteShader)( p->vs );
      tbl.call(&tbl.glDeleteShader)( p->fs );
      tbl.call(&tbl.glDeleteProgram)( p->pg );
      *p = Program();
    }
    GLuint vs = CreateFixedFunctionVertexShader( ctx );
    GLuint fs = CreateFixedFunctionFragmentShader( ctx );
    p->Init( ctx, ffstate.processed, vs, fs );
    p->progcount = progcount;
  }
  RegalAssertArrayIndex( ffprogs, base + match );
  currprog = & ffprogs[ base + match ];
  ctx->dispatcher.emulation.glUseProgram( currprog->pg );
  UpdateUniforms( ctx );
}

GLuint Iff::CreateFixedFunctionVertexShader( RegalContext * ctx ) {
  ffstate.Process( this );
  string_list vsSrc;
  GenerateVertexShaderSource( this, ffstate, vsSrc );
  GLuint vs;
  Program::Shader( ctx, ctx->dispatcher.emulation, GL_VERTEX_SHADER, vs, vsSrc.str().c_str() );
  return vs;
}

GLuint Iff::CreateFixedFunctionFragmentShader( RegalContext * ctx ) {
  ffstate.Process( this );
  string_list fsSrc;
  GenerateFragmentShaderSource( this, fsSrc );
  GLuint fs;
  Program::Shader( ctx, ctx->dispatcher.emulation, GL_FRAGMENT_SHADER, fs, fsSrc.str().c_str() );
  return fs;
}

bool NeedsUserShaderProgramInstance( State::Store & st ) {
  return st.alphaTest.enable && st.alphaTest.comp != Iff::CF_Always;
}

void Iff::UseShaderProgram( RegalContext * ctx )
{
  Internal("Regal::Iff::UseShaderProgram", boost::print::optional(ctx,Logging::pointers));

  ffstate.Process( this );

  // update currprog if necessary
  if ( (currprog == NULL) || (currprog->pg != program) ) {
    RegalAssert( shprogmap.count( program ) != 0 );
    currprog = & shprogmap[ program ];
  }

  if ( currprog->pg == 0 ) {
    Warning( "The program is 0. That can't be right.\n" );
    return;
  }

  if ( currprog->ver == ver.Current() ) {
    // no updates necessary
    return;
  }

  // determine if instancing is required
  if( (currprog->instanced == false) && NeedsUserShaderProgramInstance( ffstate.raw ) ) {
      currprog->instanced = true;
  }

  // if instanced, update per-instance uniforms if necessary
  DispatchTableGL & tbl = ctx->dispatcher.emulation;
  if( currprog->instanced ) {
    if( currinst == NULL ) {
      currinst = & inst[ currprog->pg ];
    }

    // create the primary program object, and make it the "vanilla" version the first instance
    if( currinst->instances.size() == 0 ) {
      ShaderInstance::InitProgram( tbl, currprog->pg, currinst->program );
      UserProgramInstanceKey k;
      ShaderInstance::InitProgramInstance( tbl, currinst->program, currprog->pg, currinst->instances[ k ].inst );
      currinst->prevKey = k;
      currinst->prevInstance = &currinst->instances[ k ];
      currinst->prevInstance->LocateUniforms( ctx, tbl );
    }

    UserProgramInstanceKey k( ffstate.processed );
    if( currinst->prevInstance == NULL || k != currinst->prevKey ) {
      currinst->prevKey = k;
      currinst->prevInstance = & currinst->instances[ k ];
      if( currinst->prevInstance->inst.prog != 0 ) {
        tbl.call(&tbl.glUseProgram)( currinst->prevInstance->inst.prog );
      }
    }
    UserProgramInstance & upi = * currinst->prevInstance;

    // create this instance of the program
    if( upi.inst.prog == 0 ) {
      std::vector<ShaderInstance::ShaderSource> sources;
      // alter the original sources as necessary, use a fetched or cached copy as preferred
      ShaderInstance::GetProgramSources( tbl, currinst->program.prog, sources);
      for( size_t i = 0; i < sources.size(); i++ ) {
        ShaderInstance::ShaderSource & ss = sources[i];
        string output_src;
        // If a tranformation or optimization failed should we optionally fallback
        // to a passthru shader if it's a fragment shader?
        // For now, blame glsl parse and fall back to existing source string.
        if ( OptimizeGLSL( gles, ss.type, ss.src, output_src, k.alphaFunc ) ) {
          ss.src = output_src;
        }
      }

      ShaderInstance::CreateProgramInstance( tbl, currinst->program, sources, upi.inst );
      upi.LocateUniforms( ctx, tbl );
      tbl.call(&tbl.glUseProgram)( upi.inst.prog );
    }

    if( upi.inst.ver != currinst->program.ver ) {
      upi.inst.UpdateUniforms( tbl, currinst->program );
      upi.inst.ver = currinst->program.ver;
    }
  }

  UpdateUniforms( ctx );
}

static int remove_version( string &str) {
  if( str.size() == 0 ) {
    return -1;
  }
  size_t pos = 0;
  while( (pos = str.find( "#version ", pos ) ) != string::npos ) {
    if( pos == 0 || str[pos-1] == '\n' ) { // at beginning of line
      str[ pos + 0 ] = '/';
      str[ pos + 1 ] = '/';

      string ver = str.substr( pos + 9, 3 );
      return atoi( ver.c_str() );
    }
    pos += 9;
  }
  return -1;
}

static void remove_precision( string & str ) {
  if( str.size() == 0 ) {
    return;
  }
  size_t pos = 0;
  while( (pos = str.find( "precision ", pos ) ) != string::npos ) {
    if( pos == 0 || str[pos-1] == '\n' ) { // at beginning of line
      str[ pos + 0 ] = '/';
      str[ pos + 1 ] = '/';
    }
    pos += 10;
  }
}

// replace ftransform with "rgl_ftform" in order to avoid conflict with possibly deprecated ftransform
static bool replace_ftransform( string & str ) {
  if( str.size() == 0 ) {
    return false;
  }
  size_t pos = 0;
  while ( (pos = str.find( "ftransform", pos ) ) != string::npos ) {
    int i = 0;

    // make sure ftransform is followed by "("
    while ( (str[ pos + 10 + i ] == ' ') && (pos != string::npos) )
      i++;

    if ( str[ pos + 10 + i ] != '(' )
      break;

    str.replace( pos, 10, "rgl_ftform", 10 );
    pos += 10;
    return true;
  }
  return false;
}

void Iff::ShaderSource( RegalContext *ctx, GLuint shader, GLsizei count, const GLchar * const * srcstr, const GLint *length)
{
  string src;
  for ( int i = 0; i < count; i++ ) {
    src += length != NULL ? string( srcstr[i], length[i] ) : string( srcstr[i] );
  }

  // comment out version in-place
  int version = -1;
  version = remove_version( src );

  // if used, replace ftransform with rgl_ftform
  bool uses_ftransform = replace_ftransform( src );

  if ( legacy ) {
   remove_precision( src );
  }

  // Preamble

  string_list ss;
  if (gles) {
    // hack around #version 100 on x86 failing compilation
    if ( ctx->info->gl_version_major >= 3 ) {
      ss << "#version 120\n";
      ss << "#define precision\n";
    } else {
#if REGAL_FORCE_DESKTOP_GLSL
      ss << "#version 140\n";
#else
      ss << "#version 100\n";
#if !REGAL_SYS_EMSCRIPTEN
      // GAB NOTE Dec 2018: do not use EXT_shadow_samplers on Emscripten/WebGL. This is not a valid WebGL extension, and make shader compilation fail.
      ss << "#extension GL_EXT_shadow_samplers : enable\n";
      ss << "#define shadow2D(a,b) vec4(shadow2DEXT(a,b))\n";
#else
      // GAB Note Dec: 2019: OES_standard_derivatives is available on all browsers
      ss << "#extension GL_OES_standard_derivatives : enable\n";
#endif
#endif
    }
  } else if (legacy) {
    ss << "#version 120\n";
    ss << "#define precision\n";
  } else {
    if (version > 0) {
      // We should honor the version in the original shader if we can, but leave out for now.
      //ss << "#version " << version << "\n";
      if (shaderTypeMap[ shader ] == GL_VERTEX_SHADER) {
        ss << "#define in attribute\n";
        ss << "#define out varying\n";
      } else {
        ss << "#define in varying\n";
      }
    } else {
      ss << "#version 140\n";
    }
  }
  if (gles || legacy) {
    if (shaderTypeMap[ shader ] == GL_VERTEX_SHADER) {
      ss << "#define in attribute\n";
      ss << "#define out varying\n";
    } else {
      ss << "#define in varying\n";
    }
  } else {
    if ( shaderTypeMap[ shader ] == GL_VERTEX_SHADER ) {
    } else {
      ss << "#define gl_FragColor           rglFragColor\n";
      ss << "out vec4 rglFragColor;\n";
      ss << "#define texture1D texture\n";
      ss << "#define texture2D texture\n";
      ss << "#define textureCube texture\n";
    }
  }
  if ( gles ) {
    ss << "precision highp float;\n";
  }

  ss << "#define centroid                              \n";
  ss << "#define gl_FogFragCoord        rglFogFragCoord\n";
  if ( shaderTypeMap[ shader ] == GL_VERTEX_SHADER ) {
    ss << "#define gl_Vertex              rglVertex\n";
    ss << "#define gl_Color               rglColor\n";
    ss << "#define gl_Normal              rglNormal\n";
  } else {
    ss << "#define gl_Color               rglFrontColor\n";
  }
  ss << "#define gl_FrontColor          rglFrontColor\n";
  if ( shaderTypeMap[ shader ] == GL_VERTEX_SHADER ) {
    ss << "#define gl_SecondaryColor               rglSecondaryColor\n";
  } else {
    ss << "#define gl_SecondaryColor               rglFrontSecondaryColor\n";
  }

  ss << "#define gl_FrontSecondaryColor rglFrontSecondaryColor\n";
  ss << "#define gl_ClipVertex          rglClipVertex\n";
  ss << "#define gl_FragDepth           rglFragDepth\n";
  if ( gles ) {
    ss << "#define texture3d              texture2d\n";
    ss << "#define texture3D(a,b)         texture2D(a,b.xy)\n";
    ss << "#define sampler3D              sampler2D\n";
  }

  if ( src.find( "gl_FogFragCoord" ) != string::npos ) {
    ss << "out float rglFogFragCoord;\n";
  }

// NOTE: ES 2.0 does not support gl_FragDepth, so we just discard it, for now
// See: http://www.khronos.org/registry/gles/specs/2.0/GLSL_ES_Specification_1.0.17.pdf

  if ( src.find( "gl_FragDepth" ) != string::npos ) {
    ss << (shaderTypeMap[shader]==GL_VERTEX_SHADER ? "out" : " ") <<  " float rglFragDepth;\n";
  }

  if ( uses_ftransform || (src.find( "gl_Vertex" ) != string::npos) ) {
    if ( shaderTypeMap[ shader ] == GL_VERTEX_SHADER ) {
      ss << "in vec4 rglVertex;\n";
    }
  }

  if ( src.find( "gl_Normal" ) != string::npos ) {
    if ( shaderTypeMap[ shader ] == GL_VERTEX_SHADER ) {
      ss << "in vec3 rglNormal;\n";
    }
  }

  if ( src.find( "gl_Color" ) != string::npos ) {
    if ( shaderTypeMap[ shader ] == GL_VERTEX_SHADER ) {
      ss << "in vec4 rglColor;\n";
    } else {
      ss << "in vec4 rglFrontColor;\n";
    }
  }

  if ( src.find( "gl_FrontColor" ) != string::npos ) {
    ss << "out vec4 rglFrontColor;\n";
  }

// NOTE: gl_SecondaryColor can be a vertex shader output, or a fragment shader input.
// See: http://www.opengl.org/registry/doc/GLSLangSpec.4.30.7.pdf

  if ( src.find( "gl_SecondaryColor" ) != string::npos ) {
    if ( shaderTypeMap[ shader ] == GL_VERTEX_SHADER )
    {
      ss << "in vec4 rglSecondaryColor;\n";
    }
    else
    {
      ss << "in vec4 rglFrontSecondaryColor;\n";
    }
  }

  if ( src.find( "gl_FrontSecondaryColor" ) != string::npos ) {
    ss << "out vec4 rglFrontSecondaryColor;\n";
  }
  if ( src.find( "gl_ClipVertex" ) != string::npos ) {
    // should be "out", but temporarily disabled due to register pressure concerns
    // ss << "out vec4 rglClipVertex;\n";
    ss << "vec4 rglClipVertex;\n";
  }

  // the below needs to be automated instead of done ad hoc like this - Cass
  // and it needs to handle "Inverse", "Transpose", and "InverseTranspose".

  if ( src.find( "gl_NormalMatrix" ) != string::npos ) {
    ss << "uniform mat3 rglNormalMatrix;\n";
  }

  if( src.find( "gl_ModelViewMatrix" ) != string::npos || uses_ftransform ) {
    ss << "uniform mat4 rglModelViewMatrix;\n";
  }

  if( src.find( "gl_ModelViewMatrixInverseTranspose" ) != string::npos ) {
    ss << "uniform mat4 rglModelViewMatrixInverseTranspose;\n";
  }

  if( src.find( "gl_ProjectionMatrix" ) != string::npos || uses_ftransform ) {
    ss << "uniform mat4 rglProjectionMatrix;\n";
  }

  if( src.find( "gl_ModelViewProjectionMatrix" ) != string::npos ) {
    ss << "uniform mat4 rglModelViewProjectionMatrix;\n";
  }

  const char *nums = "01234567";
  for( int i = 0; i < 8; i++ ) {
    if( src.find( string("gl_MultiTexCoord") + string( nums + i, 1 )) != string::npos ) {
      ss << "#define gl_MultiTexCoord" << i << " rglMultiTexCoord" << i << "\n";
      ss << "in vec4 rglMultiTexCoord" << i << ";\n";
    }
  }

  // GAB Note Jan 2019: take "gl_TexCoord" into consideration
  // Assume its size is REGAL_EMU_MAX_TEXTURE_UNITS (maybe this could be optimized by analysing max index used in the source shader
  if ( shaderTypeMap[ shader ] == GL_VERTEX_SHADER ) {
    ss << "#define gl_TexCoord rglTEXCOORD\n";
    ss << "out vec4 rglTEXCOORD[" << REGAL_EMU_MAX_TEXTURE_UNITS << "];\n";
  } else {
    ss << "#define gl_TexCoord rglTEXCOORD\n";
    ss << "in vec4 rglTEXCOORD[" << REGAL_EMU_MAX_TEXTURE_UNITS << "];\n";
  }

  const char * matrixSuffix[] = { "", "Inverse", "Transpose", "InverseTranspose" };

  for( int i = 0; i < 4; i++ ) {
    ss << "#define gl_ModelViewMatrix" << matrixSuffix[i] << " rglModelViewMatrix" << matrixSuffix[i] << "\n";
    ss << "#define gl_ProjectionMatrix" << matrixSuffix[i] << " rglProjectionMatrix" << matrixSuffix[i] << "\n";
    ss << "#define gl_ModelViewProjectionMatrix" << matrixSuffix[i] << " rglModelViewProjectionMatrix" << matrixSuffix[i] << "\n";
    ss << "#define gl_TextureMatrix0" << matrixSuffix[i] << " rglTextureMatrix0" << matrixSuffix[i] << "\n";
    ss << "\n";
  }

  ss << "#define gl_NormalMatrix rglNormalMatrix\n";


  ss << "#define gl_Sampler0 rglSampler0\n\n";

  if ( uses_ftransform )
  {
    ss << "vec4 rgl_ftform() { return gl_ProjectionMatrix * gl_ModelViewMatrix * rglVertex; }\n\n";
  }

  string preamble = ss.str();
  src = preamble + src;

  const GLchar * dumb = static_cast<const GLchar *>( src.c_str() );
  const GLchar ** dumber = & dumb;
  ctx->dispatcher.emulation.glShaderSource( shader, 1, dumber, NULL );
}

void Iff::LinkProgram( RegalContext *ctx, GLuint program )
{
  // need to bind attribs just before link for Regal-generated attribs
  if ( program != 0 )
  {
    if ( shprogmap.count( program ) == 0 )
    {
      ffstate.Process( this );
      ver.Reset();
      Program & p = shprogmap[ program ];
      p.pg = program;
      p.UserShaderModeAttribs(ctx);
    }
  }
  DispatchTableGL & tbl = ctx->dispatcher.emulation;
  tbl.glLinkProgram( program );
  Program & p = shprogmap[ program ];

  tbl.call(&tbl.glUseProgram)( program );
  p.Samplers( ctx, tbl );
  p.Uniforms( ctx, tbl );
  if( this->program != 0 ) {
    tbl.call(&tbl.glUseProgram)( this->program );
  }

}

void Iff::Uniform( RegalContext *ctx, GLenum type, int vecSize, GLint loc, GLsizei count, const void * data ) {
  UNUSED_PARAMETER(ctx);
  UNUSED_PARAMETER(type);
  UNUSED_PARAMETER(vecSize);
  ShaderInstance::Program & p = currinst->program;
  p.UpdateUniformStore( loc, count, data );
}

void Iff::Uniform( RegalContext *ctx, GLenum type, int cols, int rows, GLint loc, GLsizei count, const void * data ) {
  UNUSED_PARAMETER(ctx);
  UNUSED_PARAMETER(type);
  UNUSED_PARAMETER(cols);
  UNUSED_PARAMETER(rows);
  ShaderInstance::Program & p = currinst->program;
  p.UpdateUniformStore( loc, count, data );
}





}; // namespace Emu

REGAL_NAMESPACE_END

#endif // REGAL_EMULATION
