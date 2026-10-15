import assert from "node:assert/strict";
import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import test from "node:test";
import mysql from "mysql2/promise";

import { seekdb, seekdbError } from "../dist/index.js";

test("validates JavaScript input before opening", async () => {
  await assert.rejects(seekdb.open(""), TypeError);
  await assert.rejects(seekdb.open("data", { parameters: { memory_budget: 1 } }), TypeError);
  await assert.rejects(seekdb.open("data", { parameters: { "": "x" } }), TypeError);
});

test("reports native open errors with their return code and diagnostic", async () => {
  const root = mkdtempSync(join(tmpdir(), "seekdb-node-error-"));
  try {
    const file = join(root, "not-a-directory");
    writeFileSync(file, "x");
    await assert.rejects(seekdb.open(file), (error) => {
      assert.ok(error instanceof seekdbError);
      assert.equal(error.returnCode, -1);
      assert.match(error.message, /Cannot create\/access (database|run) directory/);
      return true;
    });
  } finally {
    rmSync(root, { recursive: true, force: true });
  }
});

test("rejects open when creating the native error throws", async () => {
  const root = mkdtempSync(join(tmpdir(), "seekdb-node-error-fallback-"));
  const file = join(root, "not-a-directory");
  writeFileSync(file, "x");
  const previous = Object.getOwnPropertyDescriptor(Error.prototype, "returnCode");
  Object.defineProperty(Error.prototype, "returnCode", {
    configurable: true,
    set() { throw new Error("error setter invoked"); },
  });
  let timeout;
  try {
    const result = await Promise.race([
      seekdb.open(file).then(
        () => ({ kind: "resolved" }),
        (error) => ({ kind: "rejected", error }),
      ),
      new Promise((resolveTimeout) => {
        timeout = setTimeout(() => resolveTimeout({ kind: "timeout" }), 2000);
      }),
    ]);
    assert.equal(result.kind, "rejected");
    assert.match(result.error.message, /error setter invoked/);
  } finally {
    clearTimeout(timeout);
    if (previous) Object.defineProperty(Error.prototype, "returnCode", previous);
    else delete Error.prototype.returnCode;
    rmSync(root, { recursive: true, force: true });
  }
});

test("opens two handles and gives an external MySQL client a working endpoint", async () => {
  const root = mkdtempSync(join(tmpdir(), "seekdb-node-test-"));
  const dir = join(root, "database");
  let first;
  let second;
  let connection;
  try {
    first = await seekdb.open(dir, { parameters: { memory_budget: "1G" } });
    second = await seekdb.open(dir);
    assert.equal(first.dbDir, resolve(dir));
    assert.equal(first.closed, false);

    const endpoint = second.connectionOptions();
    assert.equal(endpoint.user, "root");
    assert.ok(["unix_socket", "tcp", "named_pipe"].includes(endpoint.transport));
    const location = endpoint.transport === "tcp"
      ? { host: endpoint.host, port: endpoint.port }
      : { socketPath: endpoint.transport === "unix_socket"
          ? endpoint.unixSocket : endpoint.namedPipe };
    connection = await mysql.createConnection({
      ...location,
      user: endpoint.user,
      password: "",
      database: "test",
    });
    const [rows] = await connection.query("SELECT 1 AS value");
    assert.equal(rows[0].value, 1);

    first.close();
    first.close();
    assert.equal(first.closed, true);
    assert.throws(() => first.connectionOptions(), /closed/);
    assert.equal(second.closed, false);
    const [afterClose] = await connection.query("SELECT 2 AS value");
    assert.equal(afterClose[0].value, 2);
  } finally {
    if (connection) await connection.end();
    if (second) second.close();
    if (first) first.close();
    // The server observes the released client lock asynchronously.
    await new Promise((resolveDelay) => setTimeout(resolveDelay, 1000));
    rmSync(root, { recursive: true, force: true });
  }
});
