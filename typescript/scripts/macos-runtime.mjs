import { execFileSync } from "node:child_process";
import { readdirSync, statSync } from "node:fs";
import { basename, isAbsolute, join } from "node:path";

const approvedOpenSSL = new Set(["libssl.3.dylib", "libcrypto.3.dylib"]);

export function classifyMacOSDependency(importer, dependency, stagedNames) {
  if (dependency.startsWith("/usr/lib/") ||
      dependency.startsWith("/System/Library/")) return "system";

  const name = basename(dependency);
  if (dependency === `@loader_path/${name}` ||
      dependency === `@executable_path/${name}` ||
      dependency === `@rpath/${name}`) {
    if (!stagedNames.has(name)) {
      throw new Error(`${importer} references unbundled macOS dependency ${dependency}`);
    }
    return "bundled";
  }

  if (isAbsolute(dependency) && approvedOpenSSL.has(name)) return "openssl";
  throw new Error(`${importer} references unapproved macOS dependency ${dependency}`);
}

export function macOSDependencies(file) {
  const output = execFileSync("otool", ["-L", file], { encoding: "utf8" });
  const dependencies = output.split("\n").slice(1).flatMap((line) => {
    const match = line.match(/^\s+(.+?) \(compatibility version /);
    return match ? [match[1]] : [];
  });
  if (file.endsWith(".dylib")) {
    const installID = macOSInstallID(file);
    if (dependencies[0] !== installID) {
      throw new Error(`${file} has an unexpected macOS install ID ${installID}`);
    }
    return dependencies.slice(1);
  }
  return dependencies;
}

export function macOSInstallID(file) {
  const output = execFileSync("otool", ["-D", file], { encoding: "utf8" });
  const installID = output.split("\n").slice(1).map((line) => line.trim()).find(Boolean);
  if (!installID) throw new Error(`${file} has no macOS install ID`);
  return installID;
}

export function verifyStagedMacOSRuntime(directory) {
  const names = new Set(readdirSync(directory));
  for (const name of names) {
    if (name !== "seekdb" && !name.endsWith(".dylib") && !name.endsWith(".node")) continue;
    const file = join(directory, name);
    if (!statSync(file).isFile()) continue;
    if (name.endsWith(".dylib")) {
      const installID = macOSInstallID(file);
      if (installID !== `@loader_path/${name}`) {
        throw new Error(`${file} has an unbundled macOS install ID ${installID}`);
      }
    }
    for (const dependency of macOSDependencies(file)) {
      const kind = classifyMacOSDependency(file, dependency, names);
      if (kind === "openssl") {
        throw new Error(`${file} retains an absolute OpenSSL dependency ${dependency}`);
      }
    }
  }
  if ([...approvedOpenSSL].some((name) => names.has(name)) &&
      !names.has("LICENSE.openssl.txt")) {
    throw new Error("Bundled OpenSSL libraries require LICENSE.openssl.txt");
  }
}
