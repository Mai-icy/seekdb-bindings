//go:build !(darwin && arm64) && !(linux && amd64)

package seekdb

func platformBundle() []byte     { return nil }
func platformLibrary() string    { return "" }
func platformExecutable() string { return "" }
