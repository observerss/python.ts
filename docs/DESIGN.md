# Python.ts: 在 NodeJS 里面嵌入 Python 运行时

[toc]

## 1. The Problem

## 2. FFI Approach

### 2.1 FFI Basics

### 2.2 Embed Python in C

#### 2.1.1 Embedded Python

#### 2.1.2 Python C-API

### 2.3. C++ for NodeJS

#### 2.3.1 N-API

#### 2.3.2 Node Addon API

## 3. Implementations

### 3.1 Basic Configurations

### 3.2 Load a Python Module

### 3.3 Call Python Function

### 3.4 PyObject in NodeJS

### 3.5 Isolating Python Enviroments

### 3.6 Execute Python Calls (Long Runing) in Threads

## 4. TODOs

- [ ] APIs
  - [ ] python
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
  - [ ] clib
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
    - [ ] Clib Internals
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
- [ ] Threads and Async Callback
  - [x] `PyCallWorker`
  - [x] `PyRunWorker`
  - [ ] `ThreadPoolExecutor` with `xxsubinterpreters`
- [ ] 32bit Python and NodeJS

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
