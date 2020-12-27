# Python.ts: 在 NodeJS 里面嵌入 Python 运行时

[toc]

## 1. The Problem

2020年了，现在**实现一个桌面端的App的时候，最为常用的做法是用Electron**，这样的好处有 1）跨平台 2）开发效率高 3）UI效果好 4）Web技术栈开发人员好找。

对于一个功能丰富的基于Electron的App，**很多时候会遇到怎么扩展其功能的问题**。目前最为常见的做法是把插件作为一个简单的NodeJS项目, 通过`package.json`来指定入口 ——Visual Studio Code的插件是这样实现的；更老一点的做法，如Chrome，也是作为一个Web项目，写HTML/JS/CSS，然后通过定义`manifest.json`来定义入口。

在某些场景下，比如**RPA流程自动化**，如果插件的主要目的是提供各个系统的自动化操作，我们可能会想用Python来制作插件。**用Python来制作插件的好处有 1) 自动化生态很好 2) 开发快上手快 3) 附加科学计算(AI)插件的能力。**

用Python来做插件就需要定义一个如何和Electron的后端NodeJS互相**IPC的方式**，一般来说我们会通过一个RPC框架来做远程调用, 常见的选择是使用`gRPC`/`Thrift`，但是也有问题——每个插件会单独占用一个进程，插件一多端口占用可以用Pipe解决，但内存占用会是个问题。

解决的思路据我所知有两种

1）实现一套**可以Multiplexing的RPC框架**，充分利用NodeJS和Python都是动态语言的特性，由一个Server来动态管理全部的插件，这个方式算是进化版的RPC Approach。

2）**把Python的Runtime内嵌到NodeJS中**，使得可以在NodeJS中原生载入Python的`module`, `class`, `method`, etc。 并对其进行如同JavaScript原生对象的调用操作。这种方式有时也被称为FFI(Foreign Function Interface)方式。

本项目就是思路2的一种实现



## 2. FFI Approach

如何把Python的Runtime内嵌到NodeJS中呢?

