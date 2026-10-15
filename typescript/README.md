# seekdb Node.js binding

This package manages a local seekdb runtime. It starts or reuses the seekdb
server, returns the local MySQL-protocol endpoint, and releases the lifecycle
handle. SQL connections are owned by the application and its chosen client.

The public API is written in TypeScript and is available to JavaScript and
TypeScript applications through one package. Node.js 22 or newer is the initial
supported runtime. The package uses ES modules (`import`). Bun and Deno have not
been validated with this package.

## Build from this repository

Initialize the repository's git submodules, install Node.js and CMake, and make
a compatible `seekdb` executable available on `PATH` or set `SEEKDB_BIN`:

```sh
git submodule update --init --recursive
cd typescript
npm install
SEEKDB_BIN=/path/to/seekdb npm test
```

`npm test` builds the package before running the tests. To build without testing,
run `SEEKDB_BIN=/path/to/seekdb npm run build`. The build creates `libseekdb`,
compiles the Node-API addon and TypeScript, then stages the addon, library, and
server executable under `dist/native`.
The native files must match the host operating system and architecture.
The package is private while platform-specific release artifacts are being
defined; the local build can be consumed by JavaScript and TypeScript projects.

## Use

```js
import { seekdb } from "@oceanbase/seekdb-node";
import mysql from "mysql2/promise";

const db = await seekdb.open("./seekdb.db", {
  parameters: { memory_budget: "2G" },
});
let connection;
try {
  const endpoint = db.connectionOptions();
  const location = endpoint.transport === "tcp"
    ? { host: endpoint.host, port: endpoint.port }
    : { socketPath: endpoint.transport === "unix_socket"
        ? endpoint.unixSocket : endpoint.namedPipe };
  connection = await mysql.createConnection({
    user: endpoint.user,
    password: "",
    database: "test",
    ...location,
  });
  const [rows] = await connection.query("SELECT 1 AS value");
  console.log(rows);
} finally {
  if (connection) await connection.end();
  db.close();
}
```

The endpoint is valid while its `seekdb` handle remains open. Close external SQL
connections before the handle. `close()` is idempotent and does not delete data
or guarantee immediate server-process termination. The returned `dbDir` is an
absolute, normalized path. On Windows, a named-pipe endpoint may require a
client-specific transport adapter; validate the chosen client before relying on
it. The package exposes connection information rather than a client-specific
configuration object.

Server parameters use the names and string values accepted by `seekdb_open`.
Ordinary parameters apply on first initialization; `port` and `mysql_port_mode`
apply on each spawn. Errors from `open()` include `returnCode` and the native
startup diagnostic. A native handle is also closed when garbage collected, but
applications should call `close()` explicitly.
