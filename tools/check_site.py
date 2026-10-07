#!/usr/bin/env python3
"""Validate the dependency-free static website."""

import sys
from collections import Counter
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import unquote, urlsplit


REQUIRED_PAGES = ("index.html", "manual.html", "developers.html", "hardware.html", "404.html")
EXTERNAL_SCHEMES = {"http", "https", "mailto", "tel", "data"}


class PageParser(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.ids = []
        self.links = []
        self.title_count = 0
        self.main_count = 0

    def handle_starttag(self, tag, attrs):
        values = dict(attrs)
        if tag == "title":
            self.title_count += 1
        elif tag == "main":
            self.main_count += 1
        if values.get("id"):
            self.ids.append(values["id"])
        for attribute in ("href", "src"):
            if values.get(attribute):
                self.links.append(values[attribute])

    handle_startendtag = handle_starttag


def parse_page(path: Path) -> PageParser:
    parser = PageParser()
    parser.feed(path.read_text(encoding="utf-8"))
    parser.close()
    return parser


def check_site(root: Path) -> list[str]:
    errors = []
    if not root.is_dir():
        return [f"site directory not found: {root}"]

    for name in REQUIRED_PAGES:
        if not (root / name).is_file():
            errors.append(f"required page missing: {name}")

    pages = {}
    for path in sorted(root.rglob("*.html")):
        relative = path.relative_to(root)
        try:
            page = parse_page(path)
        except (OSError, UnicodeError) as error:
            errors.append(f"{relative}: cannot parse: {error}")
            continue
        pages[path.resolve()] = page
        if page.title_count != 1:
            errors.append(f"{relative}: expected one <title>, found {page.title_count}")
        if page.main_count != 1:
            errors.append(f"{relative}: expected one <main>, found {page.main_count}")
        for identifier, count in Counter(page.ids).items():
            if count > 1:
                errors.append(f"{relative}: duplicate id '{identifier}'")

    for source, page in pages.items():
        source_relative = source.relative_to(root.resolve())
        for raw_url in page.links:
            url = urlsplit(raw_url)
            if url.scheme in EXTERNAL_SCHEMES or url.netloc:
                continue
            target = source if not url.path else (source.parent / unquote(url.path)).resolve()
            if target.is_dir():
                target = target / "index.html"
            if not target.is_file():
                errors.append(f"{source_relative}: missing local target '{raw_url}'")
                continue
            if url.fragment:
                target_page = pages.get(target.resolve())
                if target_page is None or unquote(url.fragment) not in target_page.ids:
                    errors.append(f"{source_relative}: missing fragment '#{url.fragment}' in '{raw_url}'")

    return errors


def main(argv: list[str]) -> int:
    root = Path(argv[1] if len(argv) > 1 else "site")
    errors = check_site(root)
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print(f"Site check passed: {root}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
