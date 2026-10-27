# Brix Qt language-server fork

Keep changes focused, upstreamable and tested through standard LSP. Semantic
fixes belong in the owning compiler/language service, not editor-specific
identifier lists or regex type guesses. Read `brix/README.md` before editing.

Preserve parameter/return types, overloads, provenance, provider edits and
snippet semantics. Never invent parameter names or silently select an ambiguous
overload. A bare name completed with parentheses is degraded behavior, not a
complete callable experience. Do not claim dynamic observations prove a type.

Embedded JavaScript should use standard JavaScript language intelligence with
QML host declarations and safe source mappings. Do not grow an independent JS
type checker in the editor or assume Node/browser globals exist in QML.

Test real stdio LSP results and existing Qt utility regressions. Keep incomplete
syntax, attached/grouped values, import aliases, flags, project types, dynamic
receivers, signatures and native Markdown/source lookup in the release matrix.
Use the matching Qt SDK; never change private class layouts in the focused
archive-overlay build. Run `brix/build.sh` or its documented equivalent before
publishing. Measure responsiveness; language work must not block the GUI.
