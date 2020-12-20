import bindings = require('bindings')
import process = require('process')
import path = require('path')

if (process.platform === 'win32') {
  // 不使用系统自带的Python, 可能会缺包
  process.env.PATH = path.join(__dirname, '..', process.arch) + ';' + process.env.PATH
}

const clib: CLib = bindings('python-ts')

const PREFIX = 'python-ts'
const PYOBJECT_WRAPPER = 'python-ts/PyObject*'

type Primitive = any | number | string | boolean | null

// Python对象在NodeJS中的Wrapper
interface PyWrapper {
  type?: string // Wrapper类型, 一般为"PREFIX/PyObject*"
  value?: string // 指针对象 string(uint64_t(pointer))
  state?: string // PyThreadState* sub-interpreter state
  main?: string // PyObject * __main__ module addr, 只有"PREFIX/PyThreadState*"对象才有这个玩意, 用于隔离exec和eval
  repr?: string // repr(object)
  pytype?: string // type(object).__name__
  time?: number // 创建的时间
  index?: number // 本对象在references列表中的index, 就是第几个新建的对象

  hasOwnProperty?: Function // 假装自己是个Object对象

  // unwrap函数只有在Wrapper类型为"PREFIX/PyObject*"的时候才会有
  // 用于返回一个object, 内含PyObject的method和attr
  unwrap?: () => Unwrapped
}

// PyWrapper.unwrap之后的值
interface Unwrapped {
  // unwrap函数添加的自定义属性和方法
  __wrapper__: PyWrapper
  __refresh__: () => void // 刷新Unwrapped之内的非method属性的值

  // module相关内建属性
  __file__?: string
  __name__?: string
  __package__?: string
  __all__?: string[]
  __cached__?: string
  __doc__?: string

  // class相关内建属性/函数, 省略

  // 对象特有的函数，开放类型
  [propName: string]: any
}

interface PythonOptions {
  runtime_path?: string // Python Runtime的路径，就是有python3.dll的那个路径
  context?: boolean // 是否每个new Python对应一个新的context
  debug?: boolean // 多输出一些调试日志, 不过也没啥大用处就是了
}

// 参考`docs/DESIGN.md`或者`src/plugins.cc`
interface CLib {
  references: PyWrapper[]
  contexts: PyWrapper[]
  objects: Object
  _set_debug: (debug: boolean) => any
  _set_runtime_path: (path: string) => any
  _init_python: () => boolean
  _destroy_python: () => boolean
  _import_module: (name: string, context?: PyWrapper) => PyWrapper | null
  _reload_module: (name: string, context?: PyWrapper) => boolean
  _call_python: (pyobject: PyWrapper, method: string,
    args?: any[], kwargs?: Object,
    context?: PyWrapper, callback?: Function) => PyWrapper | Primitive
  _dir: (pyobject: PyWrapper, context?: PyWrapper) => any[]
  _exec: (code: string, context?: PyWrapper, callback?: Function) => PyWrapper | Primitive
  _eval: (code: string, context?: PyWrapper, callback?: Function) => PyWrapper | Primitive
  _delete_pyobject: (pyobject: PyWrapper, context?: PyWrapper) => boolean
  _create_pycontext: () => PyWrapper
  _delete_pycontext: (pycontext: PyWrapper) => boolean
}

class Python {
  public runtime_path: string // Python Runtime的路径，就是有python3.dll的那个路径
  public context: PyWrapper // 是否每个new Python对应一个新的context
  public debug: boolean // 多输出一些调试日志, 不过也没啥大用处就是了
  public is_deleted: boolean

  constructor (options: PythonOptions = {}) {
    this.is_deleted = false
    this.configure(options)
  }

  private configure (options: PythonOptions): void {
    this.runtime_path = options.runtime_path ?? process.arch
    this.debug = options.debug || false

    clib._set_debug(this.debug)
    clib._set_runtime_path(this.runtime_path)
    clib._init_python()
    if (options.context) {
      this.context = clib._create_pycontext()
    } else {
      // 空字典代表全局Context
      this.context = {}
    }
  }

  private _check_ok (): void {
    if (this.is_deleted) { throw Error('This Python context has been deleted, please use a new one!') }
  }

  public isPyObject (object: any): boolean {
    return (object as boolean) && object.type && object.type === PYOBJECT_WRAPPER
  }

  // 对象是否属于本上下文
  public sameContext (object: PyWrapper | Unwrapped): boolean {
    if (Object.prototype.hasOwnProperty.call(object, '__wrapper__') as boolean) {
      object = (object as Unwrapped).__wrapper__
    }
    return this.isPyObject(object) && (object.state === this.context.state)
  }

  // 添加Python模块的搜索路径
  public add_syspath (dir: string): void {
    dir = path.resolve(dir)
    this.exec(`_path = '${dir.replace(/\\/g, '\\\\')}'`)
    this.exec('if _path not in sys.path: sys.path.insert(0, _path)')
  }

