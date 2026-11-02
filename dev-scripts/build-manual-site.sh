#!/usr/bin/env bash
set -euo pipefail
# Usage: build-manual-site.sh TAG SITE_DIR [APB_BINARY]
# Adds the HTML manual for TAG to SITE_DIR (a checkout of the gh-pages branch),
# refreshes latest/, and regenerates the root version index. Needs pandoc.
tag=${1:?usage: $0 TAG SITE_DIR [APB_BINARY]}
site=${2:?usage: $0 TAG SITE_DIR [APB_BINARY]}
apb=${3:-build/apb}

if [[ ! "$tag" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "refusing tag '$tag': expected vX.Y.Z" >&2
  exit 1
fi

mkdir -p "$site/$tag"
tmp=$(mktemp)
trap 'rm -f "$tmp"' EXIT
"$apb" --manual > "$tmp"
pandoc -f markdown "$tmp" -s --toc --metadata title="apb" --metadata pagetitle="apb $tag manual" \
  -V maxwidth=42em -o "$site/$tag/index.html"

rm -rf "$site/latest"
cp -r "$site/$tag" "$site/latest"
touch "$site/.nojekyll"

{
  cat <<'EOF'
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>apb manuals</title>
<style>
  html { color-scheme: light dark; }
  body { max-width: 42em; margin: 0 auto; padding: 50px; font-family: sans-serif; }
</style>
</head>
<body>
<h1>apb manuals</h1>
<p>The manual is generated from each release's binary.</p>
<ul>
<li><a href="latest/">latest</a></li>
EOF
  for d in "$site"/v*/; do basename "$d"; done | sort -rV |
    while read -r v; do printf '<li><a href="%s/">%s</a></li>\n' "$v" "$v"; done
  printf '</ul>\n</body>\n</html>\n'
} > "$site/index.html"
