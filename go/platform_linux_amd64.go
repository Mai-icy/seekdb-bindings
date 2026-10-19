//go:build linux && amd64

package seekdb

import native "github.com/oceanbase/seekdb-bindings/go/native/linux-amd64"

func platformBundle() []byte     { return native.Archive }
func platformLibrary() string    { return "libseekdb.so" }
func platformExecutable() string { return "seekdb" }
