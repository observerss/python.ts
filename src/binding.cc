#include <string>
#include <sstream>
#include <mutex>
#include <ctime>
#include <map>
#include <set>
#include <assert.h>
#include <napi.h>
#include <Python.h>

const int MAX_CODE_SIZE = 1024;
const char *PYOBJECT_WRAPPER = "python-ts/PyObject*";
const char *PYTHREADSTATE_WRAPPER = "python-ts/PyThreadState*";
const char *OBJECTS = "python-ts/objects";
const char *CONTEXTS = "python-ts/contexts";
const char *REFERENCES = "python-ts/references";

std::string runtime_path = "x64";
wchar_t *py_program = NULL;
PyObject *py_main = NULL;
PyThreadState *py_mainstate = NULL;
PyGILState_STATE gstate;
uint32_t rtop = 0, ctop = 0;
std::mutex mutex, smutex;
bool debug = false;

// 回收Python环境
Napi::Boolean __destroy_python(const Napi::Env &env)
{
    if (py_program)
    {
        mutex.lock();
        PyEval_RestoreThread(py_mainstate);
        if (Py_FinalizeEx() < 0)
        {
            mutex.unlock();
            Napi::Error::New(env, "Failed to destroy python!").ThrowAsJavaScriptException();
            return Napi::Boolean::New(env, false);
        }
        PyMem_RawFree(py_program);
        py_program = NULL;
        py_mainstate = NULL;
        mutex.unlock();
    }
    return Napi::Boolean::New(env, true);
}
Napi::Boolean _destroy_python(const Napi::CallbackInfo &info)
{
    return __destroy_python(info.Env());
}

bool __prepare_python_env(const Napi::Env &env, bool is_main)
{
    char single_code[MAX_CODE_SIZE];

    PyRun_SimpleString("import sys");
    // 一定要把runtime的目录先加载，否则Python自带的包都无法加载
    // runtime目录就是那个有python38._pth, python38.dll, python3.dll的目录
    //    sprintf(single_code, "sys.path.insert(0, '''%s''')", runtime_path.c_str());
    //    PyRun_SimpleString(single_code);

    if (PyImport_ImportModule("os") == NULL)
    {
        // os模块如果没找到的话肯定是路径不对，报错提示一下
        __destroy_python(env);
        Napi::Error::New(env, "Unable to find `os` module, \
provide valid directory via `python-ts._set_runtime_path`!")
            .ThrowAsJavaScriptException();
        return false;
    }

    PyRun_SimpleString("import os");

    if (is_main)
    {
        // multiprocessing的path要搞对, 否则没法起进程
        PyRun_SimpleString("import multiprocessing, platform");
        sprintf(single_code,
                "if platform.system().lower() == 'windows':\n"
                "    multiprocessing.set_executable(r'''%s'''+'/python.exe')",
                runtime_path.c_str());
        PyRun_SimpleString(single_code);
        sprintf(single_code,
                "import os, subprocess\n"
                "python3_prefix = subprocess.run('python3-config --prefix',\n"
                "    shell=True, capture_output=True, text=True).stdout.strip()\n"
                "executable = os.path.join(python3_prefix, 'bin', 'python3')\n"
                "if platform.system().lower() == 'darwin':\n"
                "    multiprocessing.set_executable(executable)");
        PyRun_SimpleString(single_code);
    }

    return true;
}

// 初始化Python环境
Napi::Boolean __init_python(const Napi::Env &env)
{

    if (!py_program)
    {
        mutex.lock();

        Py_Initialize(); // Python3.7以后隐含着调用PyEval_InitThreads; 3.7以前的话要手动调用PyEval_InitThreads

        py_program = Py_GetProgramName();
        py_main = PyImport_AddModule("__main__"); // 添加一下__main__, 以后exec和eval的globals/locals都挂靠在这里

        if (!__prepare_python_env(env, true))
        {
            return Napi::Boolean::New(env, false);
        }

        // 初始化以后就可以各种RestoreThread来拿权限了
        py_mainstate = PyEval_SaveThread();
        mutex.unlock();
    }
    return Napi::Boolean::New(env, true);
}

Napi::Boolean _init_python(const Napi::CallbackInfo &info)
{
    return __init_python(info.Env());
}

uintptr_t str_to_uintptr(const std::string &value)
{
    std::istringstream iss(value);
    uintptr_t result;
    iss >> result;
    return result;
}

std::string uintptr_to_str(const uintptr_t &value)
{
    std::stringstream stream;
    stream << value;
    return stream.str();
}

inline bool is_pyobject(const Napi::Value &value)
{
    Napi::Value type;
    Napi::Object object;
    if (!value.IsObject())
        return false;
    object = value.As<Napi::Object>();
    if (object.Has("__wrapper__"))
        return is_pyobject(object.Get("__wrapper__"));
    type = object.Get("type");
    if (!type.IsString())
        return false;
    return type.As<Napi::String>().Utf8Value() == PYOBJECT_WRAPPER;
}

inline bool is_pycontext(const Napi::Value &value)
{
    Napi::Value type;
    if (!value.IsObject())
        return false;
    type = value.As<Napi::Object>().Get("type");
    if (!type.IsString())
        return false;
    return type.As<Napi::String>().Utf8Value() == PYTHREADSTATE_WRAPPER;
}

