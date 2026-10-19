package seekdb

/*
#cgo linux LDFLAGS: -ldl
#include <stdlib.h>
#include "native_loader.h"
*/
import "C"

import (
	"errors"
	"fmt"
	"path/filepath"
	"sort"
	"strings"
	"sync"
	"unsafe"
)

// OpenOptions supplies seekdb server parameters. Ordinary values apply on first initialization.
type OpenOptions struct {
	Parameters map[string]string
}

// ConnectionOptions describes the local endpoint while a DB handle is open.
type ConnectionOptions struct {
	Transport  string
	User       string
	Host       string
	Port       uint
	UnixSocket string
	NamedPipe  string
}

// Error reports a failure from libseekdb.
type Error struct {
	ReturnCode int
	Message    string
}

func (e *Error) Error() string { return e.Message }

// DB owns a local seekdb lifecycle handle. Close client connections before Close.
type DB struct {
	mu     sync.Mutex
	handle C.SeekdbHandle
	DBDir  string
}

var nativeOnce sync.Once
var nativeError error

func prepareNative() error {
	nativeOnce.Do(func() {
		dir, err := unpackBundle()
		if err != nil {
			nativeError = err
			return
		}
		library := C.CString(filepath.Join(dir, platformLibrary()))
		defer C.free(unsafe.Pointer(library))
		var message [4096]C.char
		if rc := C.sdb_load(library, &message[0], C.size_t(len(message))); rc != C.SEEKDB_SUCCESS {
			nativeError = fmt.Errorf("load libseekdb: %s", C.GoString(&message[0]))
			return
		}
		binary := C.CString(filepath.Join(dir, platformExecutable()))
		defer C.free(unsafe.Pointer(binary))
		if rc := C.sdb_set_binary_path(binary); rc != C.SEEKDB_SUCCESS {
			nativeError = fmt.Errorf("set seekdb executable path: code %d", int(rc))
		}
	})
	return nativeError
}

// Open starts or reuses a local seekdb instance rooted at dbDir.
func Open(dbDir string, options ...OpenOptions) (*DB, error) {
	if dbDir == "" {
		return nil, errors.New("dbDir must not be empty")
	}
	if strings.ContainsRune(dbDir, 0) {
		return nil, errors.New("dbDir must not contain NUL")
	}
	if len(options) > 1 {
		return nil, errors.New("pass at most one OpenOptions value")
	}
	absolute, err := filepath.Abs(dbDir)
	if err != nil {
		return nil, fmt.Errorf("resolve dbDir: %w", err)
	}
	if err := prepareNative(); err != nil {
		return nil, err
	}
	var parameters map[string]string
	if len(options) == 1 {
		parameters = options[0].Parameters
	}
	keys := make([]string, 0, len(parameters))
	for key := range parameters {
		if key == "" {
			return nil, errors.New("parameter keys must not be empty")
		}
		if strings.ContainsRune(key, 0) || strings.ContainsRune(parameters[key], 0) {
			return nil, errors.New("parameter keys and values must not contain NUL")
		}
		keys = append(keys, key)
	}
	sort.Strings(keys)
	cPairs := make([]*C.char, 0, 2*len(keys)+1)
	for _, key := range keys {
		cPairs = append(cPairs, C.CString(key), C.CString(parameters[key]))
	}
	cPairs = append(cPairs, nil)
	defer func() {
		for _, value := range cPairs {
			C.free(unsafe.Pointer(value))
		}
	}()
	cDir := C.CString(absolute)
	defer C.free(unsafe.Pointer(cDir))
	var handle C.SeekdbHandle
	var message [4096]C.char
	rc := C.sdb_open(cDir, (**C.char)(unsafe.Pointer(&cPairs[0])), &handle,
		&message[0], C.size_t(len(message)))
	if rc != C.SEEKDB_SUCCESS {
		if handle != nil {
			C.sdb_close(handle)
		}
		text := C.GoString(&message[0])
		if text == "" {
			text = "seekdb_open failed"
		}
		return nil, &Error{ReturnCode: int(rc), Message: text}
	}
	return &DB{handle: handle, DBDir: absolute}, nil
}

// ConnectionOptions returns fields for a caller-owned MySQL-protocol client.
func (db *DB) ConnectionOptions() (ConnectionOptions, error) {
	db.mu.Lock()
	defer db.mu.Unlock()
	if db.handle == nil {
		return ConnectionOptions{}, errors.New("seekdb is closed")
	}
	var native C.SeekdbConnectionOptions
	if rc := C.sdb_connection_options(db.handle, &native); rc != C.SEEKDB_SUCCESS {
		return ConnectionOptions{}, &Error{ReturnCode: int(rc), Message: "seekdb_connection_options failed"}
	}
	return ConnectionOptions{
		Transport:  C.GoString(native.transport),
		User:       C.GoString(native.user),
		Host:       C.GoString(native.host),
		Port:       uint(native.port),
		UnixSocket: C.GoString(native.unix_socket),
		NamedPipe:  C.GoString(native.named_pipe),
	}, nil
}

// Close releases this handle; repeated calls are harmless. It does not delete data.
func (db *DB) Close() error {
	db.mu.Lock()
	defer db.mu.Unlock()
	if db.handle == nil {
		return nil
	}
	rc := C.sdb_close(db.handle)
	db.handle = nil
	if rc != C.SEEKDB_SUCCESS {
		return &Error{ReturnCode: int(rc), Message: "seekdb_close failed"}
	}
	return nil
}