  // 把Python下面的method和attr都导入进来
  public unwrap (object: PyWrapper): Unwrapped {
    this._check_ok()
    const unwrapped = {
      __wrapper__: object,
      __refresh__: () => {
        const dirs = clib._dir(object, this.context)
        for (let i = 0; i < dirs.length; i++) {
          const name = dirs[i][0]
          const isMethod = dirs[i][1] as boolean
          const attr = dirs[i][2]
          if (!isMethod && !this.isPyObject(attr)) {
            // __refresh__ updates primitive attr only
            unwrapped[name] = attr
          }
        }
      }
    }
    const dirs = clib._dir(object, this.context)
    for (let i = 0; i < dirs.length; i++) {
      const name = dirs[i][0] as string
      const isMethod = dirs[i][1] as boolean
      const attr = dirs[i][2]
      if (isMethod) {
        unwrapped[name] = (...args) => {
          return this.call(object, name, args)
        }
        if (!name.startsWith('__')) {
          unwrapped[`${name}_async`] = async (...args) => {
            return await this.call_async(object, name, args)
          }
        }
      } else {
        if (this.isPyObject(attr)) {
          attr.unwrap = () => {
            return this.unwrap(attr)
          }
        }
        unwrapped[name] = attr
      }
    }
    return unwrapped
  }

  public call (object: PyWrapper, name: string,
    args?: any[], kwargs?: Object): PyWrapper | Primitive {
    this._check_ok()
    args = args ?? []
    kwargs = kwargs ?? {}
    let result = clib._call_python(object, name, args, kwargs, this.context)
    if (this.isPyObject(result)) {
      result = result as PyWrapper
      result.unwrap = () => {
        return this.unwrap(result as PyWrapper)
      }
    }
    return result
  }

  public async call_async (object: PyWrapper, name: string,
    args?: any[], kwargs?: Object): Promise<PyWrapper | Primitive> {
    this._check_ok()
    args = args ?? []
    kwargs = kwargs ?? {}
    return await new Promise((resolve, reject) => {
      clib._call_python(object, name, args, kwargs, this.context, (data) => {
        if (typeof data === 'string' && data.startsWith(PREFIX)) {
          reject(data)
        } else {
          resolve(data)
        }
      })
    })
  }

  public import (name: string): Unwrapped {
    this._check_ok()
    const result = clib._import_module(name, this.context)
    if (this.isPyObject(result)) {
      return this.unwrap(result)
    }
  }

  // 重新加载一个module, 可以是名字或者Unwrapped对象
  public reload (module: string | Unwrapped): boolean {
    this._check_ok()
    if (typeof module === 'string') {
      return clib._reload_module(module, this.context)
    } else {
      return clib._reload_module(module.__name__, this.context)
    }
  }

  // 调用Python下的exec, code可以是任意合法代码段, 不会返回任何有意义的值
  public exec (code: string): PyWrapper | Primitive {
    this._check_ok()
    return clib._exec(code, this.context)
  }

  // 异步调用exec
  public async exec_async (code: string): Promise<PyWrapper | Primitive> {
    this._check_ok()
    return await new Promise((resolve, reject) => {
      clib._exec(code, this.context, (data) => {
        if (typeof data === 'string' && data.startsWith(PREFIX)) {
          reject(data)
        } else {
          resolve(data)
        }
      })
    })
  }

  // 调用Python下的eval, code必须是一个合法的Python表达式
  public eval (code: string): PyWrapper | Primitive {
    this._check_ok()
    return clib._eval(code, this.context)
  }

  // 异步调用eval
  public async eval_async (code: string, context?: PyWrapper): Promise<PyWrapper | Primitive> {
    this._check_ok()
    return await new Promise((resolve, reject) => {
      clib._eval(code, this.context, (data) => {
        if (typeof data === 'string' && data.startsWith(PREFIX)) {
          reject(data)
        } else {
          resolve(data)
        }
      })
    })
  }

  // 刷新(更新)Unwrapped对象下面的非method属性
  public refresh (object: Unwrapped): Unwrapped {
    this._check_ok()
    object.__refresh__()
    return object
  }

  // 回收指定的PyWrapper对象
  public gc (object: PyWrapper | Unwrapped): boolean {
    this._check_ok()
    if (this.isPyObject(object)) {
      return clib._delete_pyobject(object as PyWrapper, this.context)
    } else {
      return clib._delete_pyobject((object as Unwrapped).__wrapper__, this.context)
    }
  }

  /* 清理所有this.context下的Object

    如果context.state=undefined, 那么将销毁所有全局的PyObject, 例如

    ```typescript
    let py1 = new Python()
    let py2 = new Python()
    py1.clear() // => py2下创建的对象也会被清空, 因为他们共享一个全局上下文
    ```
    */
  public clear (): void {
    this._check_ok()
    for (const ref of clib.references) {
      if (ref as boolean && this.context.state === ref.state) {
        clib._delete_pyobject(ref, this.context)
      }
    }
  }

  // 直接把Context都关了, 同时会清理Context下的所有PyObject对象
  public delete (): boolean {
    this._check_ok()
    this.clear()
    if (this.context as boolean) {
      this.is_deleted = clib._delete_pycontext(this.context)
    }
    return this.is_deleted
  }
}

export { Python, clib }
