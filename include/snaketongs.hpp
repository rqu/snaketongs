#ifndef SNAKETONGS_HPP_
#define SNAKETONGS_HPP_

#include <algorithm>
#include <concepts>
#include <cstdio>
#include <exception>
#include <functional>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

// snaketongs_impl*
#include "snaketongs_subproc.h"

namespace snaketongs::detail {

///////////////////////////////
//                           //
//   general c++ utilities   //
//                           //
///////////////////////////////

// std::forward but without the annoying template parameter

#define FWD(NAME) decltype(NAME)(NAME) // only works with id-expressions!

// a simple wrapper that allows implicit conversions that are only explicit in the wrapped object

template<typename Class>
struct implicitly_convertible : Class {
	template<typename T> requires requires(Class self) { T((Class &&) self); }
	constexpr operator T() && {
		return T((Class &&)(*this));
	}

	template<typename T> requires requires(Class self) { T((const Class &) self); }
	constexpr operator T() const & {
		return T((const Class &)(*this));
	}

	template<typename T> requires requires(Class self) { T((Class &) self); }
	constexpr operator T() & {
		return T((Class &)(*this));
	}
};

template<typename Ref> requires std::is_reference_v<Ref>
class implicitly_convertible<Ref> {
	Ref ref;

	implicitly_convertible(const implicitly_convertible &) = delete;
	implicitly_convertible(implicitly_convertible &&) = delete;

public:
	constexpr explicit implicitly_convertible(Ref ref) noexcept : ref(FWD(ref)) {}

	template<typename T> requires requires { T(FWD(ref)); }
	constexpr operator T() && {
		return T(FWD(ref));
	}
};

// concepts

template<typename>
concept always_false = false;

template<typename T, template<typename...> typename TT>
struct is_specialization_ : std::false_type {};

template<template<typename...> typename TT, typename... Ts>
struct is_specialization_<TT<Ts...>, TT> : std::true_type {};

template<typename T>
concept ostream_like = requires(T &&stream, std::string s) {
	FWD(stream) << std::move(s);
};


///////////////////////////////////////////////
//                                           //
//   forward declarations and simple types   //
//                                           //
///////////////////////////////////////////////

using int_t = std::make_signed_t<std::size_t>;

static constexpr std::size_t int_size = sizeof(std::size_t);

constexpr void pack_int(int_t v, unsigned char c[int_size]) {
	for(std::size_t i = 0; i < int_size; i++)
		c[i] = (std::size_t) v >> 8*i;
}

constexpr int_t unpack_int(unsigned char c[int_size]) {
	std::size_t v = 0;
	for(std::size_t i = 0; i < int_size; i++)
		v |= (std::size_t) c[i] << 8*i;
	return (int_t) v;
}

class object;

struct cpp_wrapped_py_exc;

template<typename, typename>
class process_t;

using process = process_t<object, cpp_wrapped_py_exc>;

struct python_iterator;

// utilities

template<typename = std::size_t>
struct args_builder;

template<typename = std::size_t>
struct args_kwargs_builder;

template<typename F, std::size_t MaxArity>
class functor_wrapper;

// simple types

struct raw_object {
	int_t remote_idx;
};

struct io_error : std::runtime_error {
	using std::runtime_error::runtime_error;
};


//////////////////////////////////////////
//                                      //
//   tongs-specific concepts - part 1   //
//                                      //
//////////////////////////////////////////

// value that can be converted to a python object - pythonized

template<typename T, typename process = process>
concept pythonizable = requires(process &proc, T &&t) {
	proc.into_object(FWD(t));
};

template<typename T>
concept pythonizable_non_object = !std::is_convertible_v<T, const object &> && pythonizable<T>;

template<typename T>
concept pythonizable_or_void = std::same_as<T, void> || pythonizable<T>;

// pythonizable_fn - function (or functor) that can be pythonized

template<typename...>
struct pythonizable_fn_impl {
	static_assert(always_false<pythonizable_fn_impl>);
};
template<typename object, typename F, std::size_t... I>
struct pythonizable_fn_impl<object, F, std::index_sequence<I...>> {
	static constexpr bool value = requires(std::remove_cvref_t<F> f, F &&ref, implicitly_convertible<object> arg) {
		{f(std::move((I, arg))...)} -> pythonizable_or_void;
		decltype(f)(ref);
	};
};
template<typename F, std::size_t Arity>
concept pythonizable_fn = !std::is_convertible_v<F, const object &> && pythonizable_fn_impl<object, F, std::make_index_sequence<Arity>>::value;

// vector-taking function (or functor) that can be pythonized

template<typename F>
concept pythonizable_vec_fn = requires(std::remove_cvref_t<F> f, F &&ref) {
	{f(std::vector<object>())} -> pythonizable_or_void;
	decltype(f)(ref);
};

// value that has implicit conversion to std::span<const std::byte>

template<typename T>
concept bytes_like = !std::same_as<std::remove_cvref_t<T>, object> && requires(T &&t, void f(std::span<const std::byte>)) {
	f(FWD(t));
};

// types that can possibly be used to store strings - string_view on c++ side, object on python side

template<typename T>
concept string_or_object = std::same_as<T, std::string_view> || std::same_as<T, const object &>;


///////////////////////////////////////////////////
//                                               //
//   rvalue-only wrapppers for syntactic hacks   //
//                                               //
///////////////////////////////////////////////////

// the result of *star_object, or **object

struct star_star_object {
	const object &obj;
};

// the result of *object

struct star_object {
	const object &obj;
	constexpr star_star_object operator *() && {
		return star_star_object(obj);
	}
};

// the result of `kw(key) = value`

template<string_or_object K, pythonizable V>
struct kw_arg {
	K key;
	V value;
};

// the result of `kw(key)` - obviously

template<string_or_object K>
struct kw {
	K key;
	constexpr explicit kw(K key) noexcept : key(key) {}

