import { seekdb, seekdbError } from "../dist/index.js";
import type { ConnectionOptions } from "../dist/index.js";

async function useBinding() {
  const db = await seekdb.open("./seekdb.db", {
    parameters: { memory_budget: "2G" },
  });
  const endpoint: ConnectionOptions = db.connectionOptions();
  if (endpoint.transport === "tcp") {
    const port: number = endpoint.port;
    void port;
  } else if (endpoint.transport === "unix_socket") {
    const path: string = endpoint.unixSocket;
    void path;
  } else {
    const path: string = endpoint.namedPipe;
    void path;
  }
  if (db.closed) throw new seekdbError("closed", -1);
  db.close();
}

void useBinding;
