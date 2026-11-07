"""QPlainTextEdit with a line-number gutter and inline object autocomplete.

Line numbers: standard Qt "Code Editor" pattern -- a small side widget
that paints line numbers, kept in sync with the editor's own block
layout/scroll position. One number per logical line (QTextBlock), drawn
once at the top of that block's bounding rect -- with word-wrap on, a
long line spanning several visual rows still gets exactly one number, at
its first row, so wrapped text stays traceable back to its actual line
instead of miscounting.

Autocomplete (issue #39 v2): a QCompleter popup appears while typing an
object name right after "${", narrowing as you keep typing, sourced from
the same live-parsed backport_conkyrc.KNOWN_OBJECTS the lint panel and
object browser (object_helper.py) already use. Tab/Enter/click accepts,
completing just the remaining characters -- text already typed and any
following "}" are left untouched. Overriding keyPressEvent has to happen
via a real subclass method (not attached/monkeypatched onto an instance
afterward) since Qt dispatches virtual methods like this through the
C++ vtable, which only sees class-level overrides.
"""

from __future__ import annotations

import re

from PySide6.QtCore import QRect, QSize, Qt
from PySide6.QtGui import QColor, QPainter, QTextCursor
from PySide6.QtWidgets import QApplication, QCompleter, QPlainTextEdit, QWidget

import backport_conkyrc

_PREFIX_RE = re.compile(r"\$\{(\w*)$")


class _LineNumberArea(QWidget):
    def __init__(self, editor: "LineNumberTextEdit") -> None:
        super().__init__(editor)
        self._editor = editor

    def sizeHint(self) -> QSize:
        return QSize(self._editor.line_number_area_width(), 0)

    def paintEvent(self, event) -> None:
        self._editor.line_number_area_paint_event(event)


class LineNumberTextEdit(QPlainTextEdit):
    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self._visible = True
        self._line_number_area = _LineNumberArea(self)
        self.blockCountChanged.connect(self._update_line_number_area_width)
        self.updateRequest.connect(self._update_line_number_area)
        self._update_line_number_area_width()

        self._completer = QCompleter(sorted(backport_conkyrc.KNOWN_OBJECTS), self)
        self._completer.setCaseSensitivity(Qt.CaseSensitivity.CaseInsensitive)
        self._completer.setCompletionMode(QCompleter.CompletionMode.PopupCompletion)
        self._completer.setFilterMode(Qt.MatchFlag.MatchStartsWith)
        self._completer.setWidget(self)
        self._completer.activated.connect(self._insert_completion)

    # -- line numbers ----------------------------------------------------

    def set_line_numbers_visible(self, visible: bool) -> None:
        if visible == self._visible:
            return
        self._visible = visible
        self._update_line_number_area_width()
        self._line_number_area.setVisible(visible)

    def line_number_area_width(self) -> int:
        if not self._visible:
            return 0
        digits = len(str(max(1, self.blockCount())))
        return 12 + self.fontMetrics().horizontalAdvance("9") * digits

    def _update_line_number_area_width(self, _count: int = 0) -> None:
        self.setViewportMargins(self.line_number_area_width(), 0, 0, 0)

    def _update_line_number_area(self, rect: QRect, dy: int) -> None:
        if dy:
            self._line_number_area.scroll(0, dy)
        else:
            self._line_number_area.update(
                0, rect.y(), self._line_number_area.width(), rect.height()
            )
        if rect.contains(self.viewport().rect()):
            self._update_line_number_area_width()

    def resizeEvent(self, event) -> None:
        super().resizeEvent(event)
        cr = self.contentsRect()
        self._line_number_area.setGeometry(
            QRect(cr.left(), cr.top(), self.line_number_area_width(), cr.height())
        )

    def line_number_area_paint_event(self, event) -> None:
        painter = QPainter(self._line_number_area)
        painter.fillRect(event.rect(), QColor("#1e1e1e"))
        painter.setPen(QColor("#6e7681"))

        block = self.firstVisibleBlock()
        block_number = block.blockNumber()
        top = round(
            self.blockBoundingGeometry(block).translated(self.contentOffset()).top()
        )
        bottom = top + round(self.blockBoundingRect(block).height())
        width = self._line_number_area.width()
        height = self.fontMetrics().height()

        while block.isValid() and top <= event.rect().bottom():
            if block.isVisible() and bottom >= event.rect().top():
                painter.drawText(
                    0, top, width - 6, height,
                    Qt.AlignmentFlag.AlignRight, str(block_number + 1),
                )
            block = block.next()
            top = bottom
            bottom = top + round(self.blockBoundingRect(block).height())
            block_number += 1

    # -- autocomplete ------------------------------------------------------

    def _current_prefix(self) -> str | None:
        """Text typed so far since the nearest unclosed "${" before the
        cursor on this line, or None if the cursor isn't in that position
        (so the popup only offers object names, not anywhere in the file).
        """
        cursor = self.textCursor()
        text_before = cursor.block().text()[: cursor.positionInBlock()]
        m = _PREFIX_RE.search(text_before)
        return m.group(1) if m else None

    def _insert_completion(self, completion: str) -> None:
        prefix = self._completer.completionPrefix()
        cursor = self.textCursor()
        cursor.movePosition(
            QTextCursor.MoveOperation.Left,
            QTextCursor.MoveMode.KeepAnchor,
            len(prefix),
        )
        cursor.insertText(completion)
        # Close the brace too -- unless one is already sitting right where
        # the cursor now is (e.g. completing inside "${cp|}" typed by
        # hand), in which case just step over it instead of doubling up.
        doc_text = self.toPlainText()
        pos = cursor.position()
        if doc_text[pos : pos + 1] == "}":
            cursor.movePosition(QTextCursor.MoveOperation.Right)
        else:
            cursor.insertText("}")
            cursor.movePosition(QTextCursor.MoveOperation.Left)
        self.setTextCursor(cursor)

    def keyPressEvent(self, event) -> None:
        popup = self._completer.popup()
        if popup.isVisible():
            if event.key() in (Qt.Key.Key_Enter, Qt.Key.Key_Return, Qt.Key.Key_Tab):
                # Accept the currently highlighted completion ourselves --
                # QCompleter's popup only handles this automatically when
                # it has keyboard focus itself, which it doesn't here (the
                # editor keeps focus so typing keeps landing in the
                # document, not the popup).
                index = popup.currentIndex()
                if index.isValid():
                    self._insert_completion(index.data())
                popup.hide()
                event.accept()
                return
            if event.key() in (Qt.Key.Key_Escape, Qt.Key.Key_Backtab):
                popup.hide()
                event.accept()
                return
            if event.key() in (Qt.Key.Key_Up, Qt.Key.Key_Down):
                # Let the popup's own list navigation handle these instead
                # of moving the text cursor up/down a line underneath it.
                QApplication.sendEvent(popup, event)
                return

        super().keyPressEvent(event)

        prefix = self._current_prefix()
        if prefix is None:
            popup.hide()
            return

        if prefix != self._completer.completionPrefix():
            self._completer.setCompletionPrefix(prefix)
            popup.setCurrentIndex(self._completer.completionModel().index(0, 0))

        if self._completer.completionCount() == 0:
            popup.hide()
            return

        rect = self.cursorRect()
        rect.setWidth(
            popup.sizeHintForColumn(0) + popup.verticalScrollBar().sizeHint().width()
        )
        self._completer.complete(rect)
