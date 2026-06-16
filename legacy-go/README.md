# legacy-go — DEPRECATED

This directory holds the **original Go implementation** of glance, built on the
Charmbracelet stack (bubbletea + glamour). It is **deprecated, unmaintained, and
kept only for reference.**

glance has been rewritten in C and now lives at the repository root (`src/`). The
C version owns its rendering (md4c → our own document model) instead of relying
on glamour as a black box, and is the source of truth going forward. See the
[root README](../README.md).

This code is preserved so the C rewrite can be checked against the behaviour it
replaced. It will be removed in a later phase once the C version has been tested
in daily use. The Go-primary state of the repository is also recoverable from the
**`go-final`** git tag.

**Do not build on or modify this code.** New work belongs in the C app.

## Building the old Go version (for reference only)

```sh
cd legacy-go
go build -o glance-go ./cmd/glance
go test ./...
```
