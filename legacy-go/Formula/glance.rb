# Manual fallback formula. The release pipeline auto-publishes the real one
# to the lucatam/homebrew-glance tap via GoReleaser; this file documents how
# the formula is shaped if someone wants to install before the tap exists.

class Glance < Formula
  desc "The terminal Markdown reader/editor for macOS"
  homepage "https://github.com/lucatam/glance"
  license "MIT"
  version "0.0.0"

  on_macos do
    on_arm do
      url "https://github.com/lucatam/glance/releases/download/v#{version}/glance_#{version}_darwin_arm64.tar.gz"
      sha256 "REPLACED_BY_GORELEASER"
    end
    on_intel do
      url "https://github.com/lucatam/glance/releases/download/v#{version}/glance_#{version}_darwin_amd64.tar.gz"
      sha256 "REPLACED_BY_GORELEASER"
    end
  end

  def install
    bin.install "glance"
  end

  test do
    system "#{bin}/glance", "--version"
  end
end
