# glance — showcase & manual test sheet

Open with **`./glance testdata/showcase.md`**. Each section exercises one
feature and tells you what to look for. Move with `j`/`k`/arrows, `?` for help,
`:q` to quit.

---

## 1. Syntax highlighting

Each fence is coloured by its language. Check: **keywords**, "strings",
`numbers`, and comments each get a distinct colour, all over the code background.

```python
def greet(name):                 # comment is dim grey
    items = [1, 2, 3]            # 1 2 3 are numbers
    return f"hello {name}" + str(42)
```

```yaml
name: glance                     # 'name' is a key (blue), 'glance' plain
version: 1.0                     # 1.0 is a number
enabled: true                    # true is a keyword
```

```bash
echo "$HOME" && ls -la | grep .md    # echo keyword, $HOME variable (red)
```

```go
func main() {
    fmt.Println("hello, glance")  // string green, func keyword
}
```

```json
{ "ok": true, "count": 3, "name": "glance" }
```

Unknown languages fall back to a plain box:

```cobol
       IDENTIFICATION DIVISION.
```

---

## 2. Tables — column alignment

Columns line up and the `:---` markers set alignment. Check: **Left** hugs the
left, **Center** is centred, **Right** hugs the right, and every border lines up.

| Left   | Center | Right |
|:-------|:------:|------:|
| a      | bb     | ccc   |
| dddd   | e      | f     |
| ✓ long | mid    | 1000  |

---

## 3. Inline image

In a graphics-capable terminal (iTerm2, Kitty, Ghostty, WezTerm) the gradient is
drawn inline. Elsewhere the `▦` placeholder stays — and `Enter` on it opens the
file. Check: a colour gradient appears below, or the placeholder + Enter opens it.

![a colour gradient](glance.png)

To **paste an image**, copy one to the clipboard (e.g. a screenshot), press `i`
to edit, then `Ctrl-V`: glance saves it as a PNG inside a `showcase_media/` folder
next to this file and inserts a `![](…)` reference at the cursor.

---

## 4. Cursor sync (reader ↔ editor)

This is about landing on the *same line* when you switch modes. Try:

1. Put the cursor on the heading **"## 4. Cursor sync"** above.
2. Press `i` (Insert) — the editor should open with the cursor on that same
   heading line, not drifted up or down.
3. Press `Esc` to return; the reader cursor should be back on the heading.
4. Repeat on a code line inside the Go block, and on a list item below —
   structural lines map exactly.

- list item one
- list item two
- list item three

---

## 5. Vault navigation (links + graph)

- `Enter` on this [external link](https://example.com) opens your browser.
- `Enter` on a `[[wikilink]]` follows it inside glance; `-` / `Ctrl-O` go back.
- `b` opens backlinks, `Ctrl-G` opens the graph explorer.
- The small vault to try this on lives under `testdata/vault/` — open
  `testdata/vault/index.md` and follow its `[[links]]`.

---

## 6. The rest (regression sanity)

- `/` then type to **search**; `n` / `N` jump between matches.
- `t` toggles the **table of contents**; `Enter` jumps to a heading.
- `e` opens **split** mode (editor left, live preview right); type and watch the
  preview update; `Esc` returns.
- `v` / `V` select lines, `y` yanks them to the system clipboard.
- `Ctrl-S` saves; `:w` `:wq` `:q` `:q!` behave vi-style.

---

That horizontal rule marks the end. If sections 1–4 look right, the four new
features work; 5–6 confirm nothing else regressed.