Napi::Object serialize_pyobject(const Napi::Env &env, PyObject *object, PyThreadState *state)
{
    Napi::Object result, objects = env.Global().Get(OBJECTS).As<Napi::Object>();
    Napi::Array references = env.Global().Get(REFERENCES).As<Napi::Array>();
    PyObject *repr;
    std::string value = uintptr_to_str((uintptr_t)object);

    if (objects.Has(value))
    {
        return objects.Get(value).As<Napi::Object>();
    }

    result = Napi::Object::New(env);
    repr = PyObject_Repr(object);

    result.Set("type", PYOBJECT_WRAPPER);
    result.Set("value", value);
    Py_INCREF(object); // 已经序列化过的对象手动加一个reference，避免被回收
    if (repr != NULL)
    {
        result.Set("repr", PyUnicode_AsUTF8(repr));
    }
    else
    {
        result.Set("repr", env.Null());
    }
    Py_XDECREF(repr);
    result.Set("pytype", Py_TYPE(object)->tp_name);
    result.Set("time", Napi::Number::New(env, (double)std::time(0)));
    if (state == NULL)
    {
        result.Set("state", env.Undefined());
    }
    else
    {
        result.Set("state", uintptr_to_str((uintptr_t)state));
    }

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

PyObject *deserialize_pyobject(const Napi::Env &env, const Napi::Object &object)
{
    Napi::String value;
    Napi::Object objects = env.Global().Get(OBJECTS).As<Napi::Array>();

    if (!is_pyobject(object))
    {
        return NULL;
    }

    if (object.Has("__wrapper__"))
    {
        value = object.Get("__wrapper__").As<Napi::Object>().Get("value").As<Napi::String>();
    }
    else
    {
        value = object.Get("value").As<Napi::String>();
    }

    if (!objects.Has(value))
    {
        // 已经被主动回收的对象不能再用了
        return NULL;
    }

    return (PyObject *)str_to_uintptr(value.Utf8Value());
}

Napi::Object serialize_pycontext(const Napi::Env &env, PyThreadState *state)
{
    Napi::Object result, objects = env.Global().Get(OBJECTS).As<Napi::Object>();
    Napi::Array contexts = env.Global().Get(CONTEXTS).As<Napi::Array>();
    PyObject *main;
    std::string jstate = uintptr_to_str((uintptr_t)state);

    if (objects.Has(jstate))
    {
        return objects.Get(jstate).As<Napi::Object>();
    }

    main = PyImport_AddModule("__main__");

    result = Napi::Object::New(env);

    result.Set("type", PYTHREADSTATE_WRAPPER);
    result.Set("state", jstate);
    result.Set("main", uintptr_to_str((uintptr_t)main));
    result.Set("time", Napi::Number::New(env, (double)std::time(0)));

    smutex.lock();
    result.Set("index", ctop);
    contexts.Set(ctop, result);
    ctop++;
    smutex.unlock();

    // 跟踪对象以便于复用
    objects.Set(jstate, result);
    return result;
}

PyThreadState *pycontext_get(const Napi::Object &object, const char *key)
{
    if (!is_pycontext(object))
    {
        return NULL;
    }

    return (PyThreadState *)str_to_uintptr(object.Get(key).As<Napi::String>().Utf8Value());
}

// 暂时没用，但未来可能要用
Napi::Object napi_parse_json(const Napi::Env &env, const Napi::String &json_string)
{
    Napi::Object json = env.Global().Get("JSON").As<Napi::Object>();
    Napi::Function parse = json.Get("parse").As<Napi::Function>();
    return parse.Call(json, {json_string}).As<Napi::Object>();
}

// 暂时没用，但未来可能要用
Napi::String napi_dump_json(const Napi::Env &env, const Napi::Object &json_object)
{
    Napi::Object json = env.Global().Get("JSON").As<Napi::Object>();
    Napi::Function stringify = json.Get("stringify").As<Napi::Function>();
    return stringify.Call(json, {json_object}).As<Napi::String>();
}

bool napi_number_is_int(const Napi::Env &env, const Napi::Value &num)
{
    return env.Global()
        .Get("Number")
        .ToObject()
        .Get("isInteger")
        .As<Napi::Function>()
        .Call({num})
        .ToBoolean()
        .Value();
}

bool napi_object_is_empty(Napi::Env &env, const Napi::Object &obj)
{
    Napi::Array keys = obj.GetPropertyNames();
    if (env.IsExceptionPending())
    {
        env.GetAndClearPendingException();
        return false;
    }
    return keys.Length() == 0;
}

bool __in_same_context(const Napi::Env &env, const Napi::Value &value, Napi::Object &context, std::set<Napi::Value *> &cref)
{
    Napi::Value item;
    Napi::Object obj;
    Napi::Array arr, keys;
    uint32_t i;

    if (value.IsBuffer()) {
        // 有些类型需要单独处理一下，否则会FATAL ERROR
        return true;
    }
    else if (value.IsArray())
    {
        arr = value.As<Napi::Array>();
        if (cref.insert(&arr).second)
        {
            for (i = 0; i < arr.Length(); i++)
            {
                if (!__in_same_context(env, arr.Get(i), context, cref))
                    return false;
            }
        }
    }
    else if (value.IsObject())
    {
        obj = value.As<Napi::Object>();
        if (obj.Has("__wrapper__"))
        {
            obj = obj.Get("__wrapper__").As<Napi::Object>();
        }
        if (cref.insert(&obj).second)
        {

            if (debug)
                printf("obj.state %s, context.state %s\n",
                       obj.Get("state").ToString().Utf8Value().c_str(),
                       context.Get("state").ToString().Utf8Value().c_str());
            if (obj.Has("state"))
                return obj.Get("state") == context.Get("state");

            for (i = 0; i < keys.Length(); i++)
            {
                if (obj.HasOwnProperty(keys.Get(i)))
                {
                    if (!__in_same_context(env, obj.Get(keys.Get(i)), context, cref))
                        return false;
                }
            }
        }
    }

    return true;
}

bool in_same_context(const Napi::Env &env, Napi::Value &value, Napi::Object &context)
{
    std::set<Napi::Value *> cref = std::set<Napi::Value *>();
    return __in_same_context(env, value, context, cref);
}

// cref这个参数是为了消解循环引用, 循环引用的对象将不在展开
PyObject *__napi_value_to_pyobject(Napi::Env &env, Napi::Value &value,
                                   std::map<Napi::Value *, PyObject *> &cref)
{
    PyObject *pResult, *pItem;
    Py_ssize_t i;
    Napi::Array arr, keys;
    Napi::Object obj;
    Napi::Value item;

    if (value.IsBoolean())
    {
        // boolean
        return Py_BuildValue("p", value.As<Napi::Boolean>().Value());
    }
    else if (value.IsNull() || value.IsUndefined())
    {
        // null or undefined => None
        return Py_BuildValue("s", NULL);
    }
    else if (value.IsNumber())
    {
        // number => (int, float)
        if (debug)
            printf("Napi::Number! %s\n", value.ToString().Utf8Value().c_str());
        if (napi_number_is_int(env, value))
        {
            pResult = PyLong_FromDouble(value.As<Napi::Number>().DoubleValue());
        }
        else
        {
            pResult = Py_BuildValue("d", value.As<Napi::Number>().DoubleValue());
        }
        return pResult;
    }
    else if (value.IsString())
    {
        // string => string
        if (debug)
            printf("String! %s\n", value.As<Napi::String>().Utf8Value().c_str());
        return Py_BuildValue("s", value.As<Napi::String>().Utf8Value().c_str());
    }
    else if (value.IsBuffer())
    {
        // buffer => bytes
        Napi::Buffer<char> buffer = value.As<Napi::Buffer<char>>();
        if (debug)
            printf("Buffer! %s\n", value.As<Napi::String>().Utf8Value().c_str());
        return Py_BuildValue("y#", (char*)(buffer.Data()), buffer.Length());
    }
    else if (value.IsArray())
    {
        if (debug)
            printf("Napi::Array! Length=%d\n", arr.Length());
        if (cref.find(&value) == cref.end())
        {
            // 第一次遇到, 正常展开
            arr = value.As<Napi::Array>();
            pResult = PyList_New(0);
            cref[&value] = pResult;
            for (i = 0; i < arr.Length(); i++)
            {
                item = arr.Get(i);
                pItem = __napi_value_to_pyobject(env, item, cref);
                PyList_Append(pResult, pItem);
            }
            return pResult;
        }
        else
        {
            // 第二次遇到, 返回之前保存的引用, 展开会死循环
            pResult = cref.find(&value)->second;
            return pResult;
        }
    }
    else if (value.IsObject())
    {
        if (debug)
            printf("Napi::Object!\n");
        if (cref.find(&value) == cref.end())
        {
            // 第一次遇到, 展开
            obj = value.As<Napi::Object>();
            if (is_pyobject(obj))
            {
                // 无法deserialize的时候，返回相应信息, 至少别出Fatal Error
                pResult = deserialize_pyobject(env, obj);
                if (pResult == NULL)
                {
                    pResult = Py_BuildValue("s", "RuntimeError('object has been recycled')");
                }
            }
            else
            {
                pResult = PyDict_New();
                cref[&value] = pResult;
                if (!napi_object_is_empty(env, obj))
                {
                    keys = obj.GetPropertyNames();
                    for (i = 0; i < keys.Length(); i++)
                    {
                        if (obj.HasOwnProperty(keys.Get(i)))
                        {
                            item = obj.Get(keys.Get(i));
                            pItem = __napi_value_to_pyobject(env, item, cref);
                            PyDict_SetItemString(pResult, keys.Get(i).As<Napi::String>().Utf8Value().c_str(), pItem);
                        }
                    }
                }
            }
            return pResult;
        }
        else
        {
            // 第二次遇到, 返回保存的引用, 展开会死循环
            pResult = cref.find(&value)->second;
            return pResult;
        }
    }
    else
    {
        // 不支持转换的类型
        // IsEmpty, IsExternal
        // IsDate, IsFunction, IsPromise, IsSymbol
        // IsBuffer, IsArrayBuffer, IsDataView, IsTypedArray
        // 其实也可以实现一下，但是好像也不是特别有必要干这事
        return Py_BuildValue("s", value.ToString().Utf8Value().c_str());
    }
}

PyObject *napi_value_to_pyobject(Napi::Env &env, Napi::Value &value)
{
    std::map<Napi::Value *, PyObject *> cref = std::map<Napi::Value *, PyObject *>();
    return __napi_value_to_pyobject(env, value, cref);
}

// cref这个参数是为了消解循环引用, 循环引用的对象将不在展开
Napi::Value __pyobject_to_napi_value(const Napi::Env &env, PyObject *object, PyThreadState *state,
                                     std::map<PyObject *, Napi::Value *> &cref)
{
    Py_ssize_t i, length;
    PyObject *keys, *values, *pIterator, *pItem;
    Napi::Value item, result, key;
    Napi::Object obj;
    Napi::Array arr;

    if (object == NULL || object == Py_None)
    {
        result = env.Null();
    }
    else if (PyLong_Check(object))
    {
        result = Napi::Number::New(env, PyLong_AsDouble(object));
    }
    else if (PyBool_Check(object))
    {
        result = object == Py_True ? Napi::Boolean::New(env, true) : Napi::Boolean::New(env, false);
    }
    else if (PyFloat_Check(object))
    {
        result = Napi::Number::New(env, PyFloat_AsDouble(object));
    }
    else if (PyUnicode_Check(object))
    {
        if (debug)
            printf("PyUnicode, %s\n", PyUnicode_AsUTF8(object));
        result = Napi::String::New(env, PyUnicode_AsUTF8(object));
    }
    else if (PyBytes_Check(object))
    {
        if (debug)
            printf("PyBytes, %s\n", PyBytes_AsString(object));
        result = Napi::Buffer<char>::Copy(env, PyBytes_AsString(object), PyBytes_Size(object));
    }
    else if (PyTuple_Check(object))
    {
        length = PyTuple_Size(object);
        arr = Napi::Array::New(env, size_t(length));
        for (i = 0; i < length; i++)
        {
            item = __pyobject_to_napi_value(env, PyTuple_GetItem(object, i), state, cref);
            arr.Set(uint32_t(i), item);
        }
        result = arr;
    }
    else if (PyList_Check(object))
    {
        if (debug)
            printf("PyList\n");
        if (cref.find(object) == cref.end())
        {
            // 第一次遇到的对象，展开
            length = PyList_Size(object);
            arr = Napi::Array::New(env, size_t(length));
            cref[object] = (Napi::Array *)&arr;
            for (i = 0; i < length; i++)
            {
                item = __pyobject_to_napi_value(env, PyList_GetItem(object, i), state, cref);
                arr.Set(uint32_t(i), item);
            }
            result = arr;
        }
        else
        {
            // 第二次遇到的对象, 返回之前保存的引用, 如果展开那就死循环了
            result = *cref.find(object)->second;
        }
    }
    else if (PyDict_Check(object))
    {
        if (cref.find(object) == cref.end())
        {
            // 第一次遇到的对象，展开
            length = PyDict_Size(object);
            obj = Napi::Object::New(env);
            cref[object] = (Napi::Array *)&obj;
            keys = PyDict_Keys(object);
            values = PyDict_Values(object);
            for (i = 0; i < length; i++)
            {
                key = __pyobject_to_napi_value(env, PyList_GetItem(keys, i), state, cref);
                if (!key.IsString())
                {
                    // Python的dict可以接受非string类的key, 遇到这种情况就放弃展开, 返回PyWrapper
                    Py_DECREF(keys);
                    Py_DECREF(values);
                    result = serialize_pyobject(env, object, state);
                    return result;
                }
                else
                {
                    obj.Set(key, __pyobject_to_napi_value(env, PyList_GetItem(values, i), state, cref));
                }
            }
            Py_DECREF(keys);
            Py_DECREF(values);
            result = obj;
        }
        else
        {
            // 第二次遇到的对象, 返回之前保存的引用, 如果展开那就死循环了
            result = *cref.find(object)->second;
        }
    }
    else if (PySet_Check(object))
    {
        pIterator = PyObject_GetIter(object);
        arr = Napi::Array::New(env);
        i = 0;
        while ((pItem = PyIter_Next(pIterator)))
        {
            arr.Set(i++, __pyobject_to_napi_value(env, pItem, state, cref));
            Py_DECREF(pItem);
        }
        Py_DECREF(pIterator);
        result = arr;
    }
    else
    {
        // 找不到任何序列化方式了，只好存个指针
        // 这个指针对象永远不会被Py_DECREF, 除非由JS发起_delete_pyobject
        result = serialize_pyobject(env, object, state);
    }

    return result;
}

Napi::Value pyobject_to_napi_value(const Napi::Env &env, PyObject *object, PyThreadState *state)
{
    std::map<PyObject *, Napi::Value *> cref = std::map<PyObject *, Napi::Value *>();
    return __pyobject_to_napi_value(env, object, state, cref);
}

std::string get_pyexception(const Napi::Env &env, const char *error_title)
{
    PyObject *type, *value, *traceback, *tracebackModule, *output, *concat;
    PyObject *func, *args;
    std::string error = "";

    // 以防python还没初始化
    __init_python(env);

    if (error_title != NULL)
        error = error_title;

    PyErr_Fetch(&type, &value, &traceback);
    if (type != NULL)
    {
        // 如果确实存在Python的错误信息
        tracebackModule = PyImport_ImportModule("traceback");
        PyErr_NormalizeException(&type, &value, &traceback);
        if (traceback != NULL)
        {
            PyException_SetTraceback(value, traceback);
            args = PyTuple_Pack(3, type, value, traceback);
            func = PyObject_GetAttrString(tracebackModule, "format_exception");
        }
        else
        {
            args = PyTuple_Pack(2, type, value);
            func = PyObject_GetAttrString(tracebackModule, "format_exception_only");
        }
        output = PyObject_Call(func, args, NULL);
        concat = PyObject_CallMethod(Py_BuildValue("s", "  "), "join", "O", output);
        error += "\n  ";
        error += PyUnicode_AsUTF8(concat);

        PyErr_Clear();

        Py_XDECREF(type);
        Py_XDECREF(value);
        Py_XDECREF(traceback);
        Py_XDECREF(tracebackModule);
        Py_XDECREF(output);
        Py_XDECREF(concat);
        Py_XDECREF(func);
        Py_XDECREF(args);
    }

    return error;
}

// 把python当前的traceback作为js的错误扔出去
void throw_pyexception_in_javascript(const Napi::Env &env, const char *error_title)
{
    std::string error = get_pyexception(env, error_title);
    if (error.length() == 0)
    {
        error = "UnknownError";
    }

    Napi::Error::New(env, error).ThrowAsJavaScriptException();
}

void AcquireGIL(PyThreadState *state, PyThreadState **ts)
{
    if (state == NULL)
        *ts = PyThreadState_New(py_mainstate->interp);
    else
    {
        *ts = PyThreadState_New(state->interp);
    }
    PyEval_RestoreThread(*ts);
}

void ReleaseGIL(PyThreadState *state, PyThreadState **ts)
{
    PyThreadState_Clear(*ts);
    PyThreadState_DeleteCurrent();
}

// _call_python专用
class PyCallWorker : public Napi::AsyncWorker
{
public:
    PyCallWorker(Napi::Function &callback,
                 PyObject *pCallable, PyObject *pArgs, PyObject *pKwargs, PyThreadState *state)
        : Napi::AsyncWorker(callback), _pCallable(pCallable), _pArgs(pArgs), _pKwargs(pKwargs), _state(state) {}

    ~PyCallWorker()
    {
        AcquireGIL(_state, &ts);
        Py_DECREF(_pCallable);
        Py_XDECREF(_pArgs);
        Py_XDECREF(_pKwargs);
        Py_XDECREF(pRet);
        ReleaseGIL(_state, &ts);
    }

    // This code will be executed on the worker thread
    void Execute() override
    {
        AcquireGIL(_state, &ts);
        pRet = PyObject_Call(_pCallable, _pArgs, _pKwargs);
        ReleaseGIL(_state, &ts);
    }

    void OnOK() override
    {
        Napi::HandleScope scope(Env());
        AcquireGIL(_state, &ts);
        if (pRet == NULL)
        {
            result = Napi::String::New(Env(), get_pyexception(Env(), "python-ts.PyCallWorker failed"));
        }
        else
        {
            result = pyobject_to_napi_value(Env(), pRet, _state);
        }
        ReleaseGIL(_state, &ts);
        Callback().Call({result});
    }

private:
    PyObject *_pCallable, *_pArgs, *_pKwargs, *pRet;
    PyThreadState *_state, *ts;
    Napi::Value result;
};

// _exec/_eval专用
class PyRunWorker : public Napi::AsyncWorker
{
public:
    PyRunWorker(Napi::Function &callback,
                const char *code, int start, PyObject *pGlobals, PyObject *pLocals, PyThreadState *state)
        : Napi::AsyncWorker(callback), _pGlobals(pGlobals), _pLocals(pLocals), _state(state),
          _code(code), _start(start) {}

    ~PyRunWorker()
    {
        AcquireGIL(_state, &ts);
        Py_XDECREF(pRet);
        ReleaseGIL(_state, &ts);
    }

    // This code will be executed on the worker thread
    void Execute() override
    {
        AcquireGIL(_state, &ts);
        pRet = PyRun_String(_code.c_str(), _start, _pGlobals, _pLocals);
        ReleaseGIL(_state, &ts);
    }

    void OnOK() override
    {
        Napi::HandleScope scope(Env());
        AcquireGIL(_state, &ts);
        if (pRet == NULL)
        {
            result = Napi::String::New(Env(), get_pyexception(Env(), "python-ts.PyRunWorker failed"));
        }
        else
        {
            result = pyobject_to_napi_value(Env(), pRet, _state);
        }
        ReleaseGIL(_state, &ts);
        Callback().Call({result});
    }

private:
    PyObject *_pGlobals, *_pLocals, *pRet;
    PyThreadState *_state, *ts;
    Napi::Value result;
    std::string _code;
    int _start;
};

/* 设置python的runtime路径
_set_runtime(path)
参数:
    path: runtime目录, 就是有python38._pth, python3.dll, python38.dll的那个文件夹的目录
          不设置的话默认为当前目录的相对路径`x64/runtime`下。
          需要在_init_python之前设置。
返回
    true代表成功, false代表失败
*/
Napi::Boolean _set_runtime_path(const Napi::CallbackInfo &info)
{
    Napi::String path;
    Napi::Env env = info.Env();
    Napi::Boolean succeeded = Napi::Boolean::New(env, true), failed = Napi::Boolean::New(env, false);

    if (info.Length() < 1)
    {
        Napi::TypeError::New(env, "Please call with (path)").ThrowAsJavaScriptException();
        return failed;
    }
    if (!info[0].IsString())
    {
        Napi::TypeError::New(env, "Argument `path` should be a String").ThrowAsJavaScriptException();
        return failed;
    }

    path = info[0].As<Napi::String>();
    runtime_path = path.Utf8Value();
    if (debug)
        printf("runtime_path = %s\n", runtime_path.c_str());
    return succeeded;
}

Napi::Value _set_debug(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    Napi::Boolean value = Napi::Boolean::New(env, false);
    if (info.Length() >= 1 && info[0].IsBoolean())
    {
        value = info[0].As<Napi::Boolean>();
    }
    debug = value.Value();
    if (debug)
        printf("debug is on\n");
    return env.Null();
}

/*  载入模块，接受两个参数,
_import_module(module_name, context)
参数
    module_name: 模块名称, 可以有.在中间，比如"urllib.parse"
    context: 上下文引用，{"type": PYTHREADSTATE_WRAPPER}, 如不提供则不隔离
返回值
    如果成功, 返回JSON.stringify({"type": PYOBJECT_WRAPPER, "value": PyObject指针地址})
    如果失败, raise错误信息
*/
Napi::Object _import_module(const Napi::CallbackInfo &info)
{
    PyObject *pModule;
    Napi::String module_name;
    Napi::Env env = info.Env();
    Napi::Object result = Napi::Object::New(env), context = Napi::Object::New(env);
    PyThreadState *substate;

    // 参数检查并赋值给module_name
    if (info.Length() < 1)
    {
        Napi::TypeError::New(env, "Please call with (module_name, context)").ThrowAsJavaScriptException();
        return result;
    }
    if (!info[0].IsString())
    {
        Napi::TypeError::New(env, "Argument `module_name` should be a String").ThrowAsJavaScriptException();
        return result;
    }
    if (info.Length() >= 2)
    {
        if (!info[1].IsObject())
        {
            Napi::TypeError::New(env, "Argument `context` should be an Object")
                .ThrowAsJavaScriptException();
            return result;
        }
        context = info[1].As<Napi::Object>();
    }
    module_name = info[0].As<Napi::String>();

    // 以防大家不手动init python, 不管怎样都init一下
    __init_python(env);

    substate = pycontext_get(context, "state");
    if (substate != NULL)
    {
        PyEval_RestoreThread(substate);
    }
    else
    {
        PyEval_RestoreThread(py_mainstate);
    }

    // 真正的import部分
    pModule = PyImport_ImportModule(module_name.Utf8Value().c_str());
    if (pModule == NULL)
    {
        throw_pyexception_in_javascript(env, "python-ts._import_module failed");
    }
    else
    {
        result = serialize_pyobject(env, pModule, substate);
    }
    Py_XDECREF(pModule);

    PyEval_SaveThread();
    return result;
}

/* 重载模块
_reload_module(module_name, context)
参数
    module_name: 模块名称, 可以有.在中间，比如"urllib.parse"
    context: 上下文引用，{"type": PYTHREADSTATE_WRAPPER}, 如不提供则不隔离
返回值
    如果成功, 返回JSON.stringify({"type": PYOBJECT_WRAPPER, "value": PyObject指针地址})
    如果失败, raise错误信息
*/
Napi::Boolean _reload_module(const Napi::CallbackInfo &info)
{
    char single_code[MAX_CODE_SIZE];
    Napi::String module_name, module_path;
    Napi::Env env = info.Env();
    Napi::Boolean result, failed = Napi::Boolean::New(env, false), succeeded = Napi::Boolean::New(env, true);
    Napi::Object context = Napi::Object::New(env);
    PyThreadState *substate;
    int ret;

    // 参数检查并赋值给module_name
    if (info.Length() < 1)
    {
        Napi::TypeError::New(env, "Please call with (module_name, context)").ThrowAsJavaScriptException();
        return failed;
    }
    if (!info[0].IsString())
    {
        Napi::TypeError::New(env, "Argument `module_name` should be a String").ThrowAsJavaScriptException();
        return failed;
    }

    if (info.Length() >= 2)
    {
        if (!info[1].IsObject())
        {
            Napi::TypeError::New(env, "Argument `context` should be an Object")
                .ThrowAsJavaScriptException();
            return failed;
        }
        context = info[1].As<Napi::Object>();
    }

    module_name = info[0].As<Napi::String>();

    // 以防大家不手动init python, 不管怎样都init一下
    __init_python(env);

    substate = pycontext_get(context, "state");
    if (substate != NULL)
    {
        PyEval_RestoreThread(substate);
    }
    else
    {
        PyEval_RestoreThread(py_mainstate);
    }

    // Python Code for reload
    PyRun_SimpleString("import importlib");
    sprintf(single_code, "importlib.reload(sys.modules['''%s'''])", module_name.Utf8Value().c_str());
    ret = PyRun_SimpleString(single_code);
    if (ret != 0)
    {
        throw_pyexception_in_javascript(env, "python-ts._reload_module failed");
        result = failed;
    }
    else
    {
        result = succeeded;
    }

    PyEval_SaveThread();
    return result;
}

/* 调用模块下的函数
_call_python(object, attr, args, kwargs, context, callback)
参数
    object: Python对象, {"type": PYOBJECT_WRAPPER, "value": 0x12341234}
    attr: 函数/类名称, "string"
    args: 参数列表array, 可选，默认为[]
    kwargs: 参数字典array, 可选，默认为{}
    context: 上下文引用，{"type": PYTHREADSTATE_WRAPPER}, 如不提供则不隔离
    callback: 如果提供callback函数，则用AsyncWorker异步回调返回，否则同步返回
返回值
    如果可以dump成json的话, python dump一下再parse_json一下，最终返回一个Object
    如果不行的话, 返回{"type": PYOBJECT_WRAPPER, "value": PyObject指针地址}
错误处理
    如果出错, traceback.format_exc将会被napi_throw_error出来
*/
Napi::Value _call_python(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    Napi::String attr;
    Napi::Array args, keys;
    Napi::Object object, kwargs, context;
    Napi::Value result = env.Null();
    Napi::Function callback;
    PyObject *pObject, *pCallable, *pRet, *pArgs_, *pArgs, *pKwargs;
    PyThreadState *substate;
    PyCallWorker *wk;
    bool has_callback = false;

    // 初始化参数
    if (info.Length() < 2)
    {
        Napi::TypeError::New(
            env,
            "Please call with (object, attr, args?=[], kwargs?={}, context?={}, callback?=()=>{})")
            .ThrowAsJavaScriptException();
        return result;
    }
    if (!info[0].IsObject())
    {
        Napi::TypeError::New(env, "Argument `module` should be an Object")
            .ThrowAsJavaScriptException();
        return result;
    }
    if (!info[1].IsString())
    {
        Napi::TypeError::New(env, "Argument `attr` should be a String")
            .ThrowAsJavaScriptException();
        return result;
    }

    object = info[0].As<Napi::Object>();
    attr = info[1].As<Napi::String>();
    args = Napi::Array::New(env);
    kwargs = Napi::Object::New(env);
    context = Napi::Object::New(env);

    if (info.Length() >= 3)
    {
        if (!info[2].IsArray())
        {
            Napi::TypeError::New(env, "Argument `args` should be an Array")
                .ThrowAsJavaScriptException();
            return result;
        }
        args = info[2].As<Napi::Array>();
    }

    if (info.Length() >= 4)
    {
        if (!info[3].IsObject())
        {
            Napi::TypeError::New(env, "Argument `kwargs` should be an Object")
                .ThrowAsJavaScriptException();
            return result;
        }
        kwargs = info[3].As<Napi::Object>();
    }

    if (info.Length() >= 5)
    {
        if (!info[4].IsObject())
        {
            Napi::TypeError::New(env, "Argument `context` should be an Object")
                .ThrowAsJavaScriptException();
            return result;
        }
        context = info[4].As<Napi::Object>();
    }

    if (info.Length() >= 6)
    {
        if (!info[5].IsFunction())
        {
            Napi::TypeError::New(env, "Argument `callback` should be an Function")
                .ThrowAsJavaScriptException();
            return result;
        }
        has_callback = true;
        callback = info[5].As<Napi::Function>();
    }
    // 参数初始化完毕 T.T

    pObject = deserialize_pyobject(env, object);
    if (pObject == NULL)
    {
        Napi::TypeError::New(env, "Argument `object` should be a <python-ts/PyObject*> object or `object` recycled!")
            .ThrowAsJavaScriptException();
        return result;
    }

    if ((object.Get("state") != context.Get("state") && !in_same_context(env, object, context)) || (args.Length() > 0 && !in_same_context(env, args, context)) || (!napi_object_is_empty(env, kwargs) && !in_same_context(env, kwargs, context)))
    {
        Napi::TypeError::New(env, "Cannot call object/args/kwargs from different context!")
            .ThrowAsJavaScriptException();
        return result;
    }

    // 以防Python没有初始化
    __init_python(env);

    substate = pycontext_get(context, "state");
    if (substate != NULL)
    {
        PyEval_RestoreThread(substate);
    }
    else
    {
        PyEval_RestoreThread(py_mainstate);
    }

    // 构造Python对象pArgs和pKwargs
    if (args.Length() == 0)
    {
        pArgs = PyList_New(0);
    }
    else
    {
        pArgs_ = napi_value_to_pyobject(env, args);
        if (pArgs_ == NULL)
        {
            Napi::TypeError::New(env, "Argument `args` can not be converted to python list")
                .ThrowAsJavaScriptException();
            goto cleanup;
        }
        else
        {
            pArgs = PyList_AsTuple(pArgs_);
            Py_XDECREF(pArgs_);
        }
    }

    if (napi_object_is_empty(env, kwargs))
    {
        pKwargs = PyDict_New();
    }
    else
    {
        pKwargs = napi_value_to_pyobject(env, kwargs);
        if (pKwargs == NULL)
        {
            Napi::TypeError::New(env, "Argument `kwargs` can not be converted to python dict")
                .ThrowAsJavaScriptException();
            goto cleanup;
        }
    }

    // 调用Python的函数
    pCallable = PyObject_GetAttrString(pObject, attr.Utf8Value().c_str());
    if (pCallable == NULL)
    {
        throw_pyexception_in_javascript(env, "python-ts._call_python failed");
        goto cleanup;
    }

    // 真正地执行调用
    if (has_callback)
    {
        // PyCallWorker异步调用
        wk = new PyCallWorker(callback, pCallable, pArgs, pKwargs, substate);
        wk->Queue();
        goto cleanup;
    }

    pRet = PyObject_Call(pCallable, pArgs, pKwargs);
    Py_DECREF(pCallable);
    Py_XDECREF(pArgs);
    Py_XDECREF(pKwargs);
    if (pRet == NULL)
    {
        throw_pyexception_in_javascript(env, "python-ts._call_python failed");
        goto cleanup;
    }

    result = pyobject_to_napi_value(env, pRet, substate);
    Py_DECREF(pRet);

cleanup:
    PyEval_SaveThread();
    return result;
}

/* 列表pyobject的可用方法
_dir(object, context)
参数
    object, serialize以后的PyObject*
    context, 上下文
返回
    [[attr1, is_method, value], [attr2, is_method, value], ...]
*/
Napi::Array _dir(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    Napi::Array result = Napi::Array::New(env), names, row;
    Napi::Object context = Napi::Object::New(env), object;
    PyObject *pObject, *dir, *pItem, *pAttr;
    PyThreadState *substate;
    std::string name;
    uint32_t i, j, is_method;

    if (info.Length() < 1 || !info[0].IsObject())
    {
        Napi::TypeError::New(env, "Please call with (object) and `object` should be of type Object")
            .ThrowAsJavaScriptException();
        return result;
    }
    object = info[0].As<Napi::Object>();

    if (info.Length() >= 2)
    {
        if (!info[1].IsObject())
        {
            Napi::TypeError::New(env, "Argument `context` should be an Object")
                .ThrowAsJavaScriptException();
            return result;
        }
        context = info[1].As<Napi::Object>();
    }

    pObject = deserialize_pyobject(env, object);
    if (pObject == NULL)
    {
        Napi::TypeError::New(env, "Argument `object` should be a python-ts/PyObject* Object or `object` recycled!")
            .ThrowAsJavaScriptException();
        return result;
    }

    if (object.Get("state") != context.Get("state") && !in_same_context(env, object, context))
    {
        Napi::TypeError::New(env, "Cannot dir object from different context!")
            .ThrowAsJavaScriptException();
        return result;
    }

    // 防止python环境未初始化
    __init_python(env);

    substate = pycontext_get(context, "state");
    if (substate != NULL)
    {
        PyEval_RestoreThread(substate);
    }
    else
    {
        PyEval_RestoreThread(py_mainstate);
    }

    dir = PyObject_Dir(pObject);
    if (dir == NULL)
    {
        throw_pyexception_in_javascript(env, "python-ts._dir failed");
        goto cleanup;
    }

    names = pyobject_to_napi_value(env, dir, substate).As<Napi::Array>();
    j = 0;
    for (i = 0; i < names.Length(); i++)
    {
        row = Napi::Array::New(env);
        name = names.Get(i).As<Napi::String>().Utf8Value();
        if (name == "__builtins__" || name == "__loader__" || name == "__spec__")
        {
            continue;
        }
        row.Set((uint32_t)0, names.Get(i));
        pItem = PyList_GetItem(dir, i);
        pAttr = PyObject_GetAttr(pObject, pItem);
        is_method = (uint32_t)PyCallable_Check(pAttr);
        row.Set((uint32_t)1, is_method);
        if (is_method)
        {
            row.Set((uint32_t)2, env.Null());
        }
        else
        {
            row.Set((uint32_t)2, pyobject_to_napi_value(env, pAttr, substate));
        }
        Py_XDECREF(pAttr);
        result.Set(j, row);
        j += 1;
    }

    Py_XDECREF(dir);

cleanup:
    PyEval_SaveThread();
    return result;
}

/* _exec/_eval的实际执行函数
_exec(code, context, callback)
_eval(code, context, callback)
核心代码 =>
    pRet = PyRun_String(code.Utf8Value().c_str(), start, pDict, pDict);
*/
Napi::Value pyrun(const Napi::CallbackInfo &info, int start)
{
    Napi::Env env = info.Env();
    Napi::Value result = env.Null();
    Napi::String code;
    Napi::Object context;
    Napi::Function callback;
    PyObject *pRet, *pDict, *main;
    PyThreadState *substate;
    PyRunWorker *wk;
    bool has_callback = false;

    if (info.Length() < 1 || !info[0].IsString())
    {
        Napi::TypeError::New(env, "Please call with (code) and `code` should be of type String")
            .ThrowAsJavaScriptException();
        return result;
    }
    code = info[0].As<Napi::String>();
    context = Napi::Object::New(env);

    if (info.Length() >= 2)
    {
        if (!info[1].IsObject())
        {
            Napi::TypeError::New(env, "Argument `context` should be an Object")
                .ThrowAsJavaScriptException();
            return result;
        }
        context = info[1].As<Napi::Object>();
    }

    if (info.Length() >= 3)
    {
        if (!info[2].IsFunction())
        {
            Napi::TypeError::New(env, "Argument `callback` should be an Function")
                .ThrowAsJavaScriptException();
            return result;
        }
        has_callback = true;
        callback = info[2].As<Napi::Function>();
    }

    // 以防Python没有初始化
    __init_python(env);

    substate = pycontext_get(context, "state");
    if (substate != NULL)
    {
        PyEval_RestoreThread(substate);
        main = (PyObject *)pycontext_get(context, "main");
        pDict = PyModule_GetDict(main);
    }
    else
    {
        PyEval_RestoreThread(py_mainstate);
        pDict = PyModule_GetDict(py_main);
    }

    if (has_callback)
    {
        // PyRunWorker异步调用
        wk = new PyRunWorker(callback, code.Utf8Value().c_str(), start, pDict, pDict, substate);
        wk->Queue();
        goto cleanup;
    }
    pRet = PyRun_String(code.Utf8Value().c_str(), start, pDict, pDict);
    if (pRet == NULL)
    {
        throw_pyexception_in_javascript(env, "python-ts._exec failed");
        goto cleanup;
    }

    result = pyobject_to_napi_value(env, pRet, substate);
    Py_XDECREF(pRet);

cleanup:
    PyEval_SaveThread();
    return result;
}

/* 执行代码块
_exec(code, context, callback)
*/
Napi::Value _exec(const Napi::CallbackInfo &info)
{
    return pyrun(info, Py_file_input);
}

/* 执行表达式并返回
_eval(code, context, callback)
*/
Napi::Value _eval(const Napi::CallbackInfo &info)
{
    return pyrun(info, Py_eval_input);
}

/* 删除python对象
_delete_pyobject(obj, context)
参数
    obj: {"type": PYOBJECT_WRAPPER, ...}
    context: 上下文
返回
    成功则返回True，失败raise
*/
Napi::Boolean _delete_pyobject(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    Napi::Object obj, ref, context = Napi::Object::New(env);
    Napi::Number index;
    Napi::Boolean result = Napi::Boolean::New(env, false);
    Napi::Array references = env.Global().Get(REFERENCES).As<Napi::Array>();
    Napi::Object objects = env.Global().Get(OBJECTS).As<Napi::Object>();
    PyThreadState *substate;

    if (info.Length() < 1 || !info[0].IsObject())
    {
        Napi::TypeError::New(env, "Please call with (obj, context?), obj must be an Object")
            .ThrowAsJavaScriptException();
        return result;
    }
    obj = info[0].As<Napi::Object>();

    if (info.Length() >= 2)
    {
        if (!info[1].IsObject())
        {
            Napi::TypeError::New(env, "Argument `context` should be an Object")
                .ThrowAsJavaScriptException();
            return result;
        }
        context = info[1].As<Napi::Object>();
    }

    if (!in_same_context(env, obj, context))
    {
        Napi::TypeError::New(env, "Cannot delete object from different context!")
            .ThrowAsJavaScriptException();
        return result;
    }

    // 防止Python未被初始化
    __init_python(env);

    substate = pycontext_get(context, "state");
    if (substate != NULL)
    {
        PyEval_RestoreThread(substate);
    }
    else
    {
        PyEval_RestoreThread(py_mainstate);
    }

    index = obj.Get("index").As<Napi::Number>();
    if (uint32_t(index) < references.Length())
    {
        ref = references.Get(index).As<Napi::Object>();
        if (obj.Get("value").As<Napi::String>() == ref.Get("value").As<Napi::String>())
        {
            Py_XDECREF(deserialize_pyobject(env, obj));
            references.Set(index, env.Null());
            objects.Delete(obj.Get("value").As<Napi::String>());
        }
    }
    else
    {
        PyEval_SaveThread();
        return result;
    }

    PyEval_SaveThread();
    return Napi::Boolean::New(env, true);
}

/* 创建python隔离上下文
_create_pycontext()
返回
    PyThreadState对象的指针(64位)
*/
Napi::Value _create_pycontext(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    Napi::Object context;
    PyThreadState *substate, *oldstate;

    __init_python(env);

    PyEval_RestoreThread(py_mainstate);

    oldstate = PyThreadState_Swap(NULL);
    substate = Py_NewInterpreter();
    if (substate == NULL)
    {
        /* Since no new thread state was created, there is no exception to
           propagate; raise a fresh one after swapping in the old thread
           state. */
        PyThreadState_Swap(oldstate);
        PyErr_SetString(PyExc_RuntimeError, "Sub-Interpreter Creation Failed");
        throw_pyexception_in_javascript(env, "python-ts._create_pycontext failed");
        PyEval_SaveThread();
        return env.Null();
    }
    else
    {
        // 这里不太会不成功, 因为之前__init_python的时候已经调用成功了
        __prepare_python_env(env, false);
    }
    // 这里我们不回收subinterpreter，而是把它添加到contexts中
    context = serialize_pycontext(env, substate);

    // 切回主解释器
    PyThreadState_Swap(oldstate);

    PyEval_SaveThread();
    return context;
}

/* 删除pycontext
_delete_pycontext(context)
参数
    context: uintptr_t的string
返回
    成功则返回true
    失败则throw相应的错误
*/
Napi::Boolean _delete_pycontext(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    Napi::Object context;
    Napi::Array contexts = env.Global().Get(CONTEXTS).As<Napi::Array>();
    Napi::Object objects = env.Global().Get(OBJECTS).As<Napi::Object>();
    PyThreadState *substate;
    PyObject *main;
    uint32_t index;

    if (info.Length() < 1 || !info[0].IsObject())
    {
        Napi::TypeError::New(env, "Argument `context` is required, it must be a string").ThrowAsJavaScriptException();
        return Napi::Boolean::New(env, false);
    }

    context = info[0].As<Napi::Object>();

    // 以防Python未被初始化
    __init_python(env);

    substate = pycontext_get(context, "state");
    main = (PyObject *)pycontext_get(context, "main");

    if (substate == NULL || main == NULL)
    {
        Napi::TypeError::New(env, "Invalid context").ThrowAsJavaScriptException();
        return Napi::Boolean::New(env, false);
    }

    PyEval_RestoreThread(substate);
    Py_XDECREF(main);
    Py_EndInterpreter(substate);
    PyEval_SaveThread();

    // 干掉contexts中的值
    index = context.Get("index").As<Napi::Number>();
    contexts.Set(index, env.Null());
    objects.Delete(context.Get("state").As<Napi::String>());

    return Napi::Boolean::New(env, true);
}

/* 用来写一些测试方法的内部调用，不删是为了方便 */
Napi::Value __internal(const Napi::CallbackInfo &info)
{
    Napi::Env env = info.Env();
    Napi::Value val;
    Napi::String str;
    Napi::Array arr = Napi::Array::New(env, 2);
    str = Napi::String::New(env, "apple");
    val = str;
    arr.Set(uint32_t(0), val);
    arr.Set(uint32_t(1), Napi::String::New(env, "banana"));
    val = arr;
    return val;
}

Napi::Object Init(Napi::Env env, Napi::Object exports)
{
    Napi::Object references = Napi::Array::New(env); // 所有没被Py_DECREF的序列化对象必须都在这里面
    Napi::Object contexts = Napi::Array::New(env);   // 所有创建的context, str表示的uintptr_t指针
    Napi::Object objects = Napi::Object::New(env);   // 为了复用references/contexts, str指针 -> {type, index}
    env.Global().Set(REFERENCES, references);
    env.Global().Set(CONTEXTS, contexts);
    env.Global().Set(OBJECTS, objects);
    exports.Set(Napi::String::New(env, "references"), references);
    exports.Set(Napi::String::New(env, "contexts"), contexts);
    exports.Set(Napi::String::New(env, "objects"), objects);

    exports.Set(Napi::String::New(env, "_set_runtime_path"), Napi::Function::New(env, _set_runtime_path));
    exports.Set(Napi::String::New(env, "_set_debug"), Napi::Function::New(env, _set_debug));
    exports.Set(Napi::String::New(env, "_init_python"), Napi::Function::New(env, _init_python));
    exports.Set(Napi::String::New(env, "_destroy_python"), Napi::Function::New(env, _destroy_python));
    exports.Set(Napi::String::New(env, "_import_module"), Napi::Function::New(env, _import_module));
    exports.Set(Napi::String::New(env, "_reload_module"), Napi::Function::New(env, _reload_module));
    exports.Set(Napi::String::New(env, "_call_python"), Napi::Function::New(env, _call_python));
    exports.Set(Napi::String::New(env, "_dir"), Napi::Function::New(env, _dir));
    exports.Set(Napi::String::New(env, "_exec"), Napi::Function::New(env, _exec));
    exports.Set(Napi::String::New(env, "_eval"), Napi::Function::New(env, _eval));
    exports.Set(Napi::String::New(env, "_delete_pyobject"), Napi::Function::New(env, _delete_pyobject));
    exports.Set(Napi::String::New(env, "_create_pycontext"), Napi::Function::New(env, _create_pycontext));
    exports.Set(Napi::String::New(env, "_delete_pycontext"), Napi::Function::New(env, _delete_pycontext));

    // testing only
    exports.Set(Napi::String::New(env, "__internal"), Napi::Function::New(env, __internal));
    return exports;
}

NODE_API_MODULE(plugins, Init)
