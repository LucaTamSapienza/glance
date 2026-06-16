# Rendering

glance parses Markdown with md4c and builds a *structured document* — a list of
visual lines, each a sequence of styled runs. It then draws those runs straight
to terminal cells. Because it owns that model, search, the cursor, the outline,
and link-following all work without parsing escape codes back out.

This note is reached from [[index]]. It also points at [[Vault Mode]], so the two
notes link to each other — open the backlinks panel with `b` to see that.

See also the rendering showcase in `../../sample.md`.