	template<pythonizable V>
	constexpr kw_arg<K, V&&> operator=(V &&value) && {
		return {key, FWD(value)};
	}
};

kw(std::string_view) -> kw<std::string_view>;
kw(const object &) -> kw<const object &>;


//////////////////////////////////////////
//                                      //
//   tongs-specific concepts - part 2   //
//                                      //
//////////////////////////////////////////

template<typename T>
concept valid_item = pythonizable<T> || std::same_as<T, star_object>;

template<typename T>
concept valid_arg = valid_item<T> || std::same_as<T, star_star_object> || is_specialization_<T, kw_arg>::value;

template<typename T>
concept special_arg = std::same_as<T, star_object> || std::same_as<T, star_star_object> || is_specialization_<T, kw_arg>::value;

template<typename... Ts>
static constexpr bool none_is_special = !(... || special_arg<std::remove_cvref_t<Ts>>);


/////////////////
//             //
//   process   //
//             //
/////////////////

class process_base {
	struct snaketongs_impl *impl;

public:
	process_base() {
		impl = snaketongs_impl_start(int_size);
		if(!impl)
			throw io_error("Cannot start subprocess");
	}
	process_base(const process_base &) = delete;
	void operator=(const process_base &) = delete;

	void send(const void *src, size_t size) {
		if(!snaketongs_impl_send(impl, src, size))
			throw io_error("Cannot send data to subprocess");
	}
	void flush() {
		if(!snaketongs_impl_flush(impl))
			throw io_error("Cannot send data to subprocess");
	}
	void recv(void *dest, size_t size) {
		if(!snaketongs_impl_recv(impl, dest, size))
			throw io_error("Cannot receive data from subprocess");
	}
	void quit() {
		auto i = impl;
		impl = nullptr;
		if(!snaketongs_impl_quit(i))
			throw io_error("Subprocess did not quit cleanly");
	}

	bool terminated() {
		return !impl;
	}

	~process_base() {
		// this->quit() may or may not have been called
		if(!terminated())
			snaketongs_impl_quit(impl);
	}
};

#define object object_ // allows most of the code in class process to refer to `object` as `object` while also having public field `object`

template<typename object, typename cpp_wrapped_py_exc> // fake template to allow forward references
class process_t : process_base {
	// plain type alias for the current template instance - in addition to disambiguating the syntax,
	// it also allows us to refer to `process` in the same way as the rest of the program
	using process = process_t;

	// makes some references easier and again makes usage somewhat consistent
	process &proc = *this;

	// O(1) allocation and deallocation of indices for c++ objects shared to python (on top of O(1) amortized vector)
	struct free_list_entry {
		std::size_t next_free;
		constexpr free_list_entry() noexcept : next_free(~0) {};
		constexpr std::size_t any() {
			return ~next_free;
		}
	};
	using py_to_cpp_ptr_t = std::variant<
		free_list_entry,
		std::function<void(process &, size_t, const raw_object *)>,
		std::exception_ptr
	>;
	std::vector<py_to_cpp_ptr_t> py_to_cpp_ptrs;
	free_list_entry py_to_cpp_ptrs_free_list;

	// (more data members at the end of the class)

	// python to c++

	int_t recv_int() {
		unsigned char data[int_size];
		recv(data, sizeof data);
		return unpack_int(data);
	}

	object wait_for_object() {
		return cook({wait_for_ret()});
	}

	int_t wait_for_ret() {
		for(;;) {
			flush();
			unsigned char data[1 + int_size];
			recv(data, sizeof data);
			int_t arg = unpack_int(data + 1);
			switch(data[0]) {
				case 'c':
					handle_call(arg);
					continue;
				case '~':
					handle_del(arg);
					continue;
				case 'r':
					return arg;
				case 'e':
					rethrow_exc({arg});
					// noreturn
				default:
					throw io_error("Subprocess returned invalid command");
			}
		}
	}

	void handle_call(int_t ptr_idx) {
		auto &fn = std::get<1>(py_to_cpp_ptrs[ptr_idx]);
		int_t num_args = recv_int();
		auto args = std::make_unique_for_overwrite<raw_object[]>(num_args);
		for(int_t i = 0; i < num_args; i++)
			args[i] = {recv_int()};
		try {
			// pass ownership of each arg, but not the args array itself
			fn(*this, num_args, args.get());
		} catch(const object &exc) {
			cmd_exc(exc);
			return;
		} catch(...) {
			cmd_exc(py_wrapped_cpp_exc(cmd_make_remote(std::current_exception())));
			return;
		}
	}

	void handle_del(int_t ptr_idx) {
		// push onto free list
		py_to_cpp_ptrs[ptr_idx] = py_to_cpp_ptrs_free_list;
		py_to_cpp_ptrs_free_list.next_free = ptr_idx;
	}

