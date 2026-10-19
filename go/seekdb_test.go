//go:build (darwin && arm64) || (linux && amd64)

package seekdb

import (
	"fmt"
	"net"
	"os"
	"path/filepath"
	"testing"
	"time"
)

func TestOpenConnectionOptionsAndClose(t *testing.T) {
	root, err := os.MkdirTemp("/tmp", "sdb-go-")
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { os.RemoveAll(root) })
	dir := filepath.Join(root, "database")
	db, err := Open(dir)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() {
		if err := db.Close(); err != nil {
			t.Error(err)
		}
	})
	if db.DBDir != dir {
		t.Fatalf("DBDir = %q, want %q", db.DBDir, dir)
	}
	endpoint, err := db.ConnectionOptions()
	if err != nil {
		t.Fatal(err)
	}
	var network, address string
	switch endpoint.Transport {
	case "unix_socket":
		network, address = "unix", endpoint.UnixSocket
	case "tcp":
		network, address = "tcp", net.JoinHostPort(endpoint.Host, fmt.Sprint(endpoint.Port))
	default:
		t.Fatalf("unexpected transport %q", endpoint.Transport)
	}
	conn, err := net.DialTimeout(network, address, 3*time.Second)
	if err != nil {
		t.Fatalf("seekdb endpoint %s %s: %v", network, address, err)
	}
	conn.Close()
	if err := db.Close(); err != nil {
		t.Fatal(err)
	}
	if err := db.Close(); err != nil {
		t.Fatalf("second Close: %v", err)
	}
	if _, err := db.ConnectionOptions(); err == nil {
		t.Fatal("ConnectionOptions succeeded after Close")
	}
	reopened, err := Open(dir)
	if err != nil {
		t.Fatalf("reopen existing database: %v", err)
	}
	if err := reopened.Close(); err != nil {
		t.Fatalf("close reopened database: %v", err)
	}
}
