use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_int, c_void};
use std::ptr;

// Raw FFI
#[repr(C)]
struct NabdTypes { _unused: [u8; 0] }
type NabdHandle = *mut NabdTypes;

extern "C" {
    fn nabd_open(name: *const c_char, capacity: usize, slot_size: usize, flags: c_int) -> NabdHandle;
    fn nabd_close(q: NabdHandle) -> c_int;
    fn nabd_unlink(name: *const c_char) -> c_int;
    fn nabd_push(q: NabdHandle, data: *const c_void, len: usize) -> c_int;
    fn nabd_pop(q: NabdHandle, buf: *mut c_void, len: *mut usize) -> c_int;
}

pub const NABD_OK: c_int = 0;
pub const NABD_EMPTY: c_int = -1;
pub const NABD_FULL: c_int = -2;

pub const NABD_CREATE: c_int = 0x01;
pub const NABD_PRODUCER: c_int = 0x02;
pub const NABD_CONSUMER: c_int = 0x04;

pub struct Nabd {
    handle: NabdHandle,
}

impl Nabd {
    pub fn open(name: &str, capacity: usize, slot_size: usize, flags: i32) -> Result<Self, String> {
        let c_name = CString::new(name).map_err(|_| "Invalid name")?;
        let handle = unsafe { nabd_open(c_name.as_ptr(), capacity, slot_size, flags as c_int) };
        
        if handle.is_null() {
            Err("Failed to open NABD queue".to_string())
        } else {
            Ok(Nabd { handle })
        }
    }

    pub fn unlink(name: &str) -> i32 {
        let c_name = CString::new(name).unwrap();
        unsafe { nabd_unlink(c_name.as_ptr()) }
    }

    pub fn push(&self, data: &[u8]) -> Result<(), i32> {
        // len + 1 to include null terminator if treating as string, but preserving raw bytes is better.
        // The C API treats it as raw bytes if we just pass len.
        // But simple_producer does strlen(msg)+1. Let's send raw bytes.
        let ret = unsafe { nabd_push(self.handle, data.as_ptr() as *const c_void, data.len()) };
        if ret == NABD_OK {
            Ok(())
        } else {
            Err(ret)
        }
    }

    pub fn pop(&self) -> Result<Vec<u8>, i32> {
        let mut buf = vec![0u8; 4096];
        let mut len = buf.len();
        
        let ret = unsafe { nabd_pop(self.handle, buf.as_mut_ptr() as *mut c_void, &mut len) };
        
        if ret == NABD_OK {
            buf.truncate(len);
            Ok(buf)
        } else {
            Err(ret)
        }
    }
}

impl Drop for Nabd {
    fn drop(&mut self) {
        unsafe { nabd_close(self.handle); }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_flow() {
        let name = "/rust_test";
        Nabd::unlink(name);

        {
            let q = Nabd::open(name, 16, 64, NABD_CREATE | NABD_PRODUCER | NABD_CONSUMER).unwrap();
            let msg = b"Hello Rust";
            q.push(msg).unwrap();
            
            let popped = q.pop().unwrap();
            assert_eq!(popped, msg);
        }
        
        Nabd::unlink(name);
    }
}
