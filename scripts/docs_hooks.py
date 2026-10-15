"""Publish the repository README and artwork without maintaining extra copies."""

from pathlib import Path
import re
from urllib.parse import urlsplit

from mkdocs.structure.files import File


ROOT = Path(__file__).resolve().parents[1]


def _homepage_link(destination, config):
    """Rebase README links from the repository root to the docs root."""
    parsed = urlsplit(destination)
    if parsed.scheme or parsed.netloc or not parsed.path:
        return destination
    if parsed.path.startswith("docs/"):
        relative = destination.removeprefix("docs/")
        return "tutorials.md" if relative in ("", "README.md") else relative
    if parsed.path.startswith("misc/images/"):
        return destination
    kind = "tree" if (ROOT / parsed.path).is_dir() else "blob"
    return f"{config.repo_url}/{kind}/main/{destination}"


def on_files(files, config):
    readme = (ROOT / "README.md").read_text(encoding="utf-8")
    readme = re.sub(
        r"(\]\()([^\s)]+)(\))",
        lambda match: match[1] + _homepage_link(match[2], config) + match[3],
        readme,
    )
    readme = re.sub(
        r'(\b(?:href|src)=")([^"]+)(")',
        lambda match: match[1] + _homepage_link(match[2], config) + match[3],
        readme,
    )
    files.append(File.generated(config, "index.md", content=readme))
    # MkDocs maps README.md to index.html too. Publish the guides index under
    # its own URL so it cannot overwrite the repository README homepage.
    guides = files.get_file_from_path("README.md")
    files.remove(guides)
    files.append(
        File.generated(config, "tutorials.md", abs_src_path=str(ROOT / "docs/README.md"))
    )
    for artwork in sorted((ROOT / "misc/images").glob("*.png")):
        files.append(
            File(
                artwork.relative_to(ROOT).as_posix(),
                src_dir=str(ROOT),
                dest_dir=config.site_dir,
                use_directory_urls=config.use_directory_urls,
            )
        )
    return files


def on_page_markdown(markdown, page, config, files):
    if page.file.src_uri == "index.md":
        page.edit_url = f"{config.repo_url}/edit/main/README.md"
    elif page.file.src_uri == "tutorials.md":
        page.edit_url = f"{config.repo_url}/edit/main/docs/README.md"
    return markdown


def on_serve(server, config, builder):
    server.watch(str(ROOT / "README.md"))
    server.watch(str(ROOT / "misc/images"))
    return server
