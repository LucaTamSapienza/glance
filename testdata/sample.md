# glance — feature showcase

A single file that exercises most of what glance renders. Open it with
`glance testdata/sample.md` and move the block cursor with `h j k l`.

## Text styles

This paragraph mixes **bold**, *italic*, ~~strikethrough~~, and `inline code`.
It is deliberately long so you can watch glance word-wrap it to whatever width
your terminal happens to be right now, reflowing live as you resize the window.

> A blockquote, for asides and pull-quotes.
> It can span several lines and is rendered with a left bar.

## Lists

- first item
- second item with a nested list
  - nested one
  - nested two
- third item

1. ordered one
2. ordered two
3. ordered three

- [x] task lists render too
- [ ] this one is unchecked

## Code

Inline `like_this`, and fenced blocks with per-language syntax highlighting:

```go
func main() {
    fmt.Println("hello, glance") // greet
}
```

```python
def greet(name):       # say hello
    return f"hi {name}" + str(42)
```

```yaml
name: glance
version: 1.0
enabled: true   # a comment
```

```sh
echo "$HOME" && ls -la | grep .md
```

## Table

| Feature   | Key   | Notes                    |
|-----------|-------|--------------------------|
| Search    | `/`   | then `n` / `N`           |
| Outline   | `t`   | jump to a heading        |
| Edit      | `i`   | Esc returns to the reader|

## Links

An [external link](https://example.com) opens in your browser with Enter.
For links *between* notes, see the little vault under `testdata/vault/`.

---

That horizontal rule marks the end. Press `?` any time for the key bindings.