	[[noreturn]] void rethrow_exc(raw_object raw_exc_obj) {
		object exc_obj = cook(raw_exc_obj);
		if(exc_obj.type().is(py_wrapped_cpp_exc)) {
			// python wrapped cpp exception => unwrap
			int_t ptr_idx = exc_obj.getattr("args").getitem(0).getattr("remote_idx").conv();
			const auto &wrapped = std::get<2>(py_to_cpp_ptrs[ptr_idx]);
			std::rethrow_exception(wrapped);
		} else {
			// other python exception => wrap
			throw cpp_wrapped_py_exc(std::move(exc_obj));
		}
	}

	// c++ to python
	
	enum class cmd : unsigned char {
		make_int    = 'I',
		make_bytes  = 'B',
		make_str    = 'S',
		make_tuple  = 'T',
		make_global = 'G',
		make_remote = 'R',
		call        = 'C',
		starcall    = 'X',
		lambda      = 'L',
		dup         = 'D',
		get_int     = 'i',
		get_bytes   = 'b',
		del_ptr     = '~',
		ret         = 'r',
		exc         = 'e',
	};

	void send_int(int_t i) {
		unsigned char data[int_size];
		pack_int(i, data);
		send(data, sizeof data);
	}

	void send_object(raw_object obj) {
		send_int(obj.remote_idx);
	}

	void send_cmd(cmd c, int_t i) {
		unsigned char data[1 + int_size] = {(unsigned char) c};
		pack_int(i, data + 1);
		send(data, sizeof data);
	}

	void send_cmd(cmd c, raw_object obj) {
		send_cmd(c, obj.remote_idx);
	}

	// c++ to python - commands

	object cmd_make_int(int_t value) {
		send_cmd(cmd::make_int, value);
		return wait_for_object();
	}

	object cmd_make_bytes(size_t size, const std::byte *data) {
		send_cmd(cmd::make_bytes, size);
		send(data, size);
		return wait_for_object();
	}

	object cmd_make_str(size_t size, const char *data) {
		send_cmd(cmd::make_str, size);
		send(data, size);
		return wait_for_object();
	}

	object cmd_make_tuple(std::initializer_list<raw_object> items) {
		send_cmd(cmd::make_tuple, items.size());
		for(raw_object item : items)
			send_object(item);
		return wait_for_object();
	}

	object cmd_make_global(std::string_view qualname) {
		send_cmd(cmd::make_global, qualname.size());
		send(qualname.data(), qualname.size());
		return wait_for_object();
	}

	object cmd_make_remote(py_to_cpp_ptr_t &&ptr) {
		std::size_t ptr_idx;
		if(py_to_cpp_ptrs_free_list.any()) {
			// pop from free list
			ptr_idx = py_to_cpp_ptrs_free_list.next_free;
			py_to_cpp_ptrs_free_list = std::get<free_list_entry>(py_to_cpp_ptrs[ptr_idx]);
			py_to_cpp_ptrs[ptr_idx] = FWD(ptr);
		} else {
			ptr_idx = py_to_cpp_ptrs.size();
			py_to_cpp_ptrs.push_back(FWD(ptr));
		}
		send_cmd(cmd::make_remote, ptr_idx);
		return wait_for_object();
	}

	object cmd_call(raw_object fn, std::initializer_list<raw_object> args) {
		send_cmd(cmd::call, args.size());
		send_object(fn);
		for(raw_object arg : args)
			send_object(arg);
		return wait_for_object();
	}

	object cmd_starcall(raw_object fn, raw_object args, raw_object kwargs) {
		send_cmd(cmd::starcall, -1);
		send_object(fn);
		send_object(args);
		send_object(kwargs);
		return wait_for_object();
	}

	object cmd_lambda(const object &obj) {
		send_cmd(cmd::lambda, obj.raw);
		return wait_for_object();
	}

	object cmd_dup(raw_object obj) {
		send_cmd(cmd::dup, obj);
		return wait_for_object();
	}

	int_t cmd_get_int(raw_object obj) {
		send_cmd(cmd::get_int, obj);
		return wait_for_ret();
	}

	template<typename Container, auto... Exprs>
	Container cmd_get_bytes(raw_object obj) {
		send_cmd(cmd::get_bytes, obj);
		int_t size = wait_for_ret();
		auto result = Container(size, Exprs...);
		recv(result.data(), size);
		return result;
	}

	void cmd_del_ptr(raw_object obj) {
		send_cmd(cmd::del_ptr, obj);
	}

