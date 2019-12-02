/*
  Copyright (c) 2011-2013 NVIDIA Corporation
  Copyright (c) 2013 Cass Everitt
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

#include "RegalShader.h"

#if REGAL_GLSL_OPTIMIZER
#include "ast.h"
#include "glsl_parser_extras.h"
#include "glsl_parser.h"
#include "ir_optimization.h"
#include "ir_print_glsl_visitor.h"
#include "ir_print_visitor.h"
#include "loop_analysis.h"
#include "program.h"
#include "linker.h"
#endif // REGAL_GLSL_OPTIMIZER

#include <map>
using std::map;

REGAL_GLOBAL_END

REGAL_NAMESPACE_BEGIN


#if REGAL_GLSL_OPTIMIZER
extern "C" struct gl_shader *
_mesa_new_shader(struct gl_context *ctx, GLuint name, GLenum type);
#endif

namespace Shader {

  // external interface for using the GLSL optimizer
  bool OptimizeGLSL (bool is_es, GLenum type, string input, string& output, Emu::Iff::CompareFunc comp )
#if !REGAL_GLSL_OPTIMIZER
  {
    // returning false here means output will be ignored and the original shader will be used
    return false;
  }
#else
  {
    bool res = true;

    // this needs to be a global/plublic class member somewhere
    regal_glsl_ctx* ctx = regal_glsl_initialize(is_es ? API_OPENGLES2 : API_OPENGL_COMPAT);

    regal_glsl_shader_type shader_type = (type == GL_VERTEX_SHADER) ? kRegalGlslShaderVertex : kRegalGlslShaderFragment;
    regal_glsl_shader* shader = regal_glsl_parse (ctx, shader_type, input.c_str());
    // hold off on this
    // regal_glsl_replace_builtins( shader );
    regal_glsl_add_alpha_test( shader, comp );
    regal_glsl_optimize( shader );
    regal_glsl_gen_output( shader );

    bool parseOk = regal_glsl_get_status(shader);
    if (parseOk) {
      output = regal_glsl_get_output (shader);

      Internal( "Shader optimize success:\n", output.c_str());

    } else {
      Internal( "Shader optimize error: ",  regal_glsl_get_log(shader));
      res = false;
    }

    regal_glsl_shader_delete (shader);

    regal_glsl_cleanup (ctx);

    return res;
  }

  static void
  initialize_mesa_context(struct gl_context *ctx, gl_api api)
  {
    memset(ctx, 0, sizeof(*ctx));

    ctx->Extensions.ARB_fragment_coord_conventions = GL_TRUE;
    ctx->Extensions.EXT_texture_array = GL_TRUE;
    ctx->Extensions.NV_texture_rectangle = GL_TRUE;
    ctx->Extensions.ARB_shader_texture_lod = GL_TRUE;

    // Enable opengl es extensions we care about here
    if (api == API_OPENGLES2)
    {
      ctx->Extensions.OES_standard_derivatives = GL_TRUE;
#if REGAL_SYS_EMSCRIPTEN
      // GAB NOTE Dec 2018: Disable EXT_shadow_samplers on Emscripten/WebGL. This is not a valid WebGL extension
      ctx->Extensions.EXT_shadow_samplers = GL_FALSE;
#else
      ctx->Extensions.EXT_shadow_samplers = GL_TRUE;
#endif
    } else {
      ctx->Const.GLSLVersion = 140;
    }

    // align api with input shader
    ctx->API = api;

    /* 1.20 minimums. */
    ctx->Const.MaxLights = 8;
    ctx->Const.MaxClipPlanes = 8;
    ctx->Const.MaxTextureUnits = 2;

    /* allow high amount */
    ctx->Const.MaxTextureCoordUnits = 16;

    ctx->Const.VertexProgram.MaxAttribs = 16;
    ctx->Const.VertexProgram.MaxUniformComponents = 512;
    ctx->Const.MaxVarying = 8;
    ctx->Const.MaxVertexTextureImageUnits = 0;
    ctx->Const.MaxCombinedTextureImageUnits = 2;
    ctx->Const.MaxTextureImageUnits = 2;
    ctx->Const.FragmentProgram.MaxUniformComponents = 64;

    // support a reasonable number of draw buffers
    ctx->Const.MaxDrawBuffers = 4;

    ctx->Driver.NewShader = _mesa_new_shader;
  }


  struct regal_glsl_ctx {
    regal_glsl_ctx (gl_api api) {
      mem_ctx = ralloc_context (NULL);
#if ! REGAL_NO_ASSERT
      RegalContext *_context = REGAL_GET_CONTEXT();
      RegalAssert(_context);
#endif
      initialize_mesa_context (&mesa_ctx, api);
    }
    ~regal_glsl_ctx() {
      ralloc_free (mem_ctx);
    }
    struct gl_context mesa_ctx;
    void* mem_ctx;
  };

  regal_glsl_ctx* regal_glsl_initialize (gl_api api)
  {
    return new regal_glsl_ctx(api);
  }

  void regal_glsl_cleanup (regal_glsl_ctx* ctx)
  {
    delete ctx;
    _mesa_glsl_release_types();
    _mesa_glsl_release_functions();
  }


  struct regal_glsl_shader
  {
    static void* operator new(size_t size, void *mem_ctx)
    {
      void *node;
      node = ralloc_size(mem_ctx, size);
      assert(node != NULL);
      return node;
    }
    static void operator delete(void *node)
    {
      ralloc_free(node);
    }

    regal_glsl_shader ()
		: state( NULL )
    , rawOutput(0)
		, optimizedOutput(0)
		, status(false)
    {
      infoLog = "Shader not compiled yet";

      whole_program = rzalloc (NULL, struct gl_shader_program);
      assert(whole_program != NULL);
      whole_program->InfoLog = ralloc_strdup(whole_program, "");

      whole_program->Shaders = reralloc(whole_program, whole_program->Shaders, struct gl_shader *, whole_program->NumShaders + 1);
      assert(whole_program->Shaders != NULL);

      shader = rzalloc(whole_program, gl_shader);
      whole_program->Shaders[whole_program->NumShaders] = shader;
      whole_program->NumShaders++;
    }

    ~regal_glsl_shader()
    {
      ralloc_free (shader->ir);
      ralloc_free (state);
      for (unsigned i = 0; i < MESA_SHADER_TYPES; i++)
        ralloc_free(whole_program->_LinkedShaders[i]);
      ralloc_free(whole_program);
    }

    struct gl_shader_program* whole_program;
    struct gl_shader* shader;
    _mesa_glsl_parse_state* state;
    regal_glsl_ctx * ctx;

    char*	rawOutput;
    char*	optimizedOutput;
    const char*	infoLog;
    bool	status;
  };

  static inline void debug_print_ir (const char* name, exec_list* ir, _mesa_glsl_parse_state* state, void* memctx)
  {
    //_mesa_print_ir (ir, state);
    Error( "GLSLOptimize debug **** ", name, ":",
              _mesa_print_ir_glsl(ir, state, ralloc_strdup(memctx, ""), kPrintGlslFragment));
    validate_ir_tree(ir);
  }

  regal_glsl_shader* regal_glsl_parse (regal_glsl_ctx* ctx, regal_glsl_shader_type type, const char* shaderSource)
  {
    regal_glsl_shader* shader = new (ctx->mem_ctx) regal_glsl_shader ();
    shader->ctx = ctx;

    PrintGlslMode printMode;
    switch (type) {
      case kRegalGlslShaderVertex: shader->shader->Type = GL_VERTEX_SHADER; printMode = kPrintGlslVertex; break;
      case kRegalGlslShaderFragment: shader->shader->Type = GL_FRAGMENT_SHADER; printMode = kPrintGlslFragment; break;
      default:
        shader->infoLog = ralloc_asprintf (ctx->mem_ctx, "Unknown shader type %d", (int)type);
        shader->status = false;
        return shader;
    }

    _mesa_glsl_parse_state* state = new (ctx->mem_ctx) _mesa_glsl_parse_state (&ctx->mesa_ctx, shader->shader->Type, ctx->mem_ctx);
    shader->state = state;
    state->error = 0;

    state->error = !!glcpp_preprocess (state, &shaderSource, &state->info_log, state->extensions, &ctx->mesa_ctx);

    if (state->error)	{
      shader->status = !state->error;
      shader->infoLog = state->info_log;
      return shader;
    }

    _mesa_glsl_lexer_ctor (state, shaderSource);
    _mesa_glsl_parse (state);
    _mesa_glsl_lexer_dtor (state);

    exec_list* ir = new (ctx->mem_ctx) exec_list();
    shader->shader->ir = ir;

    if (!state->error && !state->translation_unit.is_empty())
      _mesa_ast_to_hir (ir, state);

    // Un-optimized output
    if (!state->error) {
      validate_ir_tree(ir);
      shader->rawOutput = _mesa_print_ir_glsl(ir, state, ralloc_strdup(ctx->mem_ctx, ""), printMode);
    }

    return shader;
  }

  void regal_glsl_gen_output( regal_glsl_shader * shader ) {

    // shorthand
    _mesa_glsl_parse_state * state = shader->state;
    exec_list * ir = shader->shader->ir;
    regal_glsl_ctx *ctx = shader->ctx;

    PrintGlslMode printMode;
    switch (shader->shader->Type) {
      case GL_VERTEX_SHADER: printMode = kPrintGlslVertex; break;
      case GL_FRAGMENT_SHADER: printMode = kPrintGlslFragment; break;
      default:
        shader->infoLog = ralloc_asprintf (ctx->mem_ctx, "Unknown shader type %d", (int)shader->shader->Type);
        shader->status = false;
        return;
    }


    // Link built-in functions
    shader->shader->symbols = state->symbols;
    memcpy(shader->shader->builtins_to_link, state->builtins_to_link, sizeof(shader->shader->builtins_to_link[0]) * state->num_builtins_to_link);
    shader->shader->num_builtins_to_link = state->num_builtins_to_link;

    if (!state->error && !ir->is_empty())	{
      gl_shader * linked_shader =
      link_intrastage_shaders(ctx->mem_ctx, &ctx->mesa_ctx, shader->whole_program, shader->whole_program->Shaders, shader->whole_program->NumShaders);
      if (!linked_shader) {
        shader->status = false;
        shader->infoLog = shader->whole_program->InfoLog;
        return;
      }
      ir = linked_shader->ir;

      //debug_print_ir ("==== After link ====", ir, state, ctx->mem_ctx);
    }

    // Do optimization post-link
    if ( false && !state->error && !ir->is_empty())	{
      validate_ir_tree(ir);
    }

    // Final optimized output
    if (!state->error) {
      shader->optimizedOutput = _mesa_print_ir_glsl(ir, state, ralloc_strdup(ctx->mem_ctx, ""), printMode);
    }

    shader->status = !state->error;
    shader->infoLog = state->info_log;
  }


  class add_alpha_test : public ir_hierarchical_visitor {
  public:

    add_alpha_test( Emu::Iff::CompareFunc cf ) : func( cf ), alphaRef( NULL ), fragColor( NULL ), fragData( NULL ), fragDataIndex( -1 ) {}

    virtual ir_visitor_status visit(ir_variable *var) {
      if( (fragColor == NULL) && !strcmp(var->name, "gl_FragColor") && (var->used == 1) && ( var->mode == ir_var_shader_out ) ) {
        fragColor = var;
      }

      if( (fragData == NULL) && !strcmp(var->name, "gl_FragData") && (var->used == 1) && ( var->mode == ir_var_shader_out ) ) {
        fragData = var;
        fragDataIndex = var->index;
        assert(fragDataIndex==0);
      }
      return visit_continue;
    }

    virtual ir_visitor_status visit_enter(ir_function *ir_f) {
      if( strcmp( ir_f->name, "main") != 0 ) {
        return visit_continue;
      }
      void * ctx = ralloc_parent( ir_f );
      alphaRef = new(ctx) ir_variable( glsl_type::vec2_type, "rglAlphaRef", ir_var_uniform, glsl_precision_undefined);
      ir_f->insert_before( alphaRef );
      return visit_continue;
    }


    virtual ir_visitor_status visit_leave(ir_function_signature *ir_fs) {
      if( strcmp( ir_fs->function_name(), "main") != 0 ) {
        return visit_continue;
      }
      void * ctx = ralloc_parent( ir_fs );
      ir_rvalue * test = NULL;
      ir_rvalue * a = NULL;

      if ( fragColor )
        a = new(ctx) ir_swizzle( new(ctx) ir_dereference_variable( fragColor ), 3, 0, 0, 0, 1 );

      if ( fragData )
        a = new(ctx) ir_swizzle( new(ctx) ir_dereference_array(fragData, new(ctx) ir_constant( fragDataIndex )), 3, 0, 0, 0, 1 );

      ir_rvalue * ref = new(ctx) ir_swizzle( new(ctx) ir_dereference_variable( alphaRef ), 0, 0, 0, 0, 1 );
      switch( func ) {
        case Emu::Iff::CF_Less:     test = new(ctx) ir_expression( ir_binop_less, glsl_type::bool_type, a, ref ); break;
        case Emu::Iff::CF_Lequal:   test = new(ctx) ir_expression( ir_binop_lequal, glsl_type::bool_type, a, ref ); break;
        case Emu::Iff::CF_Equal:    test = new(ctx) ir_expression( ir_binop_all_equal, glsl_type::bool_type, a, ref ); break;
        case Emu::Iff::CF_Gequal:   test = new(ctx) ir_expression( ir_binop_gequal, glsl_type::bool_type, a, ref ); break;
        case Emu::Iff::CF_Greater:  test = new(ctx) ir_expression( ir_binop_greater, glsl_type::bool_type, a, ref ); break;
        case Emu::Iff::CF_NotEqual: test = new(ctx) ir_expression( ir_binop_any_nequal, glsl_type::bool_type, a, ref ); break;
        case Emu::Iff::CF_Never:    test = new(ctx) ir_constant( false ); break;
        case Emu::Iff::CF_Always:   test = new(ctx) ir_constant( true );  break;
        default:
          // assert
          break;
      }

      ir_if * ifalphafailed = new(ctx) ir_if( new(ctx) ir_expression( ir_unop_logic_not, test ) );
      ifalphafailed->then_instructions.push_head( new(ctx) ir_discard() );
      ir_fs->body.get_tail()->insert_after( ifalphafailed );
      return visit_continue;
    }

    Emu::Iff::CompareFunc func;
    ir_variable *alphaRef;
    ir_variable *fragColor;
    ir_variable *fragData;
    int fragDataIndex;
  };

  void regal_glsl_add_alpha_test( regal_glsl_shader * shader, Emu::Iff::CompareFunc func ) {

    if( shader == NULL || shader->shader == NULL || shader->shader->Type != GL_FRAGMENT_SHADER ) {
      return;
    }
    add_alpha_test v(func);

    visit_list_elements(&v, shader->shader->ir );
    validate_ir_tree( shader->shader->ir );
  }

  void regal_glsl_optimize( regal_glsl_shader * shader ) {
    do_copy_propagation( shader->shader->ir  );
  }

  class replace_builtins : public ir_hierarchical_visitor {
  public:
    replace_builtins( regal_glsl_shader * shader_ ) : shader( shader_ ) {
      ir_variable * v;
      if( shader == NULL || shader->shader == NULL ) {
        // assert
        return;
      }
      void * ctx = ralloc_parent( shader->shader->ir );

      // attribs
      v = new(ctx) ir_variable( glsl_type::vec4_type, "rglColor", ir_var_shader_in, glsl_precision_undefined );
      vars[ "gl_Color" ] = v;
      v = new(ctx) ir_variable( glsl_type::vec3_type, "rglNormal", ir_var_shader_in, glsl_precision_undefined );
      vars[ "gl_Normal" ] = v;

      // uniforms
      v = new(ctx) ir_variable( glsl_type::mat4_type, "rglModelViewMatrix", ir_var_uniform, glsl_precision_undefined );
      vars[ "gl_ModelViewMatrix" ] = v;
      v = new(ctx) ir_variable( glsl_type::mat4_type, "rglProjectionMatrix", ir_var_uniform, glsl_precision_undefined );
      vars[ "gl_ProjectionMatrix" ] = v;
      v = new(ctx) ir_variable( glsl_type::mat4_type, "rglModelViewProjectionMatrix", ir_var_uniform, glsl_precision_undefined );
      vars[ "gl_ModelViewProjectionMatrix" ] = v;
      glsl_type * type = new glsl_type( *glsl_type::mat4_type );
      type->base_type = GLSL_TYPE_ARRAY;
      type->length = 8; // based on max texture units
      type->fields.array = glsl_type::mat4_type;
      //type = glsl_type(GL_FLOAT, GLSL_TYPE_FLOAT, 4, 4, "squirrel");
      v = new(ctx) ir_variable( type, "rglTextureMatrix", ir_var_uniform, glsl_precision_undefined );
      vars[ "gl_TextureMatrix" ] = v;
      //v = new(ctx) ir_variable( glsl_type::mat4_type, "rgl Matrix", ir_var_uniform, glsl_precision_undefined );
      //vars[ "gl_ Matrix" ] = v;

      /*
       uniform mat4 gl_ProjectionMatrix;
       uniform mat4 gl_ModelViewProjectionMatrix;
       uniform mat4 gl_TextureMatrix[gl_MaxTextureCoords];
       uniform mat3 gl_NormalMatrix;
       uniform mat4 gl_ModelViewMatrixInverse;
       uniform mat4 gl_ProjectionMatrixInverse;
       uniform mat4 gl_ModelViewProjectionMatrixInverse;
       uniform mat4 gl_TextureMatrixInverse[gl_MaxTextureCoords];
       uniform mat4 gl_ModelViewMatrixTranspose;
       uniform mat4 gl_ProjectionMatrixTranspose;
       uniform mat4 gl_ModelViewProjectionMatrixTranspose;
       uniform mat4 gl_TextureMatrixTranspose[gl_MaxTextureCoords];
       uniform mat4 gl_ModelViewMatrixInverseTranspose;
       uniform mat4 gl_ProjectionMatrixInverseTranspose;
       uniform mat4 gl_ModelViewProjectionMatrixInverseTranspose;
       uniform mat4 gl_TextureMatrixInverseTranspose[gl_MaxTextureCoords];
       */

    }

    ir_variable * add_global( string name ) {

      if( vars.count( name ) == 0 ) {
        return NULL;
      }
      if( declared.count( name ) ) {
        return NULL;
      }
      declared[ name ] = true;
      ir_variable * v = vars[ name ];
      shader->shader->ir->get_head()->insert_before( v );
      return v;
    }

    virtual ir_visitor_status visit(ir_dereference_variable *varref) {
      ir_variable * v = add_global( varref->var->name );
      if( v ) {
        varref->var = v;
      }
      return visit_continue;
    }


    regal_glsl_shader * shader;
    map< string, ir_variable * > vars;
    map< string, bool > declared;
  };

  void regal_glsl_replace_builtins( regal_glsl_shader * shader ) {
    if( shader == NULL || shader->shader == NULL ) {
      return;
    }
    replace_builtins v(shader);

    visit_list_elements(&v, shader->shader->ir );
    validate_ir_tree( shader->shader->ir );
  }


  void regal_glsl_shader_delete (regal_glsl_shader* shader)
  {
    delete shader;
  }

  bool regal_glsl_get_status (regal_glsl_shader* shader)
  {
    return shader->status;
  }

  const char* regal_glsl_get_output (regal_glsl_shader* shader)
  {
    return shader->optimizedOutput;
  }

  const char* regal_glsl_get_raw_output (regal_glsl_shader* shader)
  {
    return shader->rawOutput;
  }

  const char* regal_glsl_get_log (regal_glsl_shader* shader)
  {
    return shader->infoLog;
  }
#endif //REGAL_GLSL_OPTIMIZER
}

REGAL_NAMESPACE_END

#endif // REGAL_EMULATION
