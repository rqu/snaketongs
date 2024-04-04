#include <snaketongs.hpp>

#include <cmath>
#include <cstdio>
#include <exception>
#include <iostream>
#include <sstream>
#include <typeinfo>

namespace {

////////////////////////////////////////////////////////////////

static int GLOBAL_failed;

struct test_failed {};

void run_test_impl(auto test, const char *name) {
	std::fprintf(stderr, "\n[%s]\n", name);
	try {
		test();
	} catch(test_failed) {
		GLOBAL_failed++;
		return;
	} catch(std::exception &e) {
		std::fprintf(stderr, "Thrown %s: %s\n", typeid(e).name(), e.what());
		GLOBAL_failed++;
		return;
	}
	std::fputs("OK\n", stderr);
}

void assert_impl(bool cond, const char *msg) {
	if(!cond) {
		std::fputs(msg, stderr);
		throw test_failed{};
	}
}

std::string to_string(std::string_view view) {
	return std::string(view);
}

void assert_eq_impl(const auto &a, const auto &b, const char *a_name, const char *b_name) {
	if(a != b) {
		using std::to_string;
		using ::to_string;
		// also using ADL
		std::string a_str = to_string(a);
		std::string b_str = to_string(b);
		std::fprintf(stderr, "Assertion '%s == %s' failed:\n- %s = %s\n- %s = %s\n",
			    a_name, b_name, a_name, a_str.c_str(), b_name, b_str.c_str());
		throw test_failed{};
	}
}

#define ASSERT(COND) assert_impl(bool(COND), "Assertion '" #COND "' failed\n")
#define ASSERT_EQ(A, B) assert_eq_impl(A, B, #A, #B)
#define TEST(NAME, ...) run_test_impl(([]{__VA_ARGS__}), (NAME))

////////////////////////////////////////////////////////////////

bool have_children() {
	bool yes_or_err = std::system("ps -Ao'pid= ppid= comm=' | awk -ve=0 '$1 != '$$' && $2 == '$PPID' {exit e=1} END {exit e}'");
	bool  no_or_err = std::system("ps -Ao'pid= ppid= comm=' | awk -ve=1 '$1 != '$$' && $2 == '$PPID' {exit e=0} END {exit e}'");
	if(yes_or_err && !no_or_err)
		return true;
	if(no_or_err && !yes_or_err)
		return false;
	throw std::runtime_error("ps | awk failed");
}

using TEST_cout = std::stringstream;

constexpr auto TEST_endl_expect(std::string_view expected) {
	return [expected](std::ostream &s) {
		ASSERT_EQ(static_cast<TEST_cout &>(s).view(), expected);
	};
}

auto operator << (std::ostream &s, decltype(TEST_endl_expect("")) manip) {
	return manip(s);
}

} // ns

