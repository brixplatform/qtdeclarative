# Brix qmlls completion patches

This branch starts at Qt `v6.12.0` (`4332fbbb4c340463dc8cee36dda0c85d932122d8`).
The changes in `src/qmlls/` also work with Qt's normal module build. They belong
in the language server, so every LSP client can receive the same semantic results.

- Recover an empty binding or flag operand in the completion request's private
  document, using the QML lexer to avoid modifying strings and comments. The
  document used for diagnostics and saving is unchanged.
- Advertise `.`, `:`, `|`, and space as completion triggers.
- Supply `name: ` snippets for semantic QML binding properties. Preserve existing
  colons, identifier suffixes and grouped/attached access; JS references stay bare.
- Resolve the expected property type with the server's DOM/import/type model,
  including attached and grouped properties, import aliases, inherited project
  properties, complex values, and QML enums.
- Rank matching values through standard LSP `sortText` and `filterText`. Preserve
  the existing completion edits. Suppress already-used flags and flag masks in
  a known enum context; other expressions keep their ordinary suggestions.
- Keep actual numeric properties numeric. In particular, Qt declares
  `font.weight` as `int`; `Font.` completes its named constants without pretending
  the property is an enum. No native metaobject/plugin is loaded for inference.

- Emit provider-authored named-parameter call snippets for single-signature methods
  in JavaScript statement positions. Assignments, callback arguments, existing
  calls, receiver uses and returns keep function references.
- Preserve compiler-known method signatures, return/parameter types and overload
  documentation in standard LSP fields. Ambiguous overloads remain explicit;
  clients must not treat an arbitrary overload as the required call signature.
- Supply brace snippets for QML object declarations, with the cursor inside the
  new body. Preserve existing bodies and qualified imports; type annotations and
  JavaScript references stay bare names. Use standard LSP snippets and edit ranges.
- Identify component origins in standard `labelDetails.description`: the semantic
  defining module, independent of import aliases, or the local source filename
  for components without a named module. Never infer origin from the base type.
- Infer dynamic record fields from bounded DOM literals or accesses to the same
  resolved receiver, including explicitly declared delegate `modelData`. Literal
  field types and observation provenance remain distinct. This is limited QML
  provider support, not a substitute for a standard JavaScript inference engine.
- Supply native type Markdown from semantic metadata (with clear provenance),
  optional installed Qt prose and a real source-declaration link. Resolve relative
  Qt SDK private-header locations without recursively scanning the SDK.

## Build and test against an installed Qt SDK

Run `brix/build.sh`. It builds `build-brix/qmlls` and runs Qt's existing completion
suite plus the plain-LSP regression matrix in `brix/tests/completions.py`.

This focused build requires an **exactly matching Qt 6.12.0 SDK**, including the
private QmlLS archive and headers. It recompiles the modified server translation
units and links the unchanged modules and moc code from that archive. These
patches do not change private class layouts. If the distribution omits the
LanguageServer protocol SDK, the script builds its pinned matching Qt source
in the build directory. It does not change the system Qt installation.

For a different Qt release, rebase and rebuild; do not reuse private archives
across Qt versions. Qt's full normal module build remains the portable upstream
build path. The focused recipe is for the native Brix/Strata Linux package.

## Strata packaging

Configure the editor with:

```sh
cmake -S ../editor -B ../editor/build \
  -DSTRATA_QMLLS_EXECUTABLE="$PWD/build-brix/qmlls"
```

The editor installs this executable into its private tools directory and selects
it before cache/PATH tools. Explicit user server paths still apply. This avoids
replacing the distribution's `qmlls`, and keeps inference and parser recovery out
of Strata. A build without this option continues to use its configured tools or
an unmodified external server, with that server's capabilities and limitations.

## Limits and upstreaming

Recovery handles a missing operand at the requested cursor, not every possible
syntax error elsewhere in a document. Expected-type ranking is confined to
simple binding references and flag expressions; it does not guess types inside
calls, literals, arbitrary JavaScript, or unresolved contexts. Existing upstream
completion tests remain enabled, including their documented expected failures.

Keep source fixes separate from this build recipe when proposing them upstream.
No upstream review or acceptance is implied by this fork.

Embedded JavaScript needs a standard JS language-service bridge with QML host
metadata and versioned source maps. The bounded record/observation support here
does not implement control-flow inference, arbitrary cross-file data flow, or
implicit delegate injection. Extend the shared bridge rather than growing a
second JavaScript type system in Strata. See `AGENTS.md` for contributor rules.
