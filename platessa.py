#!/usr/bin/env python3
"""
Markdown viewer with Tkinter.
"""

import os
import re
import sys
import tkinter as tk
from tkinter import filedialog, messagebox
from urllib.parse import quote, urlparse

import markdown
from tkinterweb import HtmlFrame


class MarkdownViewer:
    ZOOM_STEP = 1.15
    ZOOM_MIN = 0.4
    ZOOM_MAX = 5.0

    BG_COLOR = "#adadc9"

    def __init__(self, root):
        self.root = root
        self.root.title("Platessa")
        self.root.geometry("900x700")
        self.root.configure(bg=self.BG_COLOR)

        # Status bar
        self.status = tk.Label(root, text="No file open", anchor="w", relief="sunken")
        self.status.pack(side="bottom", fill="x")

        # HTML frame without scrollbars
        self.html_frame = HtmlFrame(
            root,
            messages_enabled=False,
            vertical_scrollbar=False,
            horizontal_scrollbar=False,
            on_link_click=self._handle_link,
        )
        self.html_frame.pack(fill="both", expand=True)

        self.current_file = None
        self.zoom = 1.0
        self.base_width = 900
        self.base_height = 700

        self._bind_keys()
        self._show_placeholder("logo.png")

    def _show_placeholder(self, image_path=None):
        """Show the first screen: centered image (if any) on BG_COLOR."""
        if image_path and os.path.isfile(image_path):
            img_url = self._to_file_url(image_path)
            inner = f'<img src="{img_url}" style="max-width:60%;max-height:60vh;">'
        else:
            inner = '<div style="color:#888;font-family:sans-serif;">Open a Markdown file with Ctrl+O</div>'

        self.html_frame.load_html(
            f'<html><body style="margin:0;background:{self.BG_COLOR};'
            'display:flex;align-items:center;justify-content:center;height:100vh;">'
            f'{inner}</body></html>'
        )

    # ------------------------------------------------------------------ #
    #  Link handling
    # ------------------------------------------------------------------ #
    def _handle_link(self, url):
        """Images displayed in place, other links copied to clipboard."""
        if not url:
            return

        clean_url = url.split("?")[0].split("#")[0].lower()
        image_exts = (".png", ".jpg", ".jpeg", ".gif", ".bmp", ".webp", ".ico", ".svg")

        if clean_url.endswith(image_exts):
            img_html = (
                '<html><body style="margin:0;display:flex;'
                'justify-content:center;align-items:center;height:100vh;">'
                f'<img src="{url}" style="max-width:100%;max-height:100vh;"></body></html>'
            )
            self.html_frame.load_html(img_html)
            self.status.config(text=f"Image: {url}")
        else:
            try:
                self.root.clipboard_clear()
                self.root.clipboard_append(url)
                self.root.update()
                self.status.config(text=f"Link copied: {url}")
            except Exception as e:
                self.status.config(text=f"Copy error: {e}")

    # ------------------------------------------------------------------ #
    #  Keybindings
    # ------------------------------------------------------------------ #
    # ------------------------------------------------------------------ #
    #  Keybindings
    # ------------------------------------------------------------------ #
    def _bind_keys(self):
        r = self.root
        r.bind("<Control-o>", lambda e: self.open_file())
        r.bind("r", lambda e: self.reload_file())
        r.bind("q", lambda e: r.quit())

        # Zoom in: Ctrl + / Ctrl = / numpad, AND bare + / = / KP_Add.
        # Tk delivers Shift+= as "<plus>", so bind both "<plus>" and "<equal>".
        for seq in ("<Control-plus>", "<Control-equal>", "<Control-KP_Add>",
                    "<plus>", "<equal>", "<KP_Add>"):
            r.bind(seq, lambda e: self.change_zoom(+1))

        # Zoom out
        for seq in ("<Control-minus>", "<Control-KP_Subtract>",
                    "<minus>", "<KP_Subtract>"):
            r.bind(seq, lambda e: self.change_zoom(-1))

        # Reset
        for seq in ("<Control-0>", "<Control-KP_0>",
                    "<Key-0>", "<KP_0>"):
            r.bind(seq, lambda e: self.reset_zoom())

        # Vim movement
        r.bind("j", lambda e: self.scroll_lines(+1))
        r.bind("k", lambda e: self.scroll_lines(-1))
        r.bind("h", lambda e: self.scroll_x(-1))
        r.bind("l", lambda e: self.scroll_x(+1))
        r.bind("<Control-d>", lambda e: self.scroll_lines(+15))
        r.bind("<Control-u>", lambda e: self.scroll_lines(-15))
        r.bind("<Control-f>", lambda e: self.scroll_page(+1))
        r.bind("<Control-b>", lambda e: self.scroll_page(-1))
        r.bind("G", lambda e: self.scroll_to_bottom())
        self._g_pending = False
        r.bind("g", self._on_g)
    def _on_g(self, event):
        if self._g_pending:
            self._g_pending = False
            self.scroll_to_top()
        else:
            self._g_pending = True
            self.root.after(500, lambda: setattr(self, "_g_pending", False))

    # ------------------------------------------------------------------ #
    #  Scroll helpers
    # ------------------------------------------------------------------ #
    def _html_widget(self):
        return getattr(self.html_frame, "html", None)

    def scroll_lines(self, n):
        w = self._html_widget()
        if w:
            try:
                w.yview_scroll(n, "units")
            except Exception:
                pass

    def scroll_x(self, n):
        w = self._html_widget()
        if w:
            try:
                w.xview_scroll(n * 3, "units")
            except Exception:
                pass

    def scroll_page(self, direction):
        w = self._html_widget()
        if w:
            try:
                w.yview_scroll(direction, "pages")
            except Exception:
                pass

    def scroll_to_top(self):
        w = self._html_widget()
        if w:
            try:
                w.yview_moveto(0.0)
            except Exception:
                pass

    def scroll_to_bottom(self):
        w = self._html_widget()
        if w:
            try:
                w.yview_moveto(1.0)
            except Exception:
                pass

    # ------------------------------------------------------------------ #
    #  Zoom
    # ------------------------------------------------------------------ #
    def change_zoom(self, direction):
        new_zoom = self.zoom * (self.ZOOM_STEP if direction > 0 else 1 / self.ZOOM_STEP)
        new_zoom = max(self.ZOOM_MIN, min(self.ZOOM_MAX, new_zoom))
        if abs(new_zoom - self.zoom) < 1e-6:
            return
        self.zoom = new_zoom
        self._apply_zoom()

    def reset_zoom(self):
        if abs(self.zoom - 1.0) < 1e-6:
            return
        self.zoom = 1.0
        self._apply_zoom()

    def _apply_zoom(self):
        """Re-render with new zoom, and resize the window if appropriate."""
        # Resize the window only when it's in the normal (not fullscreen /
        # not maximized / not iconified) state.  Fullscreen WMs ignore
        # geometry() anyway, and calling it there is what caused the
        # original "zoom is broken in fullscreen" bug.
        try:
            in_normal_state = self.root.state() == "normal"
        except Exception:
            in_normal_state = True

        if in_normal_state:
            sw = self.root.winfo_screenwidth()
            sh = self.root.winfo_screenheight()
            target_w = max(480, min(int(self.base_width * self.zoom), int(sw * 0.98)))
            target_h = max(360, min(int(self.base_height * self.zoom), int(sh * 0.92)))
            self.root.geometry(f"{target_w}x{target_h}")

        # Re-render so the zoom factor is baked into the CSS.
        if self.current_file:
            self.load_file(self.current_file)
        else:
            self._show_placeholder("logo.png")

        self.status.config(text=f"Zoom: {self.zoom:.0%}")
    # ------------------------------------------------------------------ #
    #  File loading
    # ------------------------------------------------------------------ #
    def open_file(self):
        path = filedialog.askopenfilename(
            title="Open Markdown file",
            filetypes=[("Markdown", "*.md *.markdown"), ("All files", "*.*")],
        )
        if path:
            self.load_file(path)

    def load_file(self, path):
        try:
            with open(path, "r", encoding="utf-8") as f:
                text = f.read()
        except Exception as e:
            messagebox.showerror("Error", f"Cannot read file:\n{e}")
            return

        self.current_file = path
        self.root.title(f"Markdown Viewer - {os.path.basename(path)}")
        self.status.config(text=path)
        self.render(text, base_dir=os.path.dirname(os.path.abspath(path)))

    def reload_file(self):
        if self.current_file:
            self.load_file(self.current_file)

    # ------------------------------------------------------------------ #
    #  Image source resolution
    # ------------------------------------------------------------------ #
    @staticmethod
    def _is_remote(src: str) -> bool:
        return bool(re.match(r"^(https?:|data:|ftp:)", src, re.IGNORECASE))

    @staticmethod
    def _to_file_url(path: str) -> str:
        return "file://" + quote(os.path.abspath(path), safe="/:")

    def _resolve_image_src(self, src: str, base_dir: str) -> str:
        src = src.strip()
        if not src:
            return src
        if self._is_remote(src):
            return src
        if src.startswith("file://"):
            return "file://" + quote(urlparse(src).path, safe="/:")

        src = os.path.expanduser(os.path.expandvars(src))
        candidate = src if os.path.isabs(src) else os.path.join(base_dir, src)
        return self._to_file_url(candidate)

    def _rewrite_img_srcs(self, html_body: str, base_dir: str) -> str:
        pattern = re.compile(
            r'(<img\b[^>]*?\bsrc\s*=\s*)(["\'])(.*?)\2', re.IGNORECASE | re.DOTALL
        )

        def repl(m):
            prefix, quote_char, src = m.group(1), m.group(2), m.group(3)
            new_src = self._resolve_image_src(src, base_dir)
            return f"{prefix}{quote_char}{new_src}{quote_char}"

        return pattern.sub(repl, html_body)

    # ------------------------------------------------------------------ #
    #  Rendering
    # ------------------------------------------------------------------ #
    def render(self, md_text, base_dir=None):
        base_dir = base_dir or os.getcwd()

        html_body = markdown.markdown(md_text, extensions=["extra", "toc", "sane_lists"])

        html_body = self._rewrite_img_srcs(html_body, base_dir)

        # Zoom is baked into the base font size. 1.0 == 15px.
        base_font = 15 * self.zoom

        html = f"""<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<style>
    html, body {{ margin: 0; padding: 0; background: #ffffff; }}
    body {{
        font-family: -apple-system, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
        font-size: {base_font:.1f}px; line-height: 1.6; color: #24292e;
        max-width: 860px; margin: 0 auto; padding: 24px;
    }}
    h1, h2, h3, h4, h5, h6 {{
        border-bottom: 1px solid #eaecef; padding-bottom: 0.3em; margin-top: 1.2em;
    }}
    code {{
        background: #f6f8fa; padding: 0.2em 0.4em; border-radius: 3px;
        font-family: "SFMono-Regular", Consolas, "Liberation Mono", monospace; font-size: 85%;
    }}
    pre {{ background: #f6f8fa; padding: 16px; border-radius: 6px; overflow: auto; }}
    pre code {{ background: transparent; padding: 0; }}
    table {{ border-collapse: collapse; margin: 1em 0; }}
    th, td {{ border: 1px solid #dfe2e5; padding: 6px 13px; }}
    th {{ background: #f6f8fa; }}
    img {{ max-width: 100%; height: auto; display: block; margin: 1em auto; }}
    blockquote {{ border-left: 4px solid #dfe2e5; color: #6a737d; margin: 0; padding-left: 1em; }}
    a {{ color: #0366d6; }}
</style>
</head>
<body>
{html_body}
</body>
</html>
"""
        self.html_frame.load_html(html)


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else None
    if path and not os.path.isfile(path):
        print(f"File not found: {path}")
        sys.exit(1)

    root = tk.Tk()
    app = MarkdownViewer(root)
    if path:
        app.load_file(path)
    root.mainloop()


if __name__ == "__main__":
    main()