	void cmd_ret(const object &obj) {
		send_cmd(cmd::ret, obj.raw);
	}
	void cmd_ret_from_main_loop() {
		send_cmd(cmd::ret, 0xD1E'A112EAD1);
	}

	void cmd_exc(const object &obj) {
		send_cmd(cmd::exc, obj.raw);
	}

	// raw_object to object

	object cook(raw_object obj) {
		return object(this, obj);
	}
	implicitly_convertible<object> cook_implicit(raw_object obj) {
		return {object(this, obj)};
	}

	friend object;
	template<typename F, std::size_t MaxArity>
	friend class functor_wrapper;

public:
	// process management

	void terminate() {
		cmd_ret_from_main_loop();
		quit();
		py_to_cpp_ptrs.clear();
	}

	using process_base::terminated;

	~process_t() {
		if(!terminated()) {
			// terminate(), but we must not fail
			try {
				cmd_ret_from_main_loop();
			} catch(const io_error &) {}
			try {
				quit(); // sets terminated() to true even on error
			} catch(const io_error &) {}
		}
		// now the default dtor:
		// - kills the canary, making expired() lambdas return true
		// - drops all builtins etc. - this is a noop, since their `proc->terminated()` is true
		// - releases py_to_cpp_ptrs
		//   - this may involve lambda dtors, which may involve more objects noop-dropping
		// - calls base dtor - noop, since terminated() is true
	}

	auto expired() const noexcept {
		return [weak_ptr = std::weak_ptr(canary)] {
			return weak_ptr.expired();
		};
	}

	// implicit conversions to object

	object into_object(int_t value) {
		return cmd_make_int(value);
	}

	object into_object(std::floating_point auto value) {
		if constexpr(std::same_as<decltype(value), double>) {
			// the only floating point type known by python
			char str[sizeof value * 2 + sizeof "+0x0.0p+0"];
			if((std::size_t) std::snprintf(str, sizeof str, "%la", value) >= sizeof str)
				std::terminate();
			return float_.call("fromhex", str);
		} else {
			// do not emit superfluous almost-duplicates
			return into_object((double) value);
		}
	}

	object into_object(bytes_like auto &&bytes) {
		std::span<const std::byte> span = FWD(bytes);
		return cmd_make_bytes(span.size(), span.data());
	}

	object into_object(std::string_view str) {
		return cmd_make_str(str.size(), str.data());
	}

	const object &into_object(std::same_as<bool> auto value) {
		return value ? True : False;
	}
	const object &into_object(std::true_type) {
		return True;
	}
	const object &into_object(std::false_type) {
		return False;
	}

	object into_object(pythonizable_fn<0> auto &&f) {
		return make_function<0>(f);
	}
	object into_object(pythonizable_fn<1> auto &&f) {
		return make_function<1>(f);
	}
	object into_object(pythonizable_fn<2> auto &&f) {
		return make_function<2>(f);
	}
	object into_object(pythonizable_fn<3> auto &&f) {
		return make_function<3>(f);
	}
	object into_object(pythonizable_fn<4> auto &&f) {
		return make_function<4>(f);
	}
	object into_object(pythonizable_fn<5> auto &&f) {
		return make_function<5>(f);
	}
	object into_object(pythonizable_fn<6> auto &&f) {
		return make_function<6>(f);
	}
	object into_object(pythonizable_fn<7> auto &&f) {
		return make_function<7>(f);
	}

	const object &into_object(const object &already_object) {
		if(already_object.proc != this)
			throw std::invalid_argument("Cannot share objects across process instances");
		return already_object;
	}

	// explicit functions for obtaining python objects

	object operator[](std::string_view qualname) {
		return cmd_make_global(qualname);
	}

	object make_tuple(valid_item auto &&... items) {
		if constexpr(none_is_special<decltype(items)...>)
			return cmd_make_tuple({into_object(items).raw...});
		else
			return tuple(make_list(FWD(items)...));
	}

	object make_list(valid_item auto &&... items) {
		args_builder<decltype(sizeof...(items))> b = {proc};
		(..., b.add(FWD(items)));
		return std::move(b.args);
	}

	template<std::size_t MaxArity, pythonizable_fn<MaxArity> F>
	object make_function(F &&f) {
		return cmd_lambda(cmd_make_remote(functor_wrapper<std::remove_cvref_t<F>, MaxArity>(FWD(f))));
	}
	object make_variadic_function(pythonizable_vec_fn auto &&f) {
		return cmd_lambda(cmd_make_remote([f = FWD(f)](process &proc, size_t num_args, const raw_object *args) {
			std::vector<object> vec;
			vec.reserve(num_args);
			for(size_t i = 0; i < num_args; i++)
				vec.push_back(proc.cook(args[i]));
			if constexpr(std::same_as<decltype(f(std::move(vec))), void>) {
				f(std::move(vec));
				proc.cmd_ret(proc.None);
			} else {
				proc.cmd_ret(proc.into_object(f(std::move(vec))));
			}
		}));
	}

	// python builtins

#define SNAKETONGS_BUILTIN(N) const object_ N = proc["builtins." #N]
#define SNAKETONGS_BUILTINS(A, B, C, D, E, F, G, H) \
	SNAKETONGS_BUILTIN(A); SNAKETONGS_BUILTIN(B); SNAKETONGS_BUILTIN(C); SNAKETONGS_BUILTIN(D); \
	SNAKETONGS_BUILTIN(E); SNAKETONGS_BUILTIN(F); SNAKETONGS_BUILTIN(G); SNAKETONGS_BUILTIN(H)

	// the 5 global constants, and 3 exception classes we need anyway
	SNAKETONGS_BUILTINS(None, True, False, Ellipsis, NotImplemented, BaseException, StopIteration, TypeError);

#undef object
	SNAKETONGS_BUILTIN(object);
#define object object_

	// https://docs.python.org/3.11/library/functions.html
	SNAKETONGS_BUILTINS(abs, aiter, all, anext, any, ascii, bin, bytearray);
	SNAKETONGS_BUILTINS(bytes, callable, chr, classmethod, complex, delattr, dict, dir);
	SNAKETONGS_BUILTINS(divmod, enumerate, filter, format, frozenset, getattr, hasattr, hash);
	SNAKETONGS_BUILTINS(hex, id, input, isinstance, issubclass, iter, len, list);
	SNAKETONGS_BUILTINS(map, max, memoryview, min, next, oct, open, ord);
	SNAKETONGS_BUILTINS(pow, print, property, range, repr, reversed, round, set);
	SNAKETONGS_BUILTINS(setattr, slice, sorted, staticmethod, str, sum, tuple, type);
	SNAKETONGS_BUILTIN(zip);
	const object bool_ = proc["builtins.bool"];
	const object float_ = proc["builtins.float"];
	const object int_ = proc["builtins.int"];

#undef SNAKETONGS_BUILTINS
#undef SNAKETONGS_BUILTIN

	// https://docs.python.org/3/library/operator.html

	const object op_contains = proc["operator.contains"];
	const object op_getitem = proc["operator.getitem"];
	const object op_setitem = proc["operator.setitem"];
	const object op_delitem = proc["operator.delitem"];

	const object op_lt = proc["operator.lt"];
	const object op_le = proc["operator.le"];
	const object op_eq = proc["operator.eq"];
	const object op_ne = proc["operator.ne"];
	const object op_ge = proc["operator.ge"];
	const object op_gt = proc["operator.gt"];

	const object op_not = proc["operator.not_"];
	const object op_is = proc["operator.is_"];
	const object op_is_not = proc["operator.is_not"];

	const object op_inv = proc["operator.inv"];
	const object op_neg = proc["operator.neg"];
	const object op_pos = proc["operator.pos"];

	const object op_add = proc["operator.add"];
	const object op_and = proc["operator.and_"];
	const object op_floordiv = proc["operator.floordiv"]; // non-operator
	const object op_lshift = proc["operator.lshift"];
	const object op_mod = proc["operator.mod"];
	const object op_mul = proc["operator.mul"];
	const object op_matmul = proc["operator.matmul"]; // non-operator
	const object op_or = proc["operator.or_"];
	const object op_pow = proc["operator.pow"]; // non-operator, hacked as a * *b
	const object op_rshift = proc["operator.rshift"];
	const object op_sub = proc["operator.sub"];
	const object op_truediv = proc["operator.truediv"];
	const object op_xor = proc["operator.xor"];

	const object op_iadd = proc["operator.iadd"];
	const object op_iand = proc["operator.iand"];
	const object op_ifloordiv = proc["operator.ifloordiv"]; // non-operator
	const object op_ilshift = proc["operator.ilshift"];
	const object op_imod = proc["operator.imod"];
	const object op_imul = proc["operator.imul"];
	const object op_imatmul = proc["operator.imatmul"]; // non-operator
	const object op_ior = proc["operator.ior"];
	const object op_ipow = proc["operator.ipow"]; // non-operator, hacked as a * *b
	const object op_irshift = proc["operator.irshift"];
	const object op_isub = proc["operator.isub"];
	const object op_itruediv = proc["operator.itruediv"];
	const object op_ixor = proc["operator.ixor"];

private:
	const object py_wrapped_cpp_exc = type("CppException", make_tuple(BaseException), dict());

	// the first subobject to be destroyed, making all expired() lambdas return true
	const std::shared_ptr<std::monostate> canary = std::make_shared<std::monostate>();
};

#undef object


////////////////
//            //
//   object   //
//            //
////////////////

#define SNAKETONGS_GENERATE_BIN_OPS() \
	SNAKETONGS_BIN_OP(<,  lt) \
	SNAKETONGS_BIN_OP(<=, le) \
	SNAKETONGS_BIN_OP(==, eq) \
	SNAKETONGS_BIN_OP(!=, ne) \
	SNAKETONGS_BIN_OP(>=, ge) \
	SNAKETONGS_BIN_OP(>,  gt) \
	SNAKETONGS_BIN_OP_I(+, add) \
	SNAKETONGS_BIN_OP_I(&, and) \
	SNAKETONGS_BIN_OP_N(floordiv) \
	SNAKETONGS_BIN_OP_I(<<, lshift) \
	SNAKETONGS_BIN_OP_I(%, mod) \
	SNAKETONGS_BIN_OP_I(*, mul) \
	SNAKETONGS_BIN_OP_N(matmul) \
	SNAKETONGS_BIN_OP_I(|, or) \
	SNAKETONGS_BIN_OP_N(pow) \
	SNAKETONGS_BIN_OP_I(>>, rshift) \
	SNAKETONGS_BIN_OP_I(-, sub) \
	SNAKETONGS_BIN_OP_I(/, truediv) \
	SNAKETONGS_BIN_OP_I(^, xor)

class object {
	process *proc;
	raw_object raw;

