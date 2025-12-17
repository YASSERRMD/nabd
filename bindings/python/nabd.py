import ctypes
import os
import sys

# Load shared library
lib_path = os.path.join(os.path.dirname(__file__), '../../build/libnabd.so')
try:
    _lib = ctypes.CDLL(lib_path)
except OSError:
    # Try finding it in current directory if relative path fails
    try:
        _lib = ctypes.CDLL('./build/libnabd.so')
    except OSError:
        raise OSError(f"Could not load libnabd.so from {lib_path} or ./build/")

# Constants
NABD_OK = 0
NABD_EMPTY = -1
NABD_FULL = -2
NABD_NOTFOUND = -3
NABD_INVALID = -4
NABD_TOOBIG = -7

NABD_CREATE = 0x01
NABD_PRODUCER = 0x02
NABD_CONSUMER = 0x04

# Types
class NabdHandle(ctypes.Structure):
    pass

_lib.nabd_open.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_size_t, ctypes.c_int]
_lib.nabd_open.restype = ctypes.POINTER(NabdHandle)

_lib.nabd_close.argtypes = [ctypes.POINTER(NabdHandle)]
_lib.nabd_close.restype = ctypes.c_int

_lib.nabd_unlink.argtypes = [ctypes.c_char_p]
_lib.nabd_unlink.restype = ctypes.c_int

_lib.nabd_push.argtypes = [ctypes.POINTER(NabdHandle), ctypes.c_void_p, ctypes.c_size_t]
_lib.nabd_push.restype = ctypes.c_int

_lib.nabd_pop.argtypes = [ctypes.POINTER(NabdHandle), ctypes.c_void_p, ctypes.POINTER(ctypes.c_size_t)]
_lib.nabd_pop.restype = ctypes.c_int

class Nabd:
    def __init__(self, name, capacity=0, slot_size=0, flags=0):
        self.name = name.encode('utf-8')
        self._handle = _lib.nabd_open(self.name, capacity, slot_size, flags)
        if not self._handle:
            raise RuntimeError(f"Failed to open NABD queue: {name}")

    def close(self):
        if self._handle:
            _lib.nabd_close(self._handle)
            self._handle = None

    def push(self, data):
        if isinstance(data, str):
            data = data.encode('utf-8')
        
        # Need to keep reference to buffer if passing pointer directly? 
        # ctypes handles bytes/bytearray automatically as void*
        ret = _lib.nabd_push(self._handle, data, len(data) + 1) # +1 for null terminator if string
        return ret

    def pop(self, max_len=4096):
        buf = ctypes.create_string_buffer(max_len)
        size = ctypes.c_size_t(max_len)
        
        ret = _lib.nabd_pop(self._handle, buf, ctypes.byref(size))
        
        if ret == NABD_OK:
            # Return raw bytes currently, or decode?
            # Let's return bytes, user can decode
            return buf.value # .value stops at null terminator for create_string_buffer
        elif ret == NABD_EMPTY:
            return None
        else:
            raise RuntimeError(f"nabd_pop failed with code {ret}")

    @staticmethod
    def unlink(name):
        return _lib.nabd_unlink(name.encode('utf-8'))

# Example usage
if __name__ == "__main__":
    queue_name = "/py_nabd_test"
    Nabd.unlink(queue_name)
    
    print(f"Creating queue {queue_name}...")
    q = Nabd(queue_name, 16, 64, NABD_CREATE | NABD_PRODUCER | NABD_CONSUMER)
    
    msg = "Hello from Python!"
    res = q.push(msg)
    print(f"Push result: {res}")
    
    popped = q.pop()
    print(f"Popped: {popped}")
    
    assert popped == msg.encode('utf-8')
    
    q.close()
    Nabd.unlink(queue_name)
    print("Done.")
