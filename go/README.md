# Go binding

This module manages the local seekdb process through the public `libseekdb` C
API. It provides `Open`, `ConnectionOptions`, and `Close`. Applications use a
MySQL protocol driver of their choice for SQL.

## Native distribution

The source repository does not carry platform binaries. A release builder,
such as GitHub Actions, supplies a seekdb executable and the matching
`libseekdb` and runtime libraries. The packaging scripts create a Go module ZIP
for each platform. The ZIPs must be served by a Go module proxy. **These proxy
URLs are still examples and do not exist yet:**

```text
https://artifacts.seekdb.example/go-proxy/github.com/oceanbase/seekdb-bindings/go/native/darwin-arm64/@v/v1.4.0.zip
https://artifacts.seekdb.example/go-proxy/github.com/oceanbase/seekdb-bindings/go/native/linux-amd64/@v/v1.4.0.zip
```

The platform module ZIP contains the corresponding `bundle.tar.gz`, a tiny Go
`go:embed` package, and license files. `go build` selects the platform module
through build tags, downloads its ZIP through `GOPROXY` when absent from the Go
module cache, and embeds the bundle in the program. It does not contact the
network when `Open` runs. On first `Open`, the bundle is extracted to the user
cache under `seekdb-go/<archive SHA-256>/`.

The main binding source does not contain either platform binary. Its `go.mod`
sets a minimum native module version. A new compatible seekdb binary can be
published as a new version of the native modules without releasing the binding
again. Applications opt into the new version with `go get`; ordinary builds
continue using the version recorded in their `go.mod` and `go.sum`.

## Supported packages

| Go platform | Native module | Bundle size | Tested host |
| --- | --- | ---: | --- |
| `darwin/arm64` | `github.com/oceanbase/seekdb-bindings/go/native/darwin-arm64` | 60.4 MB | macOS 26.5.1 |
| `linux/amd64` | `github.com/oceanbase/seekdb-bindings/go/native/linux-amd64` | 57.5 MB | Alibaba Cloud Linux 3, glibc 2.32 |

The currently verified macOS and Linux bundles use different seekdb v1.4.0
build identifiers. The Linux bundle also includes OpenSSL 1.1.1k and libaio
0.3.112. It requires glibc and is not a musl/Alpine package. The bundled
OpenSSL 1.1 runtime must be replaced before general availability.

## Build from a source checkout

The import paths name Go modules; they do not require GitHub access when the
modules are available locally. The checked-in `go.work` uses relative paths to
the main module and both platform modules. Supply a seekdb executable and a
directory containing the matching `libseekdb` and runtime libraries, then
create the bundle for your host platform. For macOS arm64:

```sh
python3 go/scripts/prepare_native_bundle.py darwin-arm64 /path/to/macos/support go/native/darwin-arm64/bundle.tar.gz --seekdb-bin /path/to/macos/seekdb
cd go
GOPROXY=off go test ./...
```

For Linux amd64, use `linux-amd64` and
`go/native/linux-amd64/bundle.tar.gz` in the preparation command. Only the
host platform's bundle is needed for a normal build. Generated bundles are
ignored by Git. `go build` and `go test` use the local modules through
`go.work`, so they do not fetch the platform module from GitHub. Building
requires `CGO_ENABLED=1` and a C compiler.

## Use after the Go module proxy is live

Configure a Go module proxy backed by the static files above. The second
proxy serves ordinary public Go dependencies. `GONOSUMDB` is needed for these
separately published native modules; the application's `go.sum` still pins the
downloaded module content.

```sh
export GOPROXY=https://artifacts.seekdb.example/go-proxy,https://proxy.golang.org
export GONOSUMDB='github.com/oceanbase/seekdb-bindings/go/native/*'
go get github.com/oceanbase/seekdb-bindings/go@v0.1.0
go build ./...
```

