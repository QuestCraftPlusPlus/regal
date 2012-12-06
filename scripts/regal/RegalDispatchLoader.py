#!/usr/bin/python -B

from string import Template, upper, replace

from ApiUtil import outputCode
from ApiUtil import typeIsVoid

from ApiCodeGen import *

from RegalDispatchLog import apiDispatchFuncInitCode
from RegalDispatchEmu import dispatchSourceTemplate
from RegalContextInfo import cond

##############################################################################################

# CodeGen for API loader function definition.

def apiLoaderFuncDefineCode(apis, args):
  categoryPrev = None
  code = ''

  for api in apis:

    code += '\n'
    if api.name in cond:
      code += '#if %s\n' % cond[api.name]

    for function in api.functions:
      if not function.needsContext:
        continue
      if getattr(function,'regalOnly',False)==True:
        continue

      name   = function.name
      params = paramsDefaultCode(function.parameters, True)
      callParams = paramsNameCode(function.parameters)
      rType  = typeCode(function.ret.type)
      category  = getattr(function, 'category', None)
      version   = getattr(function, 'version', None)

      if category:
        category = category.replace('_DEPRECATED', '')
      elif version:
        category = version.replace('.', '_')
        category = 'GL_VERSION_' + category

      # Close prev category block.
      if categoryPrev and not (category == categoryPrev):
        code += '\n'

      # Begin new category block.
      if category and not (category == categoryPrev):
        code += '// %s\n\n' % category

      categoryPrev = category

      code += 'static %sREGAL_CALL %s%s(%s) \n{\n' % (rType, 'loader_', name, params)
      code += '  RegalContext * _context = REGAL_GET_CONTEXT();\n'
      code += '  RegalAssert(_context);\n'
      code += '  DispatchTable &_driver = _context->dispatcher.driver;\n'
      code += '  GetProcAddress(_driver.%s, "%s");\n' % (name, name)
      code += '  if (_driver.%s) {\n    ' % name
      if not typeIsVoid(rType):
        code += 'return '
      code += '_driver.%s(%s);\n' % ( name, callParams )
      if typeIsVoid(rType):
        code += '    return;\n'
      code += '  }\n'
      code += '  DispatchTable *_next = _driver._next;\n'
      code += '  RegalAssert(_next);\n'
      code += '  '
      if not typeIsVoid(rType):
        code += 'return '
      code += '_next->call(&_next->%s)(%s);\n'%(name, callParams)
      code += '}\n\n'

    if api.name in cond:
      code += '#endif // %s\n' % cond[api.name]
    code += '\n'

  # Close pending if block.
  if categoryPrev:
    code += '\n'

  return code


loaderLocalCode = ''

def generateLoaderSource(apis, args):

  funcDefine = apiLoaderFuncDefineCode( apis, args )
  funcInit   = apiDispatchFuncInitCode( apis, args, 'loader' )

  # Output

  substitute = {}

  substitute['LICENSE']         = args.license
  substitute['AUTOGENERATED']   = args.generated
  substitute['COPYRIGHT']       = args.copyright
  substitute['DISPATCH_NAME']   = 'Loader'
  substitute['LOCAL_CODE']      = loaderLocalCode
  substitute['LOCAL_INCLUDE']   = ''
  substitute['API_DISPATCH_FUNC_DEFINE'] = funcDefine
  substitute['API_DISPATCH_FUNC_INIT'] = funcInit
  substitute['IFDEF'] = '#if REGAL_DRIVER && !defined(__native_client__)\n\n'
  substitute['ENDIF'] = '#endif\n'

  outputCode( '%s/RegalDispatchLoader.cpp' % args.outdir, dispatchSourceTemplate.substitute(substitute))