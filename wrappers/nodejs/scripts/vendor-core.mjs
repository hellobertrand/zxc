// Copies the ZXC C core into the package so the published tarball builds on
// its own. In the repo the addon compiles against the checkout two levels up;
// a consumer installing from npm only ever sees this copy.
//
// Run by "prepack" before the tarball is built, and undone by "postpack" so
// the working tree never keeps a second, drifting copy of the core.

import { cpSync, existsSync, mkdirSync, rmSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const pkgDir = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const repoDir = resolve(pkgDir, "..", "..");
const coreDir = join(pkgDir, "zxc-core");

// A subproject build turns the CLI, the tests and the install rules off
// (see cmake/zxcOptions.cmake), so the core needs nothing beyond these.
const ENTRIES = ["CMakeLists.txt", "cmake", "include", "src/lib", "LICENSE"];

if (process.argv.includes("--clean")) {
  rmSync(coreDir, { recursive: true, force: true });
  process.exit(0);
}

if (!existsSync(join(repoDir, "src/lib/zxc_common.c"))) {
  console.error(`vendor-core: no ZXC checkout at ${repoDir}`);
  process.exit(1);
}

rmSync(coreDir, { recursive: true, force: true });
for (const entry of ENTRIES) {
  const to = join(coreDir, entry);
  mkdirSync(dirname(to), { recursive: true });
  cpSync(join(repoDir, entry), to, { recursive: true });
}
console.log(`vendor-core: staged ${ENTRIES.join(", ")} into zxc-core/`);
