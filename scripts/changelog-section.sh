#!/usr/bin/env bash
# Prints the CHANGELOG.md section for one version (without its heading), for use as the
# GitHub Release body; the app shows it in its "What's new" dialog.
# Usage: scripts/changelog-section.sh 1.5.3
set -euo pipefail
ver="${1:?usage: $0 <version>}"
cd "$(dirname "$0")/.."
awk -v ver="$ver" '
  /^## / { if (found) exit; found = index($0, "## " ver " ") == 1 }
  found && !/^## / { print }
' CHANGELOG.md | sed '/./,$!d' | sed -e :a -e '/^\n*$/{$d;N;ba' -e '}'