	constexpr explicit object(process *proc, raw_object raw) noexcept : proc(proc), raw(raw) {}

	constexpr void drop() {
		if(proc && !proc->terminated())
			proc->cmd_del_ptr(raw);
	}

	template<pythonizable SlotT>
	class lvalue {
		static_assert(std::is_reference_v<SlotT>);

		const object &obj;
		SlotT slot; // name for attributes, index for items
		const object &fn_has;
		const object &fn_get;
		const object &fn_set;
		const object &fn_del;

		explicit constexpr lvalue(const object &obj, SlotT slot, const object &has, const object &get, const object &set, const object &del)
			: obj(obj), slot(FWD(slot)), fn_has(has), fn_get(get), fn_set(set), fn_del(del) {}
		lvalue(const lvalue &) = delete;

		friend object;

	public:
		bool present() && {
			return (bool) fn_has(obj, FWD(slot));
		}
		object get() && {
			return fn_get(obj, FWD(slot));
		}
		void set(pythonizable auto &&value) && {
			fn_set(obj, FWD(slot), FWD(value));
		}
		void del() && {
			fn_del(obj, FWD(slot));
		}
		object update(auto &&f) && requires requires(object o) {{f(o)} -> std::same_as<void>;} {
			const object &slot_obj = obj.proc->into_object(FWD(slot));
			object value = fn_get(obj, slot_obj);
			f(value);
			fn_set(obj, slot_obj, value);
			return value;
		}