The main module must also be tagged before that `go get` command will work.
To select a newer seekdb binary without changing the binding source:

```sh
go get github.com/oceanbase/seekdb-bindings/go/native/darwin-arm64@v1.5.0 \
       github.com/oceanbase/seekdb-bindings/go/native/linux-amd64@v1.5.0
go build ./...
```

On a normal platform build, Go fetches the selected platform's module ZIP.
Dependency management commands such as `go mod tidy` or `go mod download all`
may fetch both platform ZIPs.

```go
package main

import (
    "log"

    seekdb "github.com/oceanbase/seekdb-bindings/go"
)

func main() {
    db, err := seekdb.Open("./data", seekdb.OpenOptions{
        Parameters: map[string]string{"memory_budget": "1G"},
    })
    if err != nil { log.Fatal(err) }
    defer db.Close()

    endpoint, err := db.ConnectionOptions()
    if err != nil { log.Fatal(err) }
    // Pass endpoint.User and either endpoint.UnixSocket or endpoint.Host/Port
    // to your chosen MySQL protocol client.
    log.Printf("seekdb endpoint: %s", endpoint.Transport)
}
```

`Open` creates a database directory if needed and reopens an existing one.
`Close` releases the local lifecycle handle without deleting data. Close SQL
client connections before `Close`. Building requires `CGO_ENABLED=1` and a C
compiler.

## Prepare and publish native modules

Build `libseekdb` from this repository and stage it with its runtime libraries
in a separate support directory for each platform. The seekdb executable is a
separate input; an Actions job can build or download it before packaging. On
macOS, stage OpenSSL dylibs with `@loader_path` install names. On Linux, stage
OpenSSL and libaio with `$ORIGIN` ELF search paths. The preparation script uses
the supplied executable and creates a bundle outside Git:

```sh
python3 go/scripts/prepare_native_bundle.py darwin-arm64 /path/to/macos/support /tmp/seekdb-bundles/darwin-arm64/bundle.tar.gz --seekdb-bin /path/to/macos/seekdb
python3 go/scripts/prepare_native_bundle.py linux-amd64 /path/to/linux/support /tmp/seekdb-bundles/linux-amd64/bundle.tar.gz --seekdb-bin /path/to/linux/seekdb
```

Linux preparation needs `patchelf`; it leaves an executable with an existing
`$ORIGIN` RPATH untouched. `go/native-release.json` records the module path,
version, publication time, and supported platforms. The preparation and
publishing scripts print the SHA-256 of each generated bundle. If an Actions
job downloads the input executable, verify that download in the job before
packaging it.

For a different binary or C SDK build, publish a new immutable module version
and record the generated checksums with the release artifacts. The source
repository does not need to contain the native bundle. An Actions release job
can obtain the binary, prepare the bundles, generate the proxy files, and
upload them to the real module proxy endpoint. Applications then use the
published platform modules; they do not provide a local seekdb binary.

Generate the static Go module proxy layout (`.info`, `.mod`, `.zip`, and
version list) from the prepared bundles:

```sh
python3 go/scripts/publish_native_proxy.py /tmp/seekdb-go-proxy /tmp/seekdb-bundles
```

Publish the proxy files under a real `GOPROXY` endpoint. GitHub Releases alone
do not serve the Go module proxy protocol. The publisher refuses to replace
different content at an already published module version. For a new release,
pass a versioned local JSON file to the publisher with `--release-manifest`.
Applications opt into the newer platform module versions with `go get`.

For local verification, use the generated proxy as a `file://` source:

```sh
cd go
GOPROXY=file:///tmp/seekdb-go-proxy \
GONOSUMDB='github.com/oceanbase/seekdb-bindings/go/native/*' go test -count=1 ./...
```

The seekdb launcher inside each bundle is a small shell wrapper. Direct
`posix_spawn` from the Go/cgo process caused an early exit on the tested
macOS build; the wrapper launches the executable without downloading files.
