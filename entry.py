import sys
import importlib
import queue

NoResponse = object()

[_, cpp_to_py, py_to_cpp, int_size] = sys.argv
del _
sys.argv[:] = '<snaketongs>',

cpp_to_py = open(int(cpp_to_py), 'rb')
py_to_cpp = open(int(py_to_cpp), 'wb')
int_size = int(int_size)

def pack_int(i):
	return i.to_bytes(int_size, byteorder='little', signed=True)

def pack_ptr(obj):
	return pack_int(new_ptr(obj))

########################################
#                                      #
#   python objects referenced by c++   #
#                                      #
########################################

ptrs = []
ptrs_free_idx = None

def new_ptr(obj):
	global ptrs_free_idx
	if ptrs_free_idx is None:
		idx = len(ptrs)
		ptrs.append(obj)
		return idx
	else:
		idx = ptrs_free_idx
		ptrs_free_idx = ptrs[idx]
		ptrs[idx] = obj
		return idx

def del_ptr(idx):
	global ptrs_free_idx
	ptrs[idx] = ptrs_free_idx
	ptrs_free_idx = idx

######################################
#                                    #
#   transitions from python to c++   #
#                                    #
######################################

OCMD_CALL = b'c'
OCMD_DEL_PTR = b'~'
OCMD_RET = b'r'
OCMD_EXC = b'e'

deleted_remotes = queue.SimpleQueue()  # c++ objects forgotten by python

class RemoteObj:
	def __init__(self, remote_idx):
		self.remote_idx = remote_idx
	def __del__(self):
		deleted_remotes.put(self.remote_idx)

def call_lambda(lambda_obj, args):
	process_queue()
	py_to_cpp.write(OCMD_CALL)
	py_to_cpp.write(pack_int(lambda_obj.remote_idx))
	py_to_cpp.write(pack_int(len(args)))
	for arg in args:
		py_to_cpp.write(pack_ptr(arg))
	# function has been called, wait for return cmd
	ret_ref_idx = loop()
	# return to python code the value returned by c++
	return ptrs[ret_ref_idx]

def return_to_cpp(data):
	if data is NoResponse:
		return
	assert type(data) is tuple
	process_queue()
	py_to_cpp.write(OCMD_RET)
	for d in data:
		assert type(d) is bytes
		py_to_cpp.write(d)

def throw_to_cpp(exc_obj):
	process_queue()
	py_to_cpp.write(OCMD_EXC)
	py_to_cpp.write(pack_ptr(exc_obj))

def process_queue():
	while True:
		try:
			remote_idx = deleted_remotes.get_nowait()
		except queue.Empty:
			break
		py_to_cpp.write(OCMD_DEL_PTR)
		py_to_cpp.write(pack_int(remote_idx))

######################################
#                                    #
#   transitions from c++ to python   #
#                                    #
######################################

def cmd_make_int(val):
	return pack_ptr(val),

def cmd_make_bytes(size):
	return pack_ptr(read(size)),

def cmd_make_str(size):
	return pack_ptr(read_str(size)),

def cmd_make_tuple(size):
	return pack_ptr(tuple(read_ptr() for _ in range(size))),

def cmd_make_global(size):
	mod, name = read_str(size).rsplit('.', 1)
	imported = importlib.import_module(mod)
	if name != '*':
		imported = getattr(imported, name)
	return pack_ptr(imported),

def cmd_make_remote(remote_idx):
	return pack_ptr(RemoteObj(remote_idx)),

def cmd_call(size):
	return pack_ptr(read_ptr()(*(read_ptr() for _ in range(size)))),

def cmd_starcall(_):
	return pack_ptr(read_ptr()(*read_ptr(), **read_ptr())),

def cmd_lambda(remote_obj):
	remote_obj = ptrs[remote_obj]
	return pack_ptr(lambda *args: call_lambda(remote_obj, args)),

def cmd_dup(idx):
	return pack_ptr(ptrs[idx]),

def cmd_get_int(idx):
	obj = ptrs[idx]
	if isinstance(obj, int):
		return pack_int(obj),
	raise TypeError('Cannot get int from:', obj)

def cmd_get_bytes(idx):
	obj = ptrs[idx]
	if type(obj) is str:
		obj = bytes(obj, 'utf8')
	if type(obj) is bytes:
		return pack_int(len(obj)), obj,
	raise TypeError('Cannot get bytes from:', obj)

def cmd_del_ptr(idx):
	del_ptr(idx)
	return NoResponse

cmds = {
	ord('I'): cmd_make_int,
	ord('B'): cmd_make_bytes,
	ord('S'): cmd_make_str,
	ord('T'): cmd_make_tuple,
	ord('G'): cmd_make_global,
	ord('R'): cmd_make_remote,
	ord('C'): cmd_call,
	ord('X'): cmd_starcall,
	ord('L'): cmd_lambda,
	ord('D'): cmd_dup,
	ord('i'): cmd_get_int,
	ord('b'): cmd_get_bytes,
	ord('~'): cmd_del_ptr,
}

CMD_RET = ord('r')
CMD_EXC = ord('e')

def loop():
	while True:
		py_to_cpp.flush()
		cmd, = read(1)
		arg = read_int()
		if cmd == CMD_RET:
			return arg
		if cmd == CMD_EXC:
			raise ptrs[arg]
		try:
			response = cmds[cmd](arg)
		except BaseException as exc:
			throw_to_cpp(exc)
		else:
			return_to_cpp(response)

# cmd utils

def read(n):
	b = cpp_to_py.read(n)
	if len(b) != n:
		# short read, parent probably exited without cleanup,
		# probably printing a message, so don't pollute the stderr
		# (also don't throw SystemExit by calling os.exit)
		import os
		os._exit(125)
	return b

def read_int():
	return int.from_bytes(read(int_size), byteorder='little', signed=True)

def read_ptr():
	return ptrs[read_int()]

def read_str(size):
	return str(read(size), 'utf8')

#################
#               #
#   main loop   #
#               #
#################

py_to_cpp.write(b'+')
r = loop()
assert r == 0xD1E_A112EAD1
