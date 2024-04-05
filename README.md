# snaketongs — library for using Python libraries from C++

snaketongs allows you to interact with Python modules and objects as if they were normal C++ objects.
Internally, it runs a Python interpreter as a subprocess of your C++ program, communicating with it using pipes.
See below for a [comparison with embedding](#comparison-with-embedding), the official way of using Python from C++.


## Setup

snaketongs consists of two parts: `snaketongs.hpp` (a header file to be included) and `subproc.o` (an object file to be linked against).
These can typically be added by passing the following options to the C++ compiler:

```sh
-I ${PATH_TO_SNAKETONGS}/include ${PATH_TO_SNAKETONGS}/subproc.o
```

The latter, `subproc.o`, must be compiled from source by running `make` in this directory.


## Usage

The following snippet summarizes the most important features:

```cpp
#include <snaketongs.hpp>

int main() {
	// Start a process by creating a `snaketongs::process` object.
	// (The process will be terminated when it goes out of scope.)
	snaketongs::process proc;

	// All the following (auto) variables are of type `snaketongs::object`,
	// which is a move-only Python object reference.

	// Imports:
	auto copy = proc["shutil.copy"]; // from shutil import copy
	auto re = proc["re.*"]; // import re
	auto IndexError = proc["builtins.IndexError"]; // for builtins not recognized by snaketongs

	// Builtins are exposed as members of `snaketongs::process`.
	// Here we use Python's str, range, map, and sorted:
	auto bad_sorting = proc.sorted(proc.map(proc.str, proc.range(100)));
	std::cout << "%s ended up 30th" % bad_sorting[30] << std::endl;
	std::cout << "%s ended up 40th" % bad_sorting[40] << std::endl;

	// When calling objects' methods, we are forced to deviate from Python syntax,
	// since C++ does not support dynamic lookup by name:
	std::cout << "2 ended up %ith" % bad_sorting.call("index", "2") << std::endl; // means bad_sorting.index("2")

	// Similarly for attributes:
	auto complex_one = 2.71 ** proc.complex(0, 6.28);
	std::cout << complex_one.get("real") << std::endl;
	std::cout << complex_one.get("imag") << std::endl;

	// Creating collections:
	auto list = proc.make_list(1, 2, 3); // equivalent to `[1, 2, 3]`
	auto tuple = proc.make_tuple(12345, 54321, "hello!"); // equivalent to `(12345, 54321, "hello!")`
	auto tuple_singleton = proc.make_tuple("hello"); // equivalent to `("hello",)`
	// Do not confuse with Python's built-in constructors:
	auto letters = proc.tuple("hello"); // `("h", "e", "l", "l", "o")`

	// Creating functions (template argument is the maximum arity):
	auto lambda = proc.make_function<3>([](snaketongs::object a, int b, std::string c = "") {
		// ...
	});
	auto vlambda = proc.make_variadic_function([](std::vector<snaketongs::object> args) {
		// ...
	});
	// Implicitly creating functions:
	auto squares = proc.map([](auto x) { return x*x; }, proc.range(10));
	// (only works for non-variadic functions with less than 8 arguments)

	// Iterating Python objects (same as Python for-loop):
	for(auto &elem : squares)
		std::cout << elem << std::endl;

	// Simple with-statement (cannot suppress exceptions)
	// - equivalent to  `with open("README.md") as file:  print(file.readline())`
	{
		snaketongs::with file = proc.open("README.md");
		std::cout << file.call("readline") << std::endl;
	}

	// Full with-statement
	// - equivalent to  `with contextlib.suppress(IndexError):  letters[42]`
	{ snaketongs::with ctx = proc["contextlib.suppress"](IndexError); try {
		letters[42];
	} catch(...) { ctx.exit(); } }
}
```

### Conversions across languages

The following types of C++ objects can be implicitly converted into Python objects:

- integer types as Python `int` and floating point types as Python `float`
- C++ `bool` (without even implicit conversions) as Python `bool`
- `std::string_view` (including string literals, `std::string` and (`const`)` char *`) as Python `str`
- `std::span<std::byte>` as Python `bytes`
- some lambda functions (see snippet above)

In some cases, C++ cannot find the implicit conversion.
For example, `":".call("join", my_iterable)` will not work, because `":"` is a `const char[]`, not Python `str`.
To force an (otherwise implicit) conversion, use `process::into_object`, e.g. `proc.into_object(":").call("join", my_iterable)`.
Alternatively, one can use Python's builtin constructors, e.g. `proc.str(":").call("join", my_iterable)`.

On the other hand, Python objects are *not* implicitly convertible to C++ types.
This is by design: there is generally no type information available at compile-time, and as such the conversion may throw at runtime.
To convert an object you know *already is* of the desired type, use a C++ cast, e.g. `(std::string) my_string_obj`.
This cast must be explicit, except for `auto` lambda parameters.
Instead of the cast, you can also use the `.conv()` method, e.g. `std::string s = my_string_obj.conv()`.

To convert an object that might not be of the desired type (e.g. Python `int` to C++ `std::string`), two conversions are necessary:
to change the data type, and to move the object across languages (in either order).
For convenience, there are some shortcuts for this:

- Casting to C++ `bool` first converts the object to Python `bool` using Python's rules (the `bool` function, which uses `.__bool__()`).
- `to_string(my_object)` calls Python function `str` on `my_object` and then casts the result to `std::string`
- Python objects can be written to an `std::ostream` using operator `<<`.
  The effect is the same as using `print`, i.e., the object is first converted using `str`, then printed to the stream.

### Attributes and collection items

| Python syntax       | snaketongs full syntax     | snaketongs shortcut syntax                                   | note |
| ------------------- | -------------------------- | ------------------------------------------------------------ | ---- |
| `obj.field`         | `obj.attr("m").get()`      | `obj.getattr("m")`, `obj.get("m")`, `obj.call("m", args...)` |      |
| `obj.field = value` | `obj.attr("m").set(value)` | `obj.setattr("m", value)`, `obj.set("m", value)`             |      |
| `del obj.field`     | `obj.attr("m").del()`      | `obj.delattr("m")`                                           |      |
| `hasattr(obj, 'm')` | `obj.attr("m").present()`  | `obj.hasattr("m")`                                           |      |
| `obj[key]`          | `obj.item(key).get()`      | `obj.getitem(key)`, `obj[key]`                               |      |
| `obj[key] = value`  | `obj.item(key).set(value)` | `obj.setitem(key, value)`                                    |      |
| `del obj[key]`      | `obj.item(key).del()`      | `obj.delitem(key)`                                           |      |
| `key in obj`        | `obj.item(key).present()`  | `obj.hasitem(key)`                                           | only applicable to dict-like `obj`s |

The difference between `obj.getattr(name)` and `obj.get(name)` is that `obj.get(name)` requires the `name` to be a C++ string view.
Likewise with `obj.setattr(name, value)` and `obj.get(name, value)`.
The purpose of `get` and `set` is to provide a less distracting way to write `getattr` and `setattr`
while minimizing the risk of a possible confusion with `dict.get` getting past the C++ compiler.

The result of `obj.attr("m")` and `obj.item(key)` should be used immediately and only once, it should not be stored in a variable.
In addition to `get`/`set`/`del`/`present`, it can be used for assignment from `snaketongs::object` and augmented assignment from `snaketongs::object`
(with precisely the same semantics as in Python, i.e. an *in-place* operator being called and its result assigned back to the attribute/item).

### Shortcuts

| Python syntax        | snaketongs full syntax         | snaketongs shortcut syntax | note |
| -------------------- | ------------------------------ | -------------------------- | ---- |
| `repr(obj)`          | `proc.repr(obj)`               | `obj.repr()`               |      |
| `str(obj)`           | `proc.str(obj)`                | `obj.str()`                |      |
| `bytes(obj)`         | `proc.bytes(obj)`              | `obj.bytes()`              |      |
| `format(obj[, fmt])` | `proc.format(obj[, fmt])`      | `obj.format([fmt])`        |      |
| `hash(obj)`          | `(std::size_t) proc.hash(obj)` | `obj.hash()`               | or use `std::hash<snaketongs::object>(obj)` |
| `len(obj)`           | `(std::size_t) proc.len(obj)`  | `obj.len()`                |      |
| `iter(obj)`          | `proc.iter(obj)`               | `obj.iter()`               | or use range-`for` loop, which calls `iter()` implicitly |
| `type(obj)`          | `proc.type(obj)`               | `obj.type()`               |      |
| `a is [not] b`       | `proc.op_is[_not](a, b)`       | `a.is[_not](b)`            |      |
| `a [not] in b`       | `[not] proc.op_contains(b, a)` | `a.[not_]in(b)`            | note reversed operands in `contains` |

### Function arguments

snaketongs supports most of Python's function call syntax:

```cpp
using snaketongs::kw;
snaketongs::process proc;

auto dt = proc["datetime.datetime"];
auto log = proc["sys.stderr"];

auto fields = proc.make_tuple("success", 9000, true);

proc.print(dt.call("now"), *fields, kw("sep")=" | ", kw("file")=log); // equivalent to `print(dt.now(), *fields, sep=" | ", file=log)`

auto log_kwargs = proc.dict(kw("sep")=" | ", kw("file")=log); // equivalent to `dict(sep=" | ", file=log)`

proc.print(dt.call("now"), *fields, **log_kwargs); // equivalent to `print(dt.now(), *fields, **log_kwargs)`
```

There can be as many arguments, keyword arguments, `*`-arguments, and `**`-arguments as necessary.
Note that snaketongs does fewer checks, i.e., it allows (without throwing) a superset of what Python allows.
However, it is not recommended to rely on this.

### Creating Python classes

There is currently no special support for creating classes. However, you can use what Python already provides:

```cpp
using snaketongs::kw;
snaketongs::process proc;

// for simple cases:
auto Vec3 = proc["collections.namedtuple"]("Point3D", "x, y, z");

// otherwise:
auto BaseHTTPRequestHandler = proc["http.server.BaseHTTPRequestHandler"];
auto storage = proc.dict(); // must be shared across requests (and thus also across handler instances)
auto MyHTTPRequestHandler = proc.type("MyHTTPRequestHandler", proc.make_tuple(BaseHTTPRequestHandler), proc.dict(
	kw("do_GET") = [&](auto self) {
		auto data = storage.call("get", self.get("path"));
		if(data.is_not(proc.None)) {
			self.call("send_response", 200);
			self.call("send_header", "Content-Type", "text/plain");
			self.call("end_headers");
			self.get("wfile").call("write", data);
		} else {
			self.call("send_error", 404);
		}
	},
	kw("do_PUT") = [&](auto self) {
		auto len = self.get("headers")["content-length"];
		if(len)
			len = proc.int_(len);
		storage.setitem(self.get("path"), self.get("rfile").call("read", len));
		self.call("send_error", 202); // accepted
	}
));
proc["http.server.HTTPServer"](proc.make_tuple("", 8000), MyHTTPRequestHandler).call("serve_forever");
```

### `snaketongs::process` lifecycle

The `process` class does not allow copying (a Python process cannot be shared among multiple instances).
It also does not allow moving, since it is quite large and `snaketongs::object`s need to keep a reference to it.
In case you need move semantics, use `std::unique_ptr` explicitly.

A process may be in one of the following states:

- running - immediately after constructor returns
  - `process.terminated()` returns `false`
  - all members can be used
  - all `snaketongs::object`s can be used
- failed - when a process dies prematurely (e.g. after receiving a signal or after a call to Python's `os._exit()`)
           or when the communication with it fails for whatever reason
  - `process.terminated()` returns `false` (even if the process is no longer running)
  - all members can be used (but throw a `snaketongs::io_error`)
  - all `snaketongs::object`s can be used (but throw a `snaketongs::io_error`)
- terminated - after `process.terminate()` is called (this is optional)
  - `process.terminated()` returns `true`
  - using the remaining members causes UB (except for destructor)
  - using `snaketongs::object`s causes UB (except for destructor and assignment)

The constructor and `.terminate()` may also throw a `snaketongs::io_error`.

There is no way to check a process is not "failed" — such a check would be inherently racy.

### `snaketongs::object` lifetime

`object` is a proxy (remote reference) for a Python object. It is a relatively small object.
It depends on a `snaketongs::process` instance and must not ever outlive it.
In other words, if there are any `object`s existing when their `snaketongs::process`'s destructor finishes, the behavior is undefined.
It is allowed for an `object` to exist after `process.terminate()` as long as that `object` is not used and is destructed before the `process`.

Multiple `object` variables can point to the same object.
Whether this is the case can be checked using Python's `is` operator (the `.is(...)` method in C++).

While `object` instances do not have a copy constructor, they can be duplicated explicitly, using the `.dup()` method.
Doing so does not duplicate the Python object, it results in another variable pointing to the same object.
Despite this, duplicating an `object` is not trivial/constexpr/noexcept; it requires a call to Python like most other `object` methods.

`object` variables can be moved and move-assigned. Similarly to `std::unique_ptr`, moved-from variables become null.
Note that null is *distinct* from Python's `None`, which is a regular Python object like any other.
Null variables cannot be used in any meaningful way until/unless reassigned.
Note that *unlike* `std::unique_ptr`, conversion to `bool` (e.g. in an `if`) and comparison to another `object` (null or non-null) is *undefined*;
these operations inspect the target Python objects.
It is possible to check for null using `.is_nullptr()`, to assign `nullptr` to an `object` variable,
and to *explicitly* initialize an `object` variable from `nullptr`.

For most operations, a `const object &` is enough.
As with `std::unique_ptr`, the `const` only disables reassignment but does not disallow mutations to the Python object.

### Lifetimes of rvalue-only classes

The results of `object.attr(...)`, `object.item(...)`, `*object`, `**object`, `object.conv()` should be used immediately and only once.
They should not be stored in variables. To check against this, they can only be used as rvalues.
Note that `std::move` will suppress the error but will *not* fix the probable use of dangling references within the moved object.

### C++ function objects' lifetimes

When converted to a Python object, a function object is copied/moved into dynamically allocated memory and kept until released by Python.
The destructor can be called any time after the function becomes unreachable
and before the `snaketongs::process` is destructed (depending on Python's garbage collection).
However, it only happens during calls to Python, never interrupting running C++ code.

Some Python-referenced objects will only be released after the process terminates.
If you want to call any Python code from the destructor, make sure to first check that `process.terminated()` returns `false`.
This check is not necessary for `snaketongs::object` destructor calls, which already contain the check.

Note that only the function object itself is copied.
If your lambda captures variables by reference, Python code may be able to trigger UB by calling the lambda too late.
Python libraries tend to store lambdas much more often than C++ libraries (which generally call it within the same call).
Use capture by copy when possible. For values that need to be shared with other code, use `std::shared_ptr`.
Its overhead is negligible when compared to pipe IPC with Python.

On the other hand, capturing `snaketongs::process` in Python lambdas by reference is always safe, since no Python code will run after it is destructed.

### Exception lifetimes

When an exception propagates from Python to C++, it is wrapped in `snaketongs::exception` which is derived from both `std::exception` and `snaketongs::object`.
Unlike a regular `snaketongs::object`, a `snaketongs::exception` is allowed to outlive its `snaketongs::process`
(however, its methods inherited from `snaketongs::object` must not be used after the process is terminated and/or destructed).
This ensures that a Python exception can be caught even in a scope where the process is not valid anymore.

The `.what()` method of `snaketongs::exception` returns a C string that is valid for at least as long as the exception itself.
The method can be called even after the process is terminated and destructed.
The string returned is only valid until the exception is moved/copied or any of its members is used.

For technical reasons, `snaketongs::exception` can be copied, but its copy constructor is deprecated.
If you need a copy, use the `.dup()` method, as with `snaketongs::object`.
The move and copy constructors and the `.dup()` method can be safely used even if the process is terminated or destructed.
However, the copy constructor and the `.dup()` method (but not the move constructor) may throw an `io_error` if the process is failed.

The C++ `throw` statement can be used to throw instances of `snaketongs::object` (wrapping in `snaketongs::exception` is optional).
When a C++ lambda used from Python throws such an exception, the exception can propagate in Python code where it can be caught using a `try-except` of a matching type.
If uncaught by Python code, it propagates back to C++ code where it is wrapped in `snaketongs::exception` again.

When any other C++ exception propagates from a C++ lambda into Python code, it is wrapped in an opaque Python object inheriting from `BaseException` but not `Exception`.
If uncaught by Python code, it propagates back to C++ code where it is unwrapped back to the original C++ object.
Whether handled by Python code or not, `std::exception_ptr` may be used internally and kept until released by Python's garbage collector,
similarly to function objects (see the previous section for details).
On CPython, an unhandled exception is released immediately when it propagates back to C++. (However, this is a CPython implementation detail.)

### Undefined behavior

In addition to the usual language and standard library UB, snaketongs also reserves the following as undefined:

- violating the lifetime rules as described in the previous sections
  - using null (e.g. moved-from) `snaketongs::object` instances for anything other than destruction, move, re-assingment, and `.is_nullptr()` check
  - using a `snaketongs::process` whose `.terminate()` method had been called for anything other than destruction, and `.terminated()` check
  - using a `snaketongs::object` after its `snaketongs::process` is destructed or terminated (using the `.terminate()` method)
  - having a `snaketongs::object` survive its `snaketongs::process`'s destruction
  - using an instance of any of the rvalue-only classes outside of the full-expression that created it
- using/defining/specializing any entity via the `snaketongs::detail` namespace
- interfering with snaketongs Python code, for example:
  - calling Python's `exec` or `eval` from C++ without a `globals` argument
  - calling Python's `locals`, `globals`, `vars`, etc. from C++
  - using the `__main__` module
- using snaketongs from multiple C++ threads, from signal handlers, from multiple Python threads, or from Python destructors/finalizers


## Comparison with embedding

Embedding means running the entire interpreter as a library, as opposed to executing it as a standalone program.
For the basic purpose of just using a Python library from a C++ program, embedding would generally be considered more elegant.
Practically speaking, either solution has its advantages and disadvantages:

- The main reason snaketongs chooses to run a subprocess is that it is easy to set up.
  It requires almost no changes to the C++ program or its build system.
- An embedded interpreter will generally have much better performance.
  All objects live in the same shared address space and function calls do not require context switching.
  Some technologies even allow cross-language function inlining and other optimizations.
- When running a subprocess, you do not depend on any particular implementation, but you also do not get any guarantees about it.
- In both cases it is possible to use a system-provided implementation (an executable from `$PATH`, or a shared library, respectively).
  And in both cases it is possible to package the application together with a custom version of the interpreter.
- A subprocess may offer better isolation, to the point of being able to safely run untrusted code.
  Relying on just the interpreter means having a large attack surface consisting of code not primarily intended to withstand direct attacks.
  - Note that no isolation is currently supported by snagetongs itself.
    Replacing the `python` binary with a wrapper that drops privileges and/or closes itself in a container would not be hard, though.
    (Theoretically, `python` could even be replaced with a script that runs a full-blown virtual machine and the actual interpreter inside it.)
