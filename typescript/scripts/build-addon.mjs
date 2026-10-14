import { execFileSync } from "node:child_process";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";

const packageDir = resolve(fileURLToPath(new URL("..", import.meta.url)));
const nodeGyp = resolve(packageDir, "node_modules", "node-gyp", "bin", "node-gyp.js");
const environment = { ...process.env };
if (process.platform === "darwin") {
  // Homebrew LLVM can introduce an absolute dependency on its libc++ runtime.
  environment.CC = "/usr/bin/clang";
  environment.CXX = "/usr/bin/clang++";
  delete environment.LDFLAGS;
}
execFileSync(process.execPath, [nodeGyp, "rebuild"], {
  cwd: packageDir,
  env: environment,
  stdio: "inherit",
});
