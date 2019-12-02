/*
  Copyright (c) 2011-2013 NVIDIA Corporation
  Copyright (c) 2011-2013 Cass Everitt
  Copyright (c) 2011-2013 Seth Williams
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

/*

 Shader support for using Mesa's GLSL optimizer for patching shaders for emulation purposes.

 */

#ifndef __REGAL_SHADER_H__
#define __REGAL_SHADER_H__

#include "RegalUtil.h"

REGAL_GLOBAL_BEGIN

#include "RegalEmu.h"
#include "RegalIff.h"

using std::string;

REGAL_GLOBAL_END

REGAL_NAMESPACE_BEGIN

namespace Shader {

  bool OptimizeGLSL (bool is_es, GLenum type, string input, string& output, Emu::Iff::CompareFunc comp );
#if REGAL_GLSL_OPTIMIZER
  struct regal_glsl_shader;
  struct regal_glsl_ctx;

  enum regal_glsl_shader_type {
    kRegalGlslShaderVertex = 0,
    kRegalGlslShaderFragment,
  };

  regal_glsl_ctx* regal_glsl_initialize (gl_api api);
  void regal_glsl_cleanup (regal_glsl_ctx* ctx);

  regal_glsl_shader* regal_glsl_parse (regal_glsl_ctx* ctx, regal_glsl_shader_type type, const char* shaderSource);
  void regal_glsl_replace_builtins( regal_glsl_shader * shader );
  void regal_glsl_add_alpha_test( regal_glsl_shader * shader, Emu::Iff::CompareFunc func );
  void regal_glsl_optimize( regal_glsl_shader * shader );
  void regal_glsl_gen_output( regal_glsl_shader * shader );

  bool regal_glsl_get_status (regal_glsl_shader* shader);
  const char* regal_glsl_get_output (regal_glsl_shader* shader);
  const char* regal_glsl_get_raw_output (regal_glsl_shader* shader);
  const char* regal_glsl_get_log (regal_glsl_shader* shader);
  void regal_glsl_shader_delete (regal_glsl_shader* shader);
#endif
}

REGAL_NAMESPACE_END

#endif // ! __REGAL_SHADER_H__
