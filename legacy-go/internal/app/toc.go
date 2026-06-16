package app

import (
	"bufio"
	"strings"
)

type TOCItem struct {
	Level   int
	Title   string
	LineNum int
}

func ExtractTOC(md string) []TOCItem {
	var items []TOCItem
	sc := bufio.NewScanner(strings.NewReader(md))
	sc.Buffer(make([]byte, 64*1024), 1024*1024)
	inFence := false
	line := 0
	for sc.Scan() {
		line++
		text := sc.Text()
		trim := strings.TrimSpace(text)
		if strings.HasPrefix(trim, "```") || strings.HasPrefix(trim, "~~~") {
			inFence = !inFence
			continue
		}
		if inFence {
			continue
		}
		if !strings.HasPrefix(trim, "#") {
			continue
		}
		level := 0
		for level < len(trim) && trim[level] == '#' {
			level++
		}
		if level == 0 || level > 6 {
			continue
		}
		title := strings.TrimSpace(trim[level:])
		if title == "" {
			continue
		}
		items = append(items, TOCItem{Level: level, Title: title, LineNum: line})
	}
	return items
}
