"""Minimal syntax highlighting for the conkyrc editor -- issue #31 milestone 3.

Scope is deliberately narrow per the issue's own plan: just ${...} object
references highlighted distinctly. Expand later (full Lua highlighting,
comments, strings, ...) if it turns out to matter in practice.
"""

from __future__ import annotations

from PySide6.QtGui import QColor, QSyntaxHighlighter, QTextCharFormat

import backport_conkyrc


class ConkyHighlighter(QSyntaxHighlighter):
    def __init__(self, document) -> None:
        super().__init__(document)
        self._object_format = QTextCharFormat()
        self._object_format.setForeground(QColor("#4EC9B0"))
        self._object_format.setFontWeight(700)

    def highlightBlock(self, text: str) -> None:
        for start, end, _name, _args in backport_conkyrc.scan_objects(text):
            self.setFormat(start, end - start, self._object_format)
