# Overte TypeScript declarations

TypeScript declarations (`.d.ts`) for the Overte scripting API, generated from the same JSDoc that
produces the API reference. Script contexts expose different globals, so there is one file per
context plus a shared types file:

| File | Context (`@hifi-*` tag) |
| --- | --- |
| `overte-interface.d.ts` | Interface scripts (`@hifi-interface`) |
| `overte-client-entity.d.ts` | Client entity scripts (`@hifi-client-entity`) |
| `overte-avatar.d.ts` | Avatar scripts (`@hifi-avatar`) |
| `overte-server-entity.d.ts` | Server entity scripts (`@hifi-server-entity`) |
| `overte-assignment-client.d.ts` | Assignment client scripts (`@hifi-assignment-client`) |
| `overte-types.d.ts` | Typedefs, class instance types and `Signal<F>`, referenced by every context file |

## Using them

Reference exactly one context per script, either with a triple-slash directive:

```ts
/// <reference types="@heluca/overte-types/server-entity" />
```

or in `tsconfig.json`, with the DOM library left out (Overte has no DOM):

```json
{
    "compilerOptions": {
        "strict": true,
        "lib": ["ES2023"],
        "types": ["@heluca/overte-types/server-entity"]
    }
}
```

The context files declare the same globals, so two of them in one program conflict. Compile the
scripts for each context as a separate project. `sample/` holds a port of the Shortbow game
manager that is type-checked against the server entity context.

## Regenerating

From `tools/typings`:

```sh
npm ci
npm run build   # jsdoc over the engine source -> build/hifiJSDoc.json -> dist/*.d.ts
npm run check   # tsc --strict over each dist file and over sample/
```

`npm run build` runs JSDoc with the existing `tools/jsdoc` plugins (`hifi.js` collects the
`@jsdoc` blocks from the C++ source, `hifiJSONExport.js` writes the doclets) and a template that
writes nothing, then `generate.js` turns `build/hifiJSDoc.json` into `dist/`. To reuse a
`hifiJSDoc.json` made by the HTML documentation build, run
`node generate.js <path>/hifiJSDoc.json dist`. `build/` and `dist/` are not committed.

The generator prints how many JSDoc types it could not resolve. Each one is emitted as `unknown`,
never `any`; most are typos in the source JSDoc (`QUuid`, `GrphicsMeshPart`) and are fixed there.

## How the JSDoc maps to TypeScript

- An `@namespace` tagged for a context becomes a `declare namespace` in that context's file:
  functions (each documented overload kept), signals as `const name: Signal<(args) => void>`, and
  properties as `let`, or `const` when documented read-only.
- A constructible `@class` becomes an interface in the shared file and a `declare var` with a
  `new` signature in the contexts it is tagged for; a class with `@hideconstructor` is only an
  interface, because scripts never see a global of that name.
- An object `@typedef` becomes an interface. When a typedef documents defaults for its
  properties it is a settings bag the engine accepts partially, so all its properties are
  optional. Variant typedefs such as `Entities.EntityProperties-Model` are emitted as
  `Entities.EntityProperties_Model`, and their properties are folded into the base type, as the
  engine takes one flat property set.
- A string `@typedef` whose description table lists every value as `<code>"value"</code>`
  becomes a union of string literals (`Entities.EntityType`, `ShapeType`).
- Optional parameters that precede a required one become separate overloads.
- A property and a function documented under the same name (a `Q_PROPERTY` and its getter) keep
  the property.

## Follow-up: drift check

The declarations are only as accurate as the JSDoc, which is maintained by hand next to the
`Q_INVOKABLE`s it describes (for example `Script.clearInterval` was documented as taking an
object while the C++ takes an `int`). A drift check would walk the `QMetaObject` of every object
registered as a script global, per context, dumping its invokable methods, properties and signals
with their Qt types, and compare that against the doclets: undocumented members, documented
members that no longer exist, and argument or return types that disagree. Run in CI, it would
keep both the API reference and these declarations honest.
