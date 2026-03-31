# Regex Syntax-Policy Matrix

Canonical syntax-policy matrix for `RegexEngine` dialect handling.

This table is the source-of-truth for strict iregexp compatibility
policy (RFC 9485 target behavior) versus extended behavior used by
Trieste parsers.

| Construct | Extended | Strict iregexp |
|---|---|---|
| Standalone `{`, `}`, `]` | accept literal | reject malformed |
| Shorthand escapes `\d\D\s\S\w\W` | accept | reject malformed |
| Identity escapes (e.g. `\@`, `\"`, `\'`) | accept | reject malformed |
| POSIX classes `[[:alpha:]]` etc. | accept | reject malformed |
| Category escapes `\p{..}` and `\P{..}` | accept | accept |
| Single-char escapes (RFC set) | accept | accept |
| Quantifiers `?`,`*`,`+`,`{n}`,`{n,m}`,`{n,}` | accept | accept |
| Lazy quantifiers `??`,`*?`,`+?` | accept | reject malformed |
| Capturing groups `(...)` | accept | reject malformed |
| Non-capturing groups `(?:...)` | accept | reject malformed |
| Anchors `^` and `$` | accept | accept |
| Word boundary `\b` | accept | reject malformed |
| Dot `.` | accept | accept |

Strict-mode API is wired via `SyntaxMode` and parser compile-time gating.
In both modes, malformed syntax sets `malformed_` during compile.
Match calls on malformed patterns return `false`/`npos`.