		object operator=(pythonizable auto &&rhs) && {
			object value = FWD(rhs).dup();
			std::move(*this).set(value);
			return value;
		}

#define SNAKETONGS_BIN_OP(OP, NAME)
#define SNAKETONGS_BIN_OP_I(OP, NAME) \
		object operator OP##=(pythonizable auto &&rhs) && { \
			return std::move(*this).update([&rhs](object &lhs){ lhs OP##= FWD(rhs); }); \
		}
#define SNAKETONGS_BIN_OP_N(NAME) \
		object i##NAME(pythonizable auto &&rhs) && { \
			return std::move(*this).update([&rhs](object &lhs){ lhs.i##NAME(FWD(rhs)); }); \
		}
		SNAKETONGS_GENERATE_BIN_OPS()
#undef SNAKETONGS_BIN_OP
#undef SNAKETONGS_BIN_OP_I
#undef SNAKETONGS_BIN_OP_N
	};

	friend process;
	friend struct checked_dtor_object;

public:
	// from-python conversions

	template<std::integral T>
	explicit operator T() const {
		return proc->cmd_get_int(raw);
	}
	explicit operator std::vector<char>() const {
		return proc->cmd_get_bytes<std::vector<char>, char(0)>(raw);
	}
	explicit operator std::string() const {
		return proc->cmd_get_bytes<std::string, '\0'>(raw);
	}
	explicit operator double() const {
		double d;
		if(std::sscanf(std::string(proc->float_.call("hex", *this)).c_str(), "%la", &d) != 1)
			throw io_error("float.hex() returned invalid string");
		return d;
	}
	explicit operator float() const {
		return this->operator double();
	}
	explicit operator long double() const {
		return this->operator double();
	}

	explicit operator bool() const {
		return proc->bool_(*this).operator int_t();
	}

	constexpr implicitly_convertible<const object &> conv() const & {
		return implicitly_convertible<const object &>(*this);
	}

	// python operators and methods
	// https://docs.python.org/3.11/reference/datamodel.html#basic-customization

	object operator()(valid_arg auto &&... args) const {
		if constexpr(none_is_special<decltype(args)...>) {
			return proc->cmd_call(raw, {proc->into_object(FWD(args)).raw...});
		} else {
			args_kwargs_builder<decltype(sizeof...(args))> b = {{*proc}};
			(..., b.add(FWD(args)));
			return proc->cmd_starcall(raw, b.args.raw, b.kwargs.raw);
		}
	}

	object repr() const {
		return proc->repr(*this);
	}
	object str() const {
		return proc->str(*this);
	}
	object bytes() const {
		return proc->bytes(*this);
	}
	object format(std::string_view fmt = "") const {
		return proc->format(*this, fmt);
	}
	object format(const object &fmt) const {
		return proc->format(*this, fmt);
	}
	int_t hash() const {
		return (int_t) proc->hash(*this);
	}

	int_t len() const {
		return (int_t) proc->len(*this);
	}
	object iter() const {
		return proc->iter(*this);
	}
	object next() const {
		return proc->next(*this);
	}

	object type() const {
		return proc->type(*this);
	}

	auto attr(pythonizable auto &&name) const -> lvalue<decltype(name)> {
		return lvalue<decltype(name)>(*this, FWD(name), proc->hasattr, proc->getattr, proc->setattr, proc->delattr);
	}
	bool hasattr(pythonizable auto &&name) const {
		return attr(FWD(name)).present();
	}
	object getattr(pythonizable auto &&name) const {
		return attr(FWD(name)).get();
	}
	void setattr(pythonizable auto &&name, pythonizable auto &&value) const {
		attr(FWD(name)).set(FWD(value));
	}
	void delattr(pythonizable auto &&name) const {
		attr(FWD(name)).del();
	}

	auto item(pythonizable auto &&index) const -> lvalue<decltype(index)> {
		return lvalue<decltype(index)>(*this, FWD(index), proc->op_contains, proc->op_getitem, proc->op_setitem, proc->op_delitem);
	}
	bool contains(pythonizable auto &&index) const {
		return item(FWD(index)).present();
	}
	object getitem(pythonizable auto &&index) const {
		return item(FWD(index)).get();
	}
	void setitem(pythonizable auto &&index, pythonizable auto &&value) const {
		item(FWD(index)).set(FWD(value));
	}
	void delitem(pythonizable auto &&index) const {
		item(FWD(index)).del();
	}

	// shortcuts for the above

	object operator[](pythonizable auto &&index) const {
		return item(FWD(index)).get();
	}
	object get(std::string_view name) const {
		return attr(name).get();
	}
	void set(std::string_view name, pythonizable auto &&value) const {
		return attr(name).set(FWD(value));
	}
	object call(std::string_view name, valid_arg auto &&... args) const {
		return attr(name).get()(FWD(args)...);
	}

	bool is(const object &other) const {
		return (bool) proc->op_is(*this, other);
	}
	bool is_not(const object &other) const {
		return (bool) proc->op_is_not(*this, other);
	}

	bool in(pythonizable auto &&other) const {
		return (bool) proc->op_contains(FWD(other), *this);
	}
	bool not_in(pythonizable auto &&other) const {
		return !in(FWD(other));
	}

	// operators

	object operator~() const {
		return proc->op_inv(*this);
	}
	object operator-() const {
		return proc->op_neg(*this);
	}
	object operator+() const {
		return proc->op_pos(*this);
	}

#define SNAKETONGS_BIN_OP(OP, NAME) \
	friend object operator OP(const object &lhs, const object &rhs) { \
		process *proc = lhs.proc; \
		return proc->op_##NAME(FWD(lhs), FWD(rhs)); \
	} \
	friend object operator OP(const object &lhs, pythonizable_non_object auto &&rhs) { \
		process *proc = lhs.proc; \
		return proc->op_##NAME(FWD(lhs), FWD(rhs)); \
	} \
	friend object operator OP(pythonizable_non_object auto &&lhs, const object &rhs) { \
		process *proc = rhs.proc; \
		return proc->op_##NAME(FWD(lhs), FWD(rhs)); \
	}
#define SNAKETONGS_BIN_OP_I(OP, NAME) \
	SNAKETONGS_BIN_OP(OP, NAME) \
	object &operator OP##=(pythonizable auto &&rhs) & { \
		return *this = proc->op_i##NAME(*this, FWD(rhs)); \
	}
#define SNAKETONGS_BIN_OP_N(NAME) \
	object NAME(pythonizable auto &&rhs) const { \
		return proc->op_##NAME(*this, FWD(rhs)); \
	} \
	object &i##NAME(pythonizable auto &&rhs) & { \
		return *this = proc->op_i##NAME(*this, FWD(rhs)); \
	}
	SNAKETONGS_GENERATE_BIN_OPS()
#undef SNAKETONGS_BIN_OP
#undef SNAKETONGS_BIN_OP_I
#undef SNAKETONGS_BIN_OP_N

