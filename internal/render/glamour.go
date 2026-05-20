package render

import (
	"github.com/charmbracelet/glamour"
	"github.com/muesli/termenv"
)

type Glamour struct {
	r     *glamour.TermRenderer
	width int
	dark  bool
}

func NewGlamour(width int) (*Glamour, error) {
	dark := termenv.HasDarkBackground()
	r, err := newRenderer(dark, width)
	if err != nil {
		return nil, err
	}
	return &Glamour{r: r, width: width, dark: dark}, nil
}

func (g *Glamour) Render(md string) (string, error) {
	return g.r.Render(md)
}

func (g *Glamour) Width() int { return g.width }

func (g *Glamour) Resize(width int) error {
	if width == g.width {
		return nil
	}
	r, err := newRenderer(g.dark, width)
	if err != nil {
		return err
	}
	g.r = r
	g.width = width
	return nil
}

func newRenderer(dark bool, width int) (*glamour.TermRenderer, error) {
	style := glamour.DarkStyleConfig
	if !dark {
		style = glamour.LightStyleConfig
	}
	// Remove default document margins and block padding to align with the editor.
	margin := uint(0)
	style.Document.Margin = &margin
	style.H1.Margin = &margin
	style.H2.Margin = &margin
	style.H3.Margin = &margin
	style.H4.Margin = &margin
	style.H5.Margin = &margin
	style.H6.Margin = &margin
	style.Paragraph.Margin = &margin
	style.CodeBlock.Margin = &margin

	// Code blocks: use a contrasting background so they stand out on both dark
	// and light terminals. The foreground overrides Glamour's default so text
	// stays readable against the new background.
	var codeBG, codeFG string
	if dark {
		codeBG = "#e8e8e8" // near-white background
		codeFG = "#1a1a1a" // near-black text
	} else {
		codeBG = "#2b2b2b" // dark background
		codeFG = "#f0f0f0" // light text
	}
	style.CodeBlock.BackgroundColor = &codeBG
	style.CodeBlock.Color = &codeFG

	// Remove the blank line glamour appends after every heading (BlockSuffix).
	// Without this, 0 blank lines between consecutive headings in the source
	// renders as 1 blank line in the preview, causing a visible line-count mismatch.
	// The !e.First separator ("\n") added before each non-first block still provides
	// the minimum one-line break between adjacent blocks.
	style.Heading.BlockSuffix = ""
	style.H1.BlockSuffix = ""
	style.H2.BlockSuffix = ""
	style.H3.BlockSuffix = ""
	style.H4.BlockSuffix = ""
	style.H5.BlockSuffix = ""
	style.H6.BlockSuffix = ""

	return glamour.NewTermRenderer(
		glamour.WithStyles(style),
		glamour.WithWordWrap(width),
		glamour.WithEmoji(),
		glamour.WithPreservedNewLines(),
	)
}