Python是用C实现的，自带 [C-API](https://docs.python.org/zh-cn/3/c-api/index.html)

NodeJS是用C++实现的，也有基于C的 [AddonAPI](http://nodejs.cn/api/addons.html)

于是我们就可以写一个基于C的双向[Wrapper](https://en.wikipedia.org/wiki/Adapter_pattern)，在NodeJS中调用Python的时候，

1）把调用的方法和参数Wrap成Python的方法和参数，再进行Python调用

2）把Python返回的对象Wrap成NodeJS的对象，返回给NodeJS的Runtime

为了做好1）我们必须实现Python的C类型定义和常用方法；同样为了实现2）我们也得了解Node的C类型定义和常用方法；接下来我们分别介绍一下对应的实现方式。



### 2.1 Embed Python in C

#### 2.1.1 Embedded Python

在C/C++中扩展/嵌入Python还是挺常见的，Python官网甚至有篇关于此的多篇文档文档 

- <https://docs.python.org/zh-cn/3/extending/index.html>
- <https://docs.python.org/zh-cn/3/extending/embedding.html>

这里只做简单介绍

```C
#define PY_SSIZE_T_CLEAN
#include <Python.h>

int
main(int argc, char *argv[])
{
    wchar_t *program = Py_DecodeLocale(argv[0], NULL);
    if (program == NULL) {
        fprintf(stderr, "Fatal error: cannot decode argv[0]\n");
        exit(1);
    }
    Py_SetProgramName(program);  /* optional but recommended */
    Py_Initialize();
    PyRun_SimpleString("from time import time,ctime\n"
                       "print('Today is', ctime(time()))\n");
    if (Py_FinalizeEx() < 0) {
        exit(120);
    }
    PyMem_RawFree(program);
    return 0;
}
```

- Py_Initialize: 初始化Python, 不初始化后续无法使用
- PyRun_SimpleString: 执行一段简单的Python程序
- Py_FinalizeEx: 销毁Python
- PyMem_RawFree: 释放Python内存

类似Py_Initialize这种函数，统称为Python的C API, Python大约有700多个稳定的C-API, 手册在这里 [Python/C API References](https://docs.python.org/3/c-api/index.html)



#### 2.1.2 Python C-API

这里介绍一下常见的Python C-API和用法



##### Py_XINCREF, Py_XDECREF

Python的C方法通常会返回PyObject*, 这个指针有时候会增加GC计数器, 有时候只是一个Reference Pointer, 并不增加GC计数器，为了不泄露内存, 在调用Python的C方法以后，往往需要看情况手动增减GC计数器

```c
PyObject *type, *value, *traceback
// 这个函数会自动Py_XINCREF type, value, traceback
PyErr_Fetch(&type, &value, &traceback)
    
// do something with traceback
    
// 手动Py_XDECREF这些对象, 让他们可以被回收
Py_XDECREF(type)
Py_XDECREF(value)
Py_XDECREF(traceback)
```



##### PyImport_ImportModule

加载一个Python的Module

```c
PyObject *pModule = PyImport_ImportModule(name)
```



##### PyObject_GetAttrString, PyObject_Call

动态地拿到一个Python的函数，并调用之

```c
// callable = getattr(object, attr)
PyObject *pCallable = PyObject_GetAttrString(pObject, attr);

// ret = callable(*args, **kwargs)
PyObject *pRet = PyObject_Call(pCallable, pArgs, pKwargs);
```



##### PyBuildValule

简单地构造一个Python对象

```c
// Build Python String from c string
Py_BuildValue("s", "this is a c string");

// Build Python Double from c double
Py_BuildValue("d", 3.1415926);

// Build Python boolean from c boolean
Py_BuildValue("p", TRUE);
```



##### PyList_New, PyList_Append

List的操作

```c
// l = []
PyObject *pList = PyList_New(0);

// l.append(item)
PyList_Append(pList, pItem);
```



##### PyDict_New, PyDict_SetItemString

Dict的操作

```c
// d = {}
PyObject *pDict = PyDict_New();

// d[key] = value
PyDict_SetItemString(key, pValue);
```



### 2.2. NodeJS C/C++ API

#### 2.3.1 N-API

NodeJS是基于v8实现的后端JavaScript的Runtime，但是C++因为要支持函数重载，会给函数加一个混淆头,  所以没有一个稳定的ABI(Application Binary Interface)。

很久以前大家做NodeJS的扩展都是直接用C++ include v8来做，但这样兼容性很差，跨一个Node版本以后插件都不能用了，所以在2016年前后在Node在做8.0版本的时候终于封了一层C-API作为稳定版API，这样大家以后做插件都能跨Node版本了。

这一版的API被称作**N-API**, 文档在此: <https://nodejs.org/api/n-api.html>



举个例子, 实现一个Object赋值

```c
// object = {}
napi_status status;
napi_value object, string;
status = napi_create_object(env, &object);
if (status != napi_ok) {
  napi_throw_error(env, ...);
  return;
}

// bar = "bar"
status = napi_create_string_utf8(env, "bar", NAPI_AUTO_LENGTH, &string);
if (status != napi_ok) {
  napi_throw_error(env, ...);
  return;
}

// object["foo"] = bar
status = napi_set_named_property(env, object, "foo", string);
if (status != napi_ok) {
  napi_throw_error(env, ...);
  return;
}
```

- napi_create_object, 创建一个object
- napi_create_string_utf8, 创建一个utf8编码的字符串
- napi_set_named_property, object对象赋值



#### 2.3.2 Node Addon API

如上，N-API实现一个功能还是挺蛋疼的，所以NodeJS官方基于N-API又封装了一套C++的接口，叫做 [node-addon-api](https://github.com/nodejs/node-addon-api)

用node-addon-api实现上述同样的功能，画风如下

```c++
Object obj = Object::New(env);
obj["foo"] = String::New(env, "bar");
```

这下轻松多了

用node-addon-api来实现一个完整的Node插件，一般是这样

```c++
#include <napi.h>

Napi::String Method(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  return Napi::String::New(env, "world");
}

Napi::Object Init(Napi::Env env, Napi::Object exports) {
  exports.Set(Napi::String::New(env, "hello"),
              Napi::Function::New(env, Method));
  return exports;
}

NODE_API_MODULE(hello, Init)
```

更多完整的例子可见

- <https://github.com/nodejs/node-addon-examples>

node-addon-api自己的文档就在github主页里面

- <https://github.com/nodejs/node-addon-api>




## 3. C++ Implementations

接下来就是怎么运用以上的知识来实现我们的需求了，为了更轻松地写出方便好用的模块，我们拆成两部分来做

第一部分: 用C++把基础功能封装成函数, 供NodeJS调用

第二部分: 用TypeScript在第一部分函数的基础上, 封装成更为简单易用的NodeJS模块

这里我们先介绍第一部分的主要功能

### 3.1 Basic Configurations

目前所有的node插件都是用node-gyp来编译的，安装方法如下

```shell
npm install -g node-gyp
```

我们配置一下binding.gyp，主要是告诉编译器`.h`头文件的类型定义在哪, `.dll`/`.so`/`.a`这些动态/静态库文件在哪。

##### binding.gyp

```json
{
  "targets": [
    {
      "target_name": "python-ts",
      "sources": [ "src/binding.cc" ],
      "cflags!": [ "-fno-exceptions" ],
      "cflags_cc!": [ "-fno-exceptions" ],
      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")",
        "<!@(node -p \"require('./src/gyp').include_dirs\")"
      ],
      "dependencies": [
        "<!(node -p \"require('node-addon-api').gyp\")"
      ],
      "link_settings": {
        "libraries": [
          "<!@(node -p \"require('./src/gyp').libraries\")"
        ],
        "library_dirs": [
          "<!@(node -p \"require('./src/gyp').library_dirs\")"
        ]
      },
      "defines": ["NAPI_DISABLE_CPP_EXCEPTIONS"]
    }
  ],
}
```

`src/gyp.js`这个文件主要用于跨平台获得`includes`目录和`libs`目录,  这里不展开了

在binding.gyp中我们指定了编译文件为`src/binding.cc`，所以主入口都写在里面，编译可以运行以下命令

```shell
node-gyp configure
node-gyp build

// 以上命令合二为一
node-gyp rebuild
```



### 3.2 Load a Python Module

```c++
// let os = clib._import_module("os")
Napi::Object _import_module(const Napi::CallbackInfo &info)
{
	// ...

    module_name = info[0].As<Napi::String>();

    pModule = PyImport_ImportModule(module_name.Utf8Value().c_str());
    if (pModule == NULL) {
        throw_pyexception_in_javascript(env, "python-ts._import_module failed");
    }
    else {
        result = serialize_pyobject(env, pModule);
    }
    
    Py_XDECREF(pModule);
    
    return result;
}
```

其中封装了两个公共函数

- throw_pyexception_in_javascript, 把Python的出错信息封装成NodeJS的Error丢出来
- serialize_pyobject, 把Python对象转成NodeJS的一个Object, 里面存一个指针并INCREF一下



### 3.3 Call Python Function

```c++
// let res = clib._call_python(object, attr, args, kwargs)
// python: res = getattr(object, attr)(*args, **kwargs)
Napi::Value _call_python(const Napi::CallbackInfo &info)
{
    // ...

    object = info[0].As<Napi::Object>();
    attr = info[1].As<Napi::String>();
    args = info[2].As<Napi::Array>();
    kwargs = info[3].As<Napi::Object>();
    
    pObject = deserialize_pyobject(env, object);
    pArgs = napi_value_to_pyobject(env, args);
	pKwargs = napi_value_to_pyobject(env, kwargs);

    // getattr
    pCallable = PyObject_GetAttrString(pObject, attr.Utf8Value().c_str());
    
    // call function
    pRet = PyObject_Call(pCallable, pArgs, pKwargs);

    Py_DECREF(pCallable);
    Py_XDECREF(pArgs);
    Py_XDECREF(pKwargs);

    result = pyobject_to_napi_value(env, pRet, substate);
    Py_DECREF(pRet);

    return result;
}
```

这里面有两个最重要的函数

- napi_value_to_pyobject, 把任意NodeJS的对象(napi_value)转换成一个PyObject对象
- pyobject_to_napi_value, 把任意的PyObject对象转换成一个NodeJS对象(napi_value)

这两个函数可以说是最核心的部分(之一)，值得单开一章来做出阐述



### 3.4 PyObject in NodeJS

PyObject在NodeJS中有两种表示方式

1）如果是None/int/bool/float/str/tuple/list/dict/set这种基础类型, 或者基础类型的组合, 那么我们直接转成对应的NodeJS类型

- None => null
- int => number
- float => number
- str => string
- tuple => array
- list => array
- dict => object
- set => array

对应的转换函数类似下面

```c++
Napi::Value pyobject_to_napi_value(const Napi::Env &env, PyObject *object) {
    Py_ssize_t i, length;
    PyObject *keys, *values, *pIterator, *pItem;
    Napi::Value item, result, key;
    Napi::Object obj;
    Napi::Array arr;

    if (object == NULL || object == Py_None) {
        result = env.Null();
    } else if (PyLong_Check(object)){
        result = Napi::Number::New(env, PyLong_AsDouble(object));
    } else if (PyBool_Check(object)){
        result = object == Py_True ? Napi::Boolean::New(env, true) : Napi::Boolean::New(env, false);
    } else if (PyFloat_Check(object)) {
        result = Napi::Number::New(env, PyFloat_AsDouble(object));
    } else if (PyUnicode_Check(object)) {
        result = Napi::String::New(env, PyUnicode_AsUTF8(object));
    } else if (PyTuple_Check(object)) {
        length = PyTuple_Size(object);
        arr = Napi::Array::New(env, size_t(length));
        for (i = 0; i < length; i++) {
            item = pyobject_to_napi_value(env, PyTuple_GetItem(object, i));
            arr.Set(uint32_t(i), item);
        }
        result = arr;
    } else if (PyList_Check(object)) {
        length = PyList_Size(object);
        arr = Napi::Array::New(env, size_t(length));
        for (i = 0; i < length; i++) {
            item = pyobject_to_napi_value(env, PyList_GetItem(object, i));
            arr.Set(uint32_t(i), item);
        }
        result = arr;
    } else if (PyDict_Check(object)) {
        length = PyDict_Size(object);
        obj = Napi::Object::New(env);
        keys = PyDict_Keys(object);
        values = PyDict_Values(object);
        for (i = 0; i < length; i++) {
            key = pyobject_to_napi_value(env, PyList_GetItem(keys, i));
            if (!key.IsString()) {
                // Python的dict可以接受非string类的key, 遇到这种情况就放弃展开, 返回PyWrapper
                Py_DECREF(keys);
                Py_DECREF(values);
                result = serialize_pyobject(env, object);
                return result;
            } else {
                obj.Set(key, pyobject_to_napi_value(env, PyList_GetItem(values, i)));
            }
        }
        Py_DECREF(keys);
        Py_DECREF(values);
        result = obj;
    } else if (PySet_Check(object)) {
        pIterator = PyObject_GetIter(object);
        arr = Napi::Array::New(env);
        i = 0;
        while ((pItem = PyIter_Next(pIterator))) {
            arr.Set(i++, pyobject_to_napi_value(env, pItem));
            Py_DECREF(pItem);
        }
        Py_DECREF(pIterator);
        result = arr;
    } else {
        // 找不到任何序列化方式了，只好存个指针
        // 这个指针对象永远不会被Py_DECREF, 除非由JS发起_delete_pyobject
        result = serialize_pyobject(env, object);
    }

    return result;
}
```

类似的，我们也需要定义`napi_value_to_pyobject`方法，用于反向把NodeJS对象转为Python对象，这里代码是类似的，就不展开了。

2) 当无法用原生类型来转换的时候，我们把PyObject的指针记下来，包装成一个object, JavaScript无需理解这个object的含义, 只要再传回Python的时候Python侧知道怎么调用这个PyObject即可

注意，我们需要用`Py_INCREF`来增加这个PyObject的引用计数, 同时，为了以后有办法销毁这个对象, 不造成内存溢出, 所以我们把这个对象存到全局变量`references`中，同时为了避免同一个对象反复存取，我们把做一个简易的缓存`objects`

```c++
Napi::Object serialize_pyobject(const Napi::Env &env, PyObject *object) {
    Napi::Object result, objects = env.Global().Get(OBJECTS).As<Napi::Object>();
    Napi::Array references = env.Global().Get(REFERENCES).As<Napi::Array>();
    PyObject *repr;
    std::string value = uintptr_to_str((uintptr_t)object);

    if (objects.Has(value)) {
        return objects.Get(value).As<Napi::Object>();
    }

    result = Napi::Object::New(env);
    repr = PyObject_Repr(object);

    result.Set("type", PYOBJECT_WRAPPER);
    result.Set("value", value);
    Py_INCREF(object); // 已经序列化过的对象手动加一个reference，避免被回收
    if (repr != NULL) {
        result.Set("repr", PyUnicode_AsUTF8(repr));
    } else {
        result.Set("repr", env.Null());
    }
    Py_XDECREF(repr);
    result.Set("pytype", Py_TYPE(object)->tp_name);
    result.Set("time", Napi::Number::New(env, (double)std::time(0)));

    // 把新鲜热乎的不安全的没被Py_DECREF的指针放到references里面，供后续使用
    smutex.lock();
    result.Set("index", rtop);
    references.Set(rtop, result);
    rtop++;
    smutex.unlock();

    // 跟踪对象以便于复用
    objects.Set(value, result);
    return result;
}
```



### 3.5 Async Execute Python Calls (in Threads)

经过之前的操作，我们已经成功地让NodeJS可以调用Python代码，但是还有一个很严重的问题——执行Python代码会阻塞NodeJS的线程。

想象一下这样的场景：我们需要调用一个Python的脚本，这个脚本会长时间等待一个IO事件，但是因为NodeJS是单线程执行的，当前线程被Python的代码调用阻塞了，所以所有NodeJS下的其他任务都得乖乖排队等待这个Python脚本执行结束——这无疑是很低效的。

所幸`node-addon-api`提供了一种在C++下异步调用执行代码的能力——AsyncWorker

```c++
class PyCallWorker: public Napi::AsyncWorker {
    public:
        PyCallWorker(Napi::Function &callback,
                     PyObject *pCallable, PyObject *pArgs, PyObject *pKwargs)
            : Napi::AsyncWorker(callback), _pCallable(pCallable), _pArgs(pArgs), _pKwargs(pKwargs) {}

        ~PyCallWorker() {
            Py_DECREF(_pCallable);
            Py_XDECREF(_pArgs);
            Py_XDECREF(_pKwargs);
            Py_XDECREF(pRet);
        }

        // 这段代码会在NodeJS的Worker Thread中执行
        void Execute() override {
            pRet = PyObject_Call(_pCallable, _pArgs, _pKwargs);
        }

    	// 这里会回到主线程, 并且调用回调函数
        void OnOK() override {
            Napi::HandleScope scope(Env());
            result = pyobject_to_napi_value(Env(), pRet);
            Callback().Call({result});
        }

    private:
        PyObject *_pCallable, *_pArgs, *_pKwargs, *pRet;
        Napi::Value result;
};
    
// 阻塞的调用方式
pRet = PyObject_Call(pCallable, pArgs, pKwargs)

// 异步的调用方式, callback为回调函数
PyCallWorker *wk = new PyCallWorker(callback, pCallable, pArgs, pKwargs);
wk->Queue();
```

经过这样的改造，只要调用Python的时候传入callback函数，就不会阻塞NodeJS的执行了。



### 3.6 Python GILs

经过上述改造，尽管单个Python代码不会再阻塞NodeJS代码，但是同时执行2个以上的Python代码经常会Crash。这是因为Python代码执行时需要获取一个全局锁GIL，这个GIL是为了防止Python代码在多线程执行时对同一处资源进行同时的修改。

在嵌入式开发中，Py_Intialize方法会在主线程中获取GIL, 只要代码都在主线程执行, 或者主线程调用了Python代码来启动新线程，都不会有问题，Python会自动处理GIL的事情。

但是，因为我们用了AsyncWorker，这是一个C++的原生线程, 在那个线程里面没有获取GIL直接操作Python的C-API就有一定几率会造成Crash。解决方式是对我们的调用方式进行改造：

- _init_python方法中, 初始化以后, 调用PyEval_SaveThread释放主线程的GIL
- 对所有后续涉及Python C-API调用的函数
  - 在开始执行时都通过PyEval_RestoreThread方法来获取GIL
  - 执行完立即通过PyEval_SaveThread方法来释放GIL
- AsyncWorker中每个调用了Python C-APi的函数也是类似，开始时PyEval_RestoreThread，结束时PyEval_SaveThread



### 3.7 Isolating Python Enviroments

动态语言的特性是可以任意[Money Patch](https://en.wikipedia.org/wiki/Monkey_patch)修改其他已载入的模块, 这样如果有一个插件Patch了系统Module, 其他插件的使用也会被影响。这时候我们就需要有一个沙盒机制，把不同的模块隔离开来。

Python自己没有提供这个机制，但是Python的C-API有一个subinterpreter的概念，通过它我们可以实现真资源隔离——因为不同的subinterpreter导入的模块都不互通。

subinterpreter其实是没有官方文档的，关于这个机制讲得最好的是这一篇： [Python multi-thread multi-interpreter C API](https://stackoverflow.com/questions/26061298/python-multi-thread-multi-interpreter-c-api)

**创建一个Context**（subinterpreter）

```c++
Napi::Value _create_pycontext(const Napi::CallbackInfo &info) {
	// ...
    
    // 获取一下主线程的GIL
    PyEval_RestoreThread(py_mainstate);

    // 把当前线程且为NULL
    oldstate = PyThreadState_Swap(NULL);
    
    // 创建一个新的subinterpreter, 保存一下state
    substate = Py_NewInterpreter();
    
    // 像保存PyObject一样, 我们也存一份PyContext(其实就是那个substate)
    context = serialize_pycontext(env, substate);

    // 切回主解释器的state
    PyThreadState_Swap(oldstate);

    // 释放GIL
    PyEval_SaveThread();
}
```

**在主线程中**，如果要获取GIL，需要区分一下当前的context，释放的方式不变

```c++
// 获取GIL的时候, 区分一下Context
substate = pycontext_get(context, "state");
if (substate != NULL) {
    PyEval_RestoreThread(substate);
} else {
    PyEval_RestoreThread(py_mainstate);
}
```

**在AsyncWorker线程中**，因为是不同的线程，所以需要根据state->interpreter来创建一个新的state, 然后再用它获取GIL

```c++
// 在C++子线程中获取GIL
void AcquireGIL(PyThreadState *state, PyThreadState **ts) {
    if (state == NULL)
        *ts = PyThreadState_New(py_mainstate->interp);
    else {
        *ts = PyThreadState_New(state->interp);
    }
    PyEval_RestoreThread(*ts);
}

// 在C++子线程中释放GIL
void ReleaseGIL(PyThreadState *state, PyThreadState **ts) {
    PyThreadState_Clear(*ts);
    PyThreadState_DeleteCurrent();
}
```

通过以上的方式，我们实现了一个超级沙盒，所有的模块都可以在不同的subinterpreter中得到执行。

另外，还有一个额外的好处是，Python3.10马上要引入去除GIL的多解释器真多线程，[PEP 554 -- Multiple Interpreters in the Stdlib](https://www.python.org/dev/peps/pep-0554/), 等到这个PEP实施完毕, 用我们这种方式隔离的不同模块代码就自动获得了真-多线程的能力。

BTW，其实Python3.8以后`_xxsubinterpreters`库已经在标准库里面了，但是一直没有具备真-多线程的能力。



### 3.8 Cyclic Objects

循环引用的Dict/List是一种比较少见, 但是有时候又必不可少的数据结构，在NodeJS中，我们可以这样来创造一个Cyclic Object

```javascript
> var d = {}
> d.a = d
{ a: [Circular] }
```

但这个东西是没法序列化的

```javascript
> JSON.stringify(d)
Uncaught TypeError: Converting circular structure to JSON
    --> starting at object with constructor 'Object'
    --- property 'a' closes the circle
    at JSON.stringify (<anonymous>)
```

传统的RPC通讯方式对这种对象几乎是无解的，除非花很大力气来重写序列化。这既不通用，又不经济(耗时不小)。

所以大部分框架——如果不是全部的话——都没有做解循环引用的操作。

不过既然我们是直接做PyObject到napi value的互相转换，只要NodeJS里面支持循环引用，Python中也支持，那么我们直接转一下就行了，只要在dict/list类型中多做一步处理如下

```c++
// auto cref = std::map<Napi::Value*, PyObject*>();

if (PyList_Check(object)) {
    if (cref.find(object) == cref.end()) {
        // 第一次遇到的对象，展开
        length = PyList_Size(object);
        arr = Napi::Array::New(env, size_t(length));
        cref[object] = (Napi::Array *)&arr;
        for (i = 0; i < length; i++) {
            item = pyobject_to_napi_value(env, PyList_GetItem(object, i), cref);
            arr.Set(uint32_t(i), item);
        }
        result = arr;
    } else {
        // 第二次遇到的对象, 返回之前保存的引用, 如果展开那就死循环了
        result = *cref.find(object)->second;
    }
}
```



## 4. TypeScript APIs

APIs are for human. -- Kenneth Reitz (Author of `Requests`)

API Design is so hard. Double *hard* if you try to achieve a consistent *API* across languages while still being idiomatic in each language. -- Armin Ronacher (Author of `Flask`)

API Design is UI Design. -- Me and many others

我们已经有了C++封装的Node插件，Everything Works，但是对使用者来说并不友好——我们想设计出好的API，它用起来如丝般润滑，就像你第一眼看到Python的`requests`库，或者`flask`库一般——你再也不想用`urllib`了，也不想用`django`,`tornado`。

一个好用顺手的库，你用它的时候是一种享受；而糟糕的API设计，则会让你一边用一边跺脚骂街

如何设计一个好的API？

1）作为使用者，先设想我们要怎么使用这个功能，需要有哪些API，而不是怎么实现它

2）在设计出来后，尝试(假装)使用这个API，写点代码，像用户使用UI一样使用这个API，发现不足之处

3）迭代这个API，直到API进化到符合我们的审美

直接在C/C++层封装一个优秀的API太累了，我们用C/C++实现基本功能后，再用TypeScript来封装API，在我看来是比较合适的选择。

### 4.1 PyWrapper and unwrap

我们想要在NodeJS中更为方便地使用Python，比如按下面这样做让我觉得最为自然而又简洁

```typescript
import { Python } from 'python.ts'
let py = new Python()
let os = py.import("os")
console.log(os.listdir())
```

那么，为了实现这个API

1）Python必须是一个Class

2）Python必须实现import方法，这个方法参数为Python的Module名称，返回一个Unwrapped对象

3）Unwrapped对象是PyWrapper对象的解包，拉取了PyObject上的所有的attribute和method(and arguments)，可以直接在NodeJS中进行调用

4）PyWrapper对象则是包含的原始Python指针的那个对象，含有一个unwrap方法，unwrap即3）的解包操作

那么实现这个设计也就顺其自然就好了，注意为了实现第三步，我们给clib中添加了`_dir`方法来获取PyObject的全部属性和方法

```typescript
class Python {

  // ...
   
    
  public import (name: string): Unwrapped {
    const result = clib._import_module(name, this.context)
    if (this.isPyWapper(result)) {
      return this.unwrap(result)
    }
  }

  // 把Python下面的method和attr都导入进来
  public unwrap (object: PyWrapper): Unwrapped {
    const unwrapped = {
      __wrapper__: object,
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
      } else {
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
  
  // ...
}
```



### 4.2 eval and exec

如果我们想直接在NodeJS中执行Python代码块，那么引入Python的eval和exec方法会让大家都方便很多

```typescript
py.exec(`
import math
def add(a, b):
    return math.pi * (a + b)`)

let res = py.eval(`add(1, 1)`)

// 有时候想做异步调用
let res = await py.eval_async(`add(1, 2)`)
```

那根据需求也可以实现TypeScript如下，并在clib中添加`_exec`和`_eval`方法

```typescript
  // 调用Python下的exec, code可以是任意合法代码段, 不会返回任何有意义的值
  public exec (code: string): PyWrapper | Primitive {
    return clib._exec(code, this.context)
  }

  // 异步调用exec
  public async exec_async (code: string): Promise<PyWrapper | Primitive> {
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
    return clib._eval(code, this.context)
  }

  // 异步调用eval
  public async eval_async (code: string, context?: PyWrapper): Promise<PyWrapper | Primitive> {
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
```



### 4.3 references and contexts

调用Python所产生的临时对象，都存放在clib.references中，手动清理会很麻烦，但是好在我们是支持上下文隔离的，那么能不能在销毁上下文的时候直接回收所有的对象呢?

```typescript
let py1 = new Python({ context: true })

// py1的各种操作

// 关闭py1上下文，并且回收全部资源
py1.delete()
```

Here we go! `clib._delete_pyobject`，`clib._delete_pycontext`配合着实现起！

```typescript
  public clear (): void {
    for (const ref of clib.references) {
      if (ref as boolean && this.context.state === ref.state) {
        clib._delete_pyobject(ref, this.context)
      }
    }
  }
  
  // 直接把Context都关了, 同时会清理Context下的所有PyObject对象
  public delete (): boolean {
    this.clear()
    if (this.context as boolean) {
      this.is_deleted = clib._delete_pycontext(this.context)
    }
    return this.is_deleted
  }
```



## 5. Conclusions and Futures

综上，我们实现了一个据我所知最为Feature Complete，也最为User Friendly的在NodeJS中调用Python程序的插件。

对这个插件进行的性能测试表明，该插件可以在空循环中进行200k/s的调用，速度是普通RPC方式实现插件的40倍，达成了设计预期。

还没想好还有哪些可以改进的点，就先把之前开发期间整理的TODO列表放在最后吧。



## 6. TODOs

- [x] APIs
  - [x] python
    - [x] class `Python`
    - [x] `py.import`
    - [x] `py.call`
    - [x] `py.unwrap`
    - [x] `py.reload`
    - [x] `py.exec`
    - [x] `py.eval`
    - [x] `py.call_async`
    - [x] `py.exec_async`
    - [x] `py.eval_async`
  - [x] clib
    - [x] `references`
    - [x] `contexts`
    - [x] `_set_runtime_path`
    - [x] `_set_plugins_path`
    - [x] `_init_python`
    - [x] `_destroy_python`
    - [x] `_import_module`
    - [x] `_reload_module`
    - [x] `_call_python`
    - [x] `_dir`
    - [x] `_exec`
    - [x] `_eval`
    - [x] `_delete_pyobject`
    - [x] `_create_pycontext`
    - [x] `_delete_pycontext`
    - [x] Clib Internals
      - [x] `str_to_uint64`
      - [x] `uint64_to_str`
      - [x] `is_pyobject`
      - [x] `is_pycontext`
      - [x] `serialize_pyobject`
      - [x] `deserialize_pyobject`
      - [x] `serialize_pycontext`
      - [x] `deserialize_pycontext`
      - [x] `napi_parse_json`
      - [x] `napi_dump_json`
      - [x] `napi_number_is_int`
      - [x] `napi_object_is_empty`
      - [x] `napi_value_to_pyobject`
      - [x] `pyobject_to_napi_value`
      - [x] `throw_pyexception_in_javascript`
- [x] Debugging output
- [x] Benchs and Tests
- [x] Threads and Async Callback
  - [x] `PyCallWorker`
  - [x] `PyRunWorker`
  - [ ] `ThreadPoolExecutor` with `xxsubinterpreters`
- [x] Cross Platform
  - [x] MacOS
  - [x] Linux
  - [x] Windows
- [x] Python Versions
  - [x] 3.9
  - [x] 3.8
  - [x] 3.7
  - [x] 3.6
- [ ] Architecture
  - [x] amd64
  - [ ] x86
  - [ ] arm64
  - [ ] arm32



## References

- Node Addon
  - nodejs n-api: [Index \| Node.js v14.15.1 Documentation](https://nodejs.org/dist/latest-v14.x/docs/api/)
  - node-addon-api: [GitHub - nodejs/node-addon-api: Module for using N-API from C++](https://github.com/nodejs/node-addon-api)
- Python:
  - [Python/C API Reference Manual — Python 3.9.0 documentation](https://docs.python.org/3/c-api/index.html)
- Python subinterpreters
  - [multithreading - Python multi-thread multi-interpreter C API - Stack Overflow](https://stackoverflow.com/questions/26061298/python-multi-thread-multi-interpreter-c-api)
  - [multithreading - Embedded Python: Multiple Sub-Interpreters not working - Stack Overflow](https://stackoverflow.com/questions/59666352/embedded-python-multiple-sub-interpreters-not-working)
  - [Embedding multiple Python sub-interpreters into a C program - Stack Overflow](https://stackoverflow.com/questions/53965865/embedding-multiple-python-sub-interpreters-into-a-c-program)
  - `cpython/Modules/_xxsubinterpreters`
