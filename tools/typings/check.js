//
//  check.js
//
//  Copyright 2026 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
//  Type-checks each generated context file on its own (the contexts redeclare the same
//  globals, so they cannot share one program), then the sample script.
//

"use strict";

const { spawnSync } = require("child_process");
const path = require("path");
const { CONTEXTS } = require("./lib/model");

const tsc = path.join(__dirname, "node_modules", "typescript", "bin", "tsc");
const strict = ["--noEmit", "--strict", "--lib", "es2023", "--types", ""];

function run(label, args) {
    console.log("tsc " + label);
    const result = spawnSync(process.execPath, [tsc].concat(args), { cwd: __dirname, stdio: "inherit" });
    return result.status === 0;
}

let ok = run("dist/overte-types.d.ts", strict.concat(["dist/overte-types.d.ts"]));
CONTEXTS.forEach(context => {
    ok = run("dist/overte-" + context.id + ".d.ts", strict.concat(["dist/overte-" + context.id + ".d.ts"])) && ok;
});
ok = run("-p sample/tsconfig.json", ["-p", "sample/tsconfig.json"]) && ok;
process.exit(ok ? 0 : 1);
