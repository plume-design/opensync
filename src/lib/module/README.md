# Introduction

This library can be used to implement loadable and built-in modules. The design
goals are:

- Support built-in (compiled in) modules
- Support dynamically loadable modules (via shared libraries)
- Support loading of multiple modules
- No code changes should be required to convert a built-in module to a dynamic
  one and vice versa

The library was loosely inspired by the Linux module system.

## Implementation Notes

The current implementation exploits a concept in the GCC compiler known as
library constructors (and destructors). Library constructors are functions that
are automatically called by the compiler when an execution unit is loaded into
memory -- be it a shared library or an executable image (in this case they are
executed **before** the `main()` function).

A variant of these constructor functions are available in all major compilers,
including LLVM and MSVC. Despite this fact, the API is designed in such a way
that it would be theoretically possible to write an implementation without the
use of such functions.

# Usage

The API provides functions for declaring modules and for managing said modules.

## Module Structure

All that a module is required to do is declare its unique name and start/stop
functions. This is simply achieved by calling the following macro:

```
MODULE(name, start, stop)
```

This will declare a module with the specified `name`. `start` and `stop` should
be pointers to the respective start/stop functions of the module.

## Module Management

There is no special requirement to manage built-in modules. They will be
registered automatically when they are compiled in (using the library
constructor functions). To load external modules, there are two functions
available:

- `module_load(const char *path)` this function will load a single module
  using the file `path`.
- `module_load_all(const char *patter)` this function will load several modules
  at once. `glob()` will be used to expand `pattern` and it will proceed to 
  load all matching files. For example `module_load_all("/lib/modules/*.so")`
  will load all `.so` files from `/lib/modules`.

Similarly, a module can be unloaded with the following functions:

- `module_unload(const char *path)`
- `module_unload_all(const char *pattern)`

_Note that both `module_unload()` and `module_unload_all()` should take
**exactly** the same string hat was used in their counterpart functions._

When the main process starts, the built-in modules only register themselves.
No start or stop functions are called yet. In order to do this, `module_init()`
must be called by the main process when it deems that it is safe to do so.

Similarly, loadable modules are not started right away. After `module_load()` or
`module_load_all()`, `module_init()` must be called in order to start the
modules. `module_init()` can be called several times and modules that are
already started won't be started twice.

Modules are automatically stopped when they are unloaded or when the main
process exits. During the later case, the module is stopped **AFTER** the
`main()` function exits. Applications are enocouraged to stop modules in a
controlled fashion before the `main()` process exits using the `module_fini()`
function.