	constexpr star_object operator*() const {
		return {*this};
	}

	// c++ interoperability

	inline python_iterator begin() const;
	constexpr python_iterator end() const;
	friend std::string to_string(const object &obj) {
		return (std::string) obj.str();
	}
	friend decltype(auto) operator<<(ostream_like auto &&stream, const object &obj) {
		return FWD(stream) << to_string(obj);
	}

	// explicit pointer copy

	object dup() const & {
		return proc->cmd_dup(raw);
	}
	constexpr object &&dup() && {
		return std::move(*this);
	}

	// boring stuff

	constexpr explicit object(std::nullptr_t) noexcept : proc(nullptr) {}
	constexpr object(object &&from) noexcept : proc(from.proc), raw(from.raw) {
		from.proc = nullptr;
	}
	object(const object &) = delete;
	void operator=(const object &) = delete;
	constexpr object &operator=(object &&from) & {
		if(&from == this)
			return *this;
		drop();
		proc = from.proc;
		raw = from.raw;
		from.proc = nullptr;
		return *this;
	}
	constexpr void operator=(std::nullptr_t) & {
		drop();
		proc = nullptr;
	}

	constexpr bool is_nullptr() const {
		return !proc;
	}
	constexpr process &get_process() const {
		return *proc;
	}

	constexpr ~object() {
		try {
			drop();
		} catch(const io_error &) {}
	}

	friend constexpr void swap(object &a, object &b) noexcept {
		std::swap(a.proc, b.proc);
		std::swap(a.raw, b.raw);
	}
};

#undef GENERATE_BIN_OPS

struct checked_dtor_object : object {
private:
	using proc_expired_t = decltype(proc->expired());
	const proc_expired_t proc_expired;

	// construct an already expired (assuming proc_expired() == true)
	checked_dtor_object(const proc_expired_t &proc_expired) noexcept : object(nullptr), proc_expired(proc_expired) {}

public:
	// construct non-expired from base
	checked_dtor_object(object &&orig) noexcept : object(FWD(orig)), proc_expired(proc->expired()) {}

	// move ctor
	checked_dtor_object(checked_dtor_object &&) noexcept = default;

	checked_dtor_object dup() const & {
		if(proc_expired() || proc->terminated())
			return checked_dtor_object(proc_expired);
		else
			return checked_dtor_object(object::dup());
	}
	constexpr checked_dtor_object &&dup() && {
		return std::move(*this);
	}

	~checked_dtor_object() {
		if(proc_expired())
			proc = nullptr;
	}
};


////////////////////////////////////
//                                //
//   remaining parts of the api   //
//                                //
////////////////////////////////////

// object * star_object = object ** object = object.pow(object)

object operator *(pythonizable auto &&lhs, std::same_as<star_object> auto &&star_rhs) {
	const object &rhs = star_rhs.obj;
	process &proc = rhs.get_process();
	return proc.into_object(lhs).pow(rhs);
}

// object.begin(), object.end()

struct python_iterator {
	using difference_type = int;
	using value_type = object;

	object iter_object;

private:
	mutable object current;
	object next() {
		try {
			return iter_object.next();
		} catch(const object &exc) {
			process &proc = exc.get_process();
			if(proc.isinstance(exc, proc.StopIteration))
				return (object) nullptr;
			else
				throw;
		}
	}

public:
	python_iterator(object &&iter_object) : iter_object(FWD(iter_object)), current(this->iter_object.next()) {}
	constexpr python_iterator() noexcept : iter_object(nullptr), current(nullptr) {}

