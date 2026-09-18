"""Object reference browser -- issue #39 milestone v1.

A non-modal, searchable list of every real conky object name (sourced
from backport_conkyrc.KNOWN_OBJECTS, which is parsed live from
src/core.cc's OBJ*/OBJ_IF* registration macros, so it never goes stale).
Double-click (or Enter) inserts "${name}" at the editor's cursor, cursor
left positioned right before the closing brace so typing arguments is a
single continuation, not a second click.

No argument-signature data exists anywhere in this codebase in
structured form (see the issue), so that's out of scope here -- this is
names only, a browsable reference and quick-insert, not full IntelliSense.
"""

from __future__ import annotations

from PySide6.QtCore import Qt, Signal
from PySide6.QtGui import QTextCursor
from PySide6.QtWidgets import (
    QDialog,
    QLabel,
    QLineEdit,
    QListWidget,
    QPushButton,
    QVBoxLayout,
)

import backport_conkyrc


class ObjectHelperDialog(QDialog):
    object_chosen = Signal(str)

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self.setWindowTitle("Insert Object")
        self.resize(320, 480)
        # Non-modal: stays open so several objects can be inserted in a
        # row while composing a theme, without reopening each time.
        self.setModal(False)

        self._names = sorted(backport_conkyrc.KNOWN_OBJECTS)

        layout = QVBoxLayout(self)

        self.search = QLineEdit(self)
        self.search.setPlaceholderText("Filter objects...")
        self.search.textChanged.connect(self._refilter)
        layout.addWidget(self.search)

        self.list = QListWidget(self)
        self.list.addItems(self._names)
        self.list.itemActivated.connect(self._on_activated)
        layout.addWidget(self.list)

        hint = QLabel(
            "Double-click or Enter inserts ${name} at the cursor.\n"
            "Names only -- no argument info available (see issue #39).",
            self,
        )
        hint.setWordWrap(True)
        layout.addWidget(hint)

        close_button = QPushButton("Close", self)
        close_button.clicked.connect(self.close)
        layout.addWidget(close_button)

        self.search.setFocus()

    def _refilter(self, text: str) -> None:
        needle = text.strip().lower()
        self.list.clear()
        self.list.addItems(
            [n for n in self._names if needle in n.lower()] if needle else self._names
        )

    def _on_activated(self, item) -> None:
        self.object_chosen.emit(item.text())


def insert_object_reference(editor, name: str) -> None:
    """Insert "${name}" at editor's cursor, cursor left before the "}"."""
    cursor = editor.textCursor()
    cursor.insertText(f"${{{name}}}")
    cursor.movePosition(QTextCursor.MoveOperation.Left)
    editor.setTextCursor(cursor)
    editor.setFocus()