int main() {

if(have_children()) {
	std::fprintf(stderr, "Cannot run tests: unexpected child processes");
	return 1;
}

////////////////////////////////////////////////////////////////

TEST("proc ctor & dtor", {
	{
		snaketongs::process proc;
		ASSERT(have_children());
		ASSERT(not proc.terminated());
	}
	ASSERT(not have_children());
});

TEST("proc terminate", {
	snaketongs::process proc;
	ASSERT(have_children());
	ASSERT(not proc.terminated());
	proc.terminate();
	ASSERT(not have_children());
	ASSERT(proc.terminated());
});

TEST("proc crash", {
	{
		snaketongs::process proc;
		auto py_exit = proc["os._exit"];
		try {
			py_exit(0); // never returns
			ASSERT(not "exit returned");
		} catch(const snaketongs::io_error &) {
			// OK
		}
		try {
			proc.print("this should not be printed");
			ASSERT(not "print returned");
		} catch(const snaketongs::io_error &) {
			// OK
		}
		std::system("sleep .1 || sleep 1");
		ASSERT(have_children());
		ASSERT(not proc.terminated());
	}
	ASSERT(not have_children());
});

TEST("argv", {
	snaketongs::process proc;
	std::string argv_repr = to_string(proc["sys.argv"]);
	ASSERT_EQ(argv_repr, "['<snaketongs>']");
});

TEST("simple strings", {
	snaketongs::process proc;
	auto hw = proc.into_object(" ").call("join", proc.make_tuple("hello", "world"));
	static_assert(std::same_as<decltype(hw), snaketongs::object>);
	auto hw_str = (std::string) hw;

	// compare in c++
	ASSERT_EQ(hw_str, "hello world");

	// compare in python
	ASSERT(hw == "hello world");
	ASSERT("hello world" == hw);
	ASSERT(!bool(hw == "helloworld"));
	ASSERT(!bool(hw == "helloworld"));
});

TEST("simple numbers", {
	snaketongs::process proc;
	auto one = proc.into_object(1);
	ASSERT_EQ(one.type().get("__name__"), "int");

	auto half = one / proc.into_object(2);
	static_assert(std::same_as<decltype(half), snaketongs::object>);
	ASSERT_EQ(half.type().get("__name__"), "float");
	ASSERT_EQ(half.str(), "0.5");
	ASSERT_EQ((double) half, 0.5);
	ASSERT_EQ((float) half, 0.5f);
	ASSERT_EQ(half, 0.5);

	auto two = one * proc.into_object(2);
	static_assert(std::same_as<decltype(two), snaketongs::object>);
	ASSERT_EQ(two.type().get("__name__"), "int");
	ASSERT_EQ(two.str(), "2");
	ASSERT_EQ((int) two, 2);
	ASSERT_EQ((unsigned) two, 2u);
	ASSERT_EQ(two, 2);
});

TEST("float roundtrip", {
	snaketongs::process proc;

	auto inner = [&proc]<typename T> {
		ASSERT_EQ(T(proc.into_object((T) 1.0)), (T) 1.0);
		ASSERT_EQ(T(proc.into_object((T) 1.1)), (T) 1.1); // periodic in binary and hex
		ASSERT_EQ(T(proc.into_object((T) -42.)), (T) -42.);
		ASSERT_EQ(T(proc.into_object((T) +0.0)), (T) +0.0);
		ASSERT_EQ(T(proc.into_object((T) -0.0)), (T) -0.0);
		ASSERT_EQ(T(proc.into_object((T) +INFINITY)), (T) +INFINITY);
		ASSERT_EQ(T(proc.into_object((T) -INFINITY)), (T) -INFINITY);
		ASSERT(std::isnan(T(proc.into_object((T) NAN))));
	};
	inner.operator()<float>();
	inner.operator()<double>();
});

TEST("power", {
	snaketongs::process proc;
	{
		auto result = 3 ** proc.into_object(4);
		static_assert(std::same_as<decltype(result), snaketongs::object>);
		ASSERT_EQ(result.type().get("__name__"), "int");
		ASSERT_EQ((int) result, 81);
	}
	{
		auto result = proc.into_object(3) ** proc.into_object(4);
		static_assert(std::same_as<decltype(result), snaketongs::object>);
		ASSERT_EQ(result.type().get("__name__"), "int");
		ASSERT_EQ((int) result, 81);
	}
});

TEST("starcall", {
	using snaketongs::kw;
	snaketongs::process proc;
	auto lambda = proc["builtins.eval"]("lambda *args, **kwargs: repr(args) + repr(kwargs)", proc.dict());

	// not a starcall, just check the lambda
	ASSERT_EQ((std::string) lambda(1, 2, 3), "(1, 2, 3){}");

	// args
	ASSERT_EQ((std::string) lambda(*proc.into_object("xyz")), "('x', 'y', 'z'){}");
	ASSERT_EQ((std::string) lambda("ab", *proc.into_object("xyz"), proc.into_object("cd"), *proc.make_tuple(1, 2, 3), "ef"), "('ab', 'x', 'y', 'z', 'cd', 1, 2, 3, 'ef'){}");

	// kwargs
	ASSERT_EQ((std::string) lambda(kw("a")=1, kw("c")=2, kw("b")=3), "(){'a': 1, 'c': 2, 'b': 3}");
	auto dict = proc.dict(); dict.setitem("d", 3); dict.setitem("f", 2); dict.setitem("e", 1);
	ASSERT_EQ((std::string) lambda(**dict), "(){'d': 3, 'f': 2, 'e': 1}");
	auto dict2 = proc.dict(); dict2.setitem("g", 5);
	ASSERT_EQ((std::string) lambda(kw("a")=1, **dict, kw("c")=2, **dict2, kw("b")=3), "(){'a': 1, 'd': 3, 'f': 2, 'e': 1, 'c': 2, 'g': 5, 'b': 3}");

	// args + kwargs
	ASSERT_EQ((std::string) lambda("ab", *proc.into_object("xyz"), proc.into_object("cd"), kw("a")=1, **dict, kw("c")=proc.into_object(3)), "('ab', 'x', 'y', 'z', 'cd'){'a': 1, 'd': 3, 'f': 2, 'e': 1, 'c': 3}");
});

TEST("lambda", {
	using snaketongs::object;
	snaketongs::process proc;

	// unary
	ASSERT_EQ(to_string(proc.list(proc.map([](auto a){return a*a;}, proc.range(5)))), "[0, 1, 4, 9, 16]");
	ASSERT_EQ(to_string(proc.list(proc.map([](int a){return a*a;}, proc.range(5)))), "[0, 1, 4, 9, 16]");
	ASSERT_EQ(to_string(proc.list(proc.map([](object a){return a*a;}, proc.range(5)))), "[0, 1, 4, 9, 16]");

	// binary
	auto reduce = proc["functools.reduce"];
	ASSERT_EQ((std::string) reduce([](auto a, auto b){return b+a;}, "sdrawkcab"), "backwards");
	ASSERT_EQ((std::string) reduce([](std::string a, auto b){return b+a;}, "sdrawkcab"), "backwards");
	ASSERT_EQ((std::string) reduce([](auto a, std::string b){return b+a;}, "sdrawkcab"), "backwards");
	ASSERT_EQ((std::string) reduce([](std::string a, std::string b){return b+a;}, "sdrawkcab"), "backwards");
	ASSERT_EQ((std::string) reduce([](object a, auto b){return b+a;}, "sdrawkcab"), "backwards");
	ASSERT_EQ((std::string) reduce([](auto a, object b){return b+a;}, "sdrawkcab"), "backwards");
	ASSERT_EQ((std::string) reduce([](object a, object b){return b+a;}, "sdrawkcab"), "backwards");
	ASSERT_EQ((std::string) reduce([](std::string a, object b){return b+a;}, "sdrawkcab"), "backwards");
	ASSERT_EQ((std::string) reduce([](object a, std::string b){return b+a;}, "sdrawkcab"), "backwards");

	// variadic
	auto fn = proc.make_variadic_function([&proc](auto &&v) {
		static_assert(std::same_as<decltype(v), std::vector<object> &&>);
		switch(v.size()) {
			case 1: return v[0] * v[0];
			case 2: return v[1] + v[0];
			default: return proc.Ellipsis.dup(); // should not happen
		}
	});
	ASSERT_EQ(to_string(proc.list(proc.map(fn, proc.range(5)))), "[0, 1, 4, 9, 16]");
	ASSERT_EQ((std::string) reduce(fn, "sdrawkcab"), "backwards");
});

TEST("exceptions: py to cpp", {
	snaketongs::process proc;

	try {
		proc.dict()["nonexistent"];
		ASSERT(not "getitem returned");
	} catch(const snaketongs::object &exc) {
		ASSERT_EQ(exc.type().get("__name__"), "KeyError");
		ASSERT_EQ((std::string) exc.repr(), "KeyError('nonexistent')");
	}

	ASSERT_EQ((std::string) proc.into_object("ok"), "ok");
});

TEST("exceptions: cpp to py to cpp", {
	snaketongs::process proc;

	struct local_exc {
		int value;
	};

	try {
		proc.list(proc.map([&](auto){throw local_exc{42};}, "chars"));
		ASSERT(not "list(map) returned");
	} catch(const local_exc &exc) {
		ASSERT_EQ(exc.value, 42);
	}
});

TEST("exceptions: cpp obj to py to cpp", {
	snaketongs::process proc;

	try {
		proc.list(proc.map([&](auto){throw proc["builtins.KeyError"]("manual");}, "chars"));
		ASSERT(not "list(map) returned");
	} catch(const snaketongs::object &exc) {
		ASSERT_EQ(exc.type().get("__name__"), "KeyError");
		ASSERT_EQ((std::string) exc.repr(), "KeyError('manual')");
	}
});

TEST("exceptions: cpp to py", {
	snaketongs::process proc;

	auto globals = proc.dict();
	proc["builtins.exec"](proc["textwrap.dedent"](R"(
		def catch_and_return(fn):
			try:
				fn()
			except BaseException as e:
				return e
	)"), globals);
	auto catch_and_return = globals["catch_and_return"];

	auto e = catch_and_return([] {
		throw std::exception();
	});
	ASSERT_EQ(e.type().get("__name__"), "CppException");
});

TEST("exceptions: cpp obj to py", {
	snaketongs::process proc;

	auto globals = proc.dict();
	proc["builtins.exec"](proc["textwrap.dedent"](R"(
		def catch_and_return(fn):
			try:
				fn()
			except BaseException as e:
				return e
	)"), globals);
	auto catch_and_return = globals["catch_and_return"];

	auto exc = catch_and_return([&proc] {
		throw proc["builtins.KeyError"]("manual");
	});
	ASSERT_EQ(exc.type().get("__name__"), "KeyError");
	ASSERT_EQ((std::string) exc.repr(), "KeyError('manual')");
});

TEST("exceptions: py to cpp to py", {
	snaketongs::process proc;

	auto globals = proc.dict();
	proc["builtins.exec"](proc["textwrap.dedent"](R"(
		def catch_and_return(fn):
			try:
				fn()
			except BaseException as e:
				return e
	)"), globals);
	auto catch_and_return = globals["catch_and_return"];

	auto exc = catch_and_return([&proc] {
		proc.dict()["nonexistent"];
		ASSERT(not "getitem returned");
	});
	ASSERT_EQ(exc.type().get("__name__"), "KeyError");
	ASSERT_EQ((std::string) exc.repr(), "KeyError('nonexistent')");
});

TEST("readme: intro", {
	// Start a process by creating a `snaketongs::process` object.
	// (The process will be terminated when it goes out of scope.)
	snaketongs::process proc;

	// All the following (auto) variables are of type `snaketongs::object`,
	// which is a move-only Python object reference.

	// Imports:
	auto copy = proc["shutil.copy"]; // from shutil import copy
	auto re = proc["re.*"]; // import re

	// Builtins are exposed as members of `snaketongs::process`.
	// Here we use Python's str, range, map, and sorted:
	auto bad_sorting = proc.sorted(proc.map(proc.str, proc.range(100)));
	TEST_cout() << "%s ended up 30th" % bad_sorting[30] << TEST_endl_expect("36 ended up 30th");
	TEST_cout() << "%s ended up 40th" % bad_sorting[40] << TEST_endl_expect("45 ended up 40th");

	// When calling objects' methods, we are forced to deviate from Python syntax,
	// since C++ does not support dynamic lookup by name:
	TEST_cout() << "2 ended up %ith" % bad_sorting.call("index", "2") << TEST_endl_expect("2 ended up 12th"); // means bad_sorting.index("2")

	// Similarly for attributes:
	auto complex_one = 2.71 ** proc.complex(0, 6.28);
	TEST_cout() << complex_one.get("real") << TEST_endl_expect("0.999750296521069");
	TEST_cout() << complex_one.get("imag") << TEST_endl_expect("-0.02234601991484522");

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
	for(auto TEST_i = 0; auto &elem : squares)
		TEST_cout() << elem << TEST_endl_expect(std::to_string(TEST_i*TEST_i)), TEST_i++;
});

TEST("readme: func args", {
	using snaketongs::kw;
	snaketongs::process proc;

	auto dt = proc["datetime.datetime"];
	auto log = proc["sys.stderr"];

	// TEST
	log = proc["io.StringIO"]();

	auto fields = proc.make_tuple("success", 9000, true);

	proc.print(dt.call("now"), *fields, kw("sep")=" | ", kw("file")=log); // equivalent to `print(dt.now(), *fields, sep=" | ", file=log)`

	auto log_kwargs = proc.dict(kw("sep")=" | ", kw("file")=log); // equivalent to `dict(sep=" | ", file=log)`

	proc.print(dt.call("now"), *fields, **log_kwargs); // equivalent to `print(dt.now(), *fields, **log_kwargs)`

	// TEST
	ASSERT_EQ(proc["re.sub"]("20..-..-.. ..:..:..[.].{6}", "<time>", log.call("getvalue")), (
		"<time> | success | 9000 | True\n"
		"<time> | success | 9000 | True\n"
	));
});

TEST("readme: class", {
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
	proc["http.server.HTTPServer"](proc.make_tuple("", 8000), MyHTTPRequestHandler)/*TEST .call("serve_forever")*/;
});

////////////////////////////////////////////////////////////////

std::fprintf(stderr, GLOBAL_failed ? "\n%i failed\n\n" : "\nAll passed\n\n", GLOBAL_failed);
return !!GLOBAL_failed;

} // main