	constexpr const object &operator*() const {
		return current;
	}
	constexpr const object *operator->() const {
		return &current;
	}
	python_iterator &operator++() { // prefix
		current = next();
		return *this;
	}
	void operator++(int) { // postfix
		current = next();
	}
	friend constexpr bool operator==(const python_iterator &a, const python_iterator &b) {
		return a.current.is_nullptr() == b.current.is_nullptr();
	}
	friend constexpr bool operator!=(const python_iterator &a, const python_iterator &b) {
		return a.current.is_nullptr() != b.current.is_nullptr();
	}
};

static_assert(std::input_iterator<python_iterator>);

inline python_iterator object::begin() const {
	return python_iterator(iter());
}

constexpr python_iterator object::end() const {
	return python_iterator();
}

// wrapper for python exceptions - always optional, and always used by snaketongs itself

struct cpp_wrapped_py_exc final : std::exception, checked_dtor_object {
	const std::string msg;

	// the main ctor - wrapping object in an exception
	cpp_wrapped_py_exc(object &&obj) : checked_dtor_object(FWD(obj)), msg(repr()) {}

	// copy ctor needlessly dictated by the standard
	[[deprecated(
		"Exception copy constructor used"
		" - suppress this by using the move constructor or .dup() instead"
		" - if you see this for a 'throw cpp_wrapped_py_exc(...)', consider using a better compiler"
	)]]
	cpp_wrapped_py_exc(const cpp_wrapped_py_exc &orig) : cpp_wrapped_py_exc(orig.dup()) {} // dup and delegate to move below

	// move ctor would be deleted because of the previous
	cpp_wrapped_py_exc(cpp_wrapped_py_exc &&) noexcept = default;

	const char* what() const noexcept override {
		return msg.c_str();
	}

	cpp_wrapped_py_exc dup() const & {
		return {*this, msg};
	}
	constexpr cpp_wrapped_py_exc &&dup() && {
		return std::move(*this);
	}

private: // for use by .dup()
	cpp_wrapped_py_exc(const checked_dtor_object &obj, const std::string &msg) : checked_dtor_object(obj.dup()), msg(msg) {}
};


////////////////////////////////////////////////
//                                            //
//   utilities that could not be made local   //
//                                            //
////////////////////////////////////////////////

template<typename>
struct args_builder {
	process &proc;
	object args = proc.list();
	void add(pythonizable auto &&plain) {
		args.call("append", FWD(plain));
	}
	void add(star_object &&iterable) {
		args.call("extend", iterable.obj);
	}
};

template<typename>
struct args_kwargs_builder : args_builder<> {
	object kwargs = proc.dict();
	using args_builder<>::add;
	template<typename K, typename V>
	void add(kw_arg<K, V> &&kwarg) {
		kwargs.setitem(kwarg.key, FWD(kwarg.value));
	}
	void add(star_star_object &&keyable) {
		kwargs.call("update", keyable.obj);
	}
};

// user lambdas may have various argument types and return types due to implicit conversions to/from object;
// this wrapper unifies the types to `void(process &, size_t, const raw_object *)` for use with std::function

template<typename F, std::size_t MaxArity>
class functor_wrapper {
	static_assert(!std::is_reference_v<F>);
	F f;

	functor_wrapper() = delete;
	constexpr explicit functor_wrapper(auto &&f) : f(FWD(f)) {}

	template<std::size_t... I>
	void call(process &proc, std::index_sequence<I...>, const raw_object *args) {
		if constexpr(std::same_as<decltype(f(proc.cook_implicit(args[I])...)), void>) {
			f(proc.cook_implicit(args[I])...);
			proc.cmd_ret(proc.None);
		} else {
			proc.cmd_ret(proc.into_object(f(proc.cook_implicit(args[I])...)));
		}
	}

	template<std::size_t PossibleArity>
	void fallback_call(process &proc, size_t num_args, const raw_object *args) {
		if constexpr(PossibleArity + 1) {
			if constexpr(pythonizable_fn<F, PossibleArity>) {
				if(num_args == PossibleArity)
					return call(proc, std::make_index_sequence<PossibleArity>(), args);
			}
			fallback_call<PossibleArity - 1>(proc, num_args, args);
		} else {
			for(std::size_t i = 0; i < num_args; i++)
				proc.cook(args[i]); // delete via object::~object
			proc.cmd_exc(proc.TypeError("Incorrect number of arguments for a lambda function"));
		}
	}

	friend process;

public:
	void operator()(process &proc, size_t num_args, const raw_object *args) {
		if(num_args == MaxArity)
			call(proc, std::make_index_sequence<MaxArity>(), args);
		else
			fallback_call<MaxArity - 1>(proc, num_args, args);
	}
};

#undef FWD

} // namespace snaketongs::detail


////////////////////////////////
//                            //
//   "exported" identifiers   //
//                            //
////////////////////////////////

namespace snaketongs {
	struct process : detail::process {};
	using detail::object;
	using exception = detail::cpp_wrapped_py_exc;
	using detail::io_error;
	using detail::kw;
}

template<>
struct std::hash<snaketongs::object> {
	std::size_t operator()(const snaketongs::object& obj) const {
		return obj.hash();
	}
};

#endif
