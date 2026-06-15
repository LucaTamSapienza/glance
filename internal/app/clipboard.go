package app

import "github.com/atotto/clipboard"

// copyToClipboard writes text to the system clipboard. On macOS this shells
// out to pbcopy via atotto/clipboard, so the copied text is available to any
// other application — i.e. it survives outside glance.
func copyToClipboard(text string) error {
	return clipboard.WriteAll(text)
}
