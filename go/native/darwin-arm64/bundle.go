package native

import _ "embed"

// Archive contains the seekdb executable, libseekdb, and its runtime libraries.
//
//go:embed bundle.tar.gz
var Archive []byte
