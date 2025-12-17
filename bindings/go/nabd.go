package nabd

/*
#cgo CFLAGS: -I../../include
#cgo LDFLAGS: -L../../build -lnabd
#include <stdlib.h>
#include "nabd/nabd.h"
*/
import "C"
import (
	"errors"
	"unsafe"
)

// Flags
const (
	Create   = C.NABD_CREATE
	Producer = C.NABD_PRODUCER
	Consumer = C.NABD_CONSUMER
)

// Errors
var (
	ErrFull   = errors.New("buffer full")
	ErrEmpty  = errors.New("buffer empty")
	ErrTooBig = errors.New("message too big")
	ErrFailed = errors.New("operation failed")
)

type Queue struct {
	ptr *C.nabd_t
}

// Open opens or creates a NABD queue
func Open(name string, capacity, slotSize int, flags int) (*Queue, error) {
	cName := C.CString(name)
	defer C.free(unsafe.Pointer(cName))

	q := C.nabd_open(cName, C.size_t(capacity), C.size_t(slotSize), C.int(flags))
	if q == nil {
		return nil, ErrFailed
	}

	return &Queue{ptr: q}, nil
}

// Close closes the queue handle
func (q *Queue) Close() {
	if q.ptr != nil {
		C.nabd_close(q.ptr)
		q.ptr = nil
	}
}

// Unlink removes the queue from the system
func Unlink(name string) error {
	cName := C.CString(name)
	defer C.free(unsafe.Pointer(cName))

	if ret := C.nabd_unlink(cName); ret != 0 {
		return ErrFailed
	}
	return nil
}

// Push pushes data to the queue
func (q *Queue) Push(data []byte) error {
	if len(data) == 0 {
		return nil
	}

	// We pass pointer to first element of slice
	ptr := unsafe.Pointer(&data[0])
	ret := C.nabd_push(q.ptr, ptr, C.size_t(len(data)))

	if ret == C.NABD_OK {
		return nil
	} else if ret == C.NABD_FULL {
		return ErrFull
	} else if ret == C.NABD_TOOBIG {
		return ErrTooBig
	}
	return ErrFailed
}

// Pop pops data from the queue
func (q *Queue) Pop(maxLen int) ([]byte, error) {
	buf := make([]byte, maxLen)
	var size C.size_t = C.size_t(maxLen)

	ptr := unsafe.Pointer(&buf[0])
	ret := C.nabd_pop(q.ptr, ptr, &size)

	if ret == C.NABD_OK {
		return buf[:size], nil
	} else if ret == C.NABD_EMPTY {
		return nil, ErrEmpty
	}
	return nil, ErrFailed
}
