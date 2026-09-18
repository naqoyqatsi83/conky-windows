#!/usr/bin/env python3
"""conky_editor - live-preview GUI config editor for this Windows port.

See issue #31. A QPlainTextEdit for the raw conkyrc text, a positioning
grid that mirrors conky's own alignment/gap_x/gap_y/xinerama_head model
(see issue #32 for the xinerama_head/multi-monitor support this drives),
and a managed conky.exe subprocess (preview.py) that re-renders against a
debounced temp-file write on every edit -- the preview is the real
renderer, not a second implementation of it.

Usage:
    python tools/conky_editor/main.py [path/to/theme.conkyrc]
"""

from __future__ import annotations

import argparse
import sys
import tempfile
from pathlib import Path

from PySide6.QtCore import QTimer
from PySide6.QtGui import QColor
from PySide6.QtWidgets import (
    QApplication,
    QButtonGroup,
    QComboBox,
    QFileDialog,
    QGridLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QListWidget,
    QListWidgetItem,
    QMainWindow,
    QPlainTextEdit,
    QPushButton,
    QRadioButton,
    QSpinBox,
    QVBoxLayout,
    QWidget,
)

# tools/backport_conkyrc.py is a sibling of this tools/conky_editor/ package,
# not on sys.path by default when running main.py as a script.
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import backport_conkyrc
import config_io
import monitors
from highlighter import ConkyHighlighter
from preview import PreviewProcess

_LINT_COLORS = {
    "ERROR": "#F14C4C",
    "UNSUPPORTED": "#F14C4C",
    "UNKNOWN": "#D19A66",
    "EXEC (manual)": "#D19A66",
    "REVIEW": "#DCDCAA",
    "CONFIG": "#DCDCAA",
    "EXEC (auto)": "#9CDCFE",
}

DEBOUNCE_MS = 400

DEFAULT_TEMPLATE = """conky.config = {
  alignment = 'top_right',
  own_window = true,
  gap_x = 20, gap_y = 60,
  font = 'Consolas:size=10',
  default_color = 'white',
  update_interval = 1.0,
}

conky.text = [[
${font Segoe UI:bold:size=12}${alignc}System Monitor
${hr 2}
Uptime: ${alignr}$uptime
CPU:    ${alignr}${cpu}%
]];
"""

_GRID_POSITIONS = [
    ("top_left", "top_middle", "top_right"),
    ("middle_left", "middle_middle", "middle_right"),
    ("bottom_left", "bottom_middle", "bottom_right"),
]


class ConkyEditorWindow(QMainWindow):
    def __init__(self, initial_path: Path | None, conky_exe: Path | None = None) -> None:
        super().__init__()
        self.setWindowTitle("Conky Editor")
        self.resize(1000, 700)

        self._current_path: Path | None = None
        self._preview = PreviewProcess(conky_exe) if conky_exe else PreviewProcess()
        self._temp_file = tempfile.NamedTemporaryFile(
            suffix=".conkyrc", delete=False, mode="w"
        )
        self._temp_path = Path(self._temp_file.name)
        self._temp_file.close()

        self._debounce = QTimer(self)
        self._debounce.setSingleShot(True)
        self._debounce.setInterval(DEBOUNCE_MS)
        self._debounce.timeout.connect(self._on_debounce)

        self._build_ui()
        self._highlighter = ConkyHighlighter(self.editor.document())

        if initial_path is not None:
            self._load_file(initial_path)
        else:
            self._set_editor_text(DEFAULT_TEMPLATE)
            self._sync_controls_from_text()
            self._update_lint()

    # -- UI construction ----------------------------------------------

    def _build_ui(self) -> None:
        central = QWidget(self)
        self.setCentralWidget(central)
        layout = QHBoxLayout(central)

        self.editor = QPlainTextEdit(self)
        self.editor.setLineWrapMode(QPlainTextEdit.LineWrapMode.NoWrap)
        self.editor.textChanged.connect(self._on_text_changed)
        layout.addWidget(self.editor, stretch=3)

        side = QVBoxLayout()
        layout.addLayout(side, stretch=1)

        self.preview_button = QPushButton("Start Preview", self)
        self.preview_button.clicked.connect(self._toggle_preview)
        side.addWidget(self.preview_button)

        open_button = QPushButton("Open...", self)
        open_button.clicked.connect(self._open_dialog)
        side.addWidget(open_button)

        save_button = QPushButton("Save", self)
        save_button.clicked.connect(self._save)
        side.addWidget(save_button)

        side.addWidget(self._build_position_group())

        self.status_label = QLabel("", self)
        self.status_label.setWordWrap(True)
        side.addWidget(self.status_label)

        side.addWidget(self._build_lint_group(), stretch=1)

    def _build_lint_group(self) -> QGroupBox:
        group = QGroupBox("Lint (backport_conkyrc.py)", self)
        layout = QVBoxLayout(group)
        self.lint_summary_label = QLabel("", self)
        self.lint_summary_label.setWordWrap(True)
        layout.addWidget(self.lint_summary_label)
        self.lint_list = QListWidget(group)
        layout.addWidget(self.lint_list)
        return group

    def _build_position_group(self) -> QGroupBox:
        group = QGroupBox("Position", self)
        grid = QGridLayout(group)

        grid.addWidget(QLabel("monitor"), 0, 0)
        self.monitor_combo = QComboBox(group)
        self.monitor_combo.addItem("Default (primary)", -1)
        for mon in monitors.list_monitors():
            self.monitor_combo.addItem(mon.label, mon.index)
        self.monitor_combo.currentIndexChanged.connect(self._on_monitor_changed)
        grid.addWidget(self.monitor_combo, 0, 1, 1, 2)

        self._alignment_group = QButtonGroup(self)
        self._alignment_buttons: dict[str, QRadioButton] = {}
        for row, cells in enumerate(_GRID_POSITIONS):
            for col, alignment in enumerate(cells):
                button = QRadioButton(alignment, group)
                button.toggled.connect(self._on_alignment_toggled(alignment))
                self._alignment_group.addButton(button)
                self._alignment_buttons[alignment] = button
                grid.addWidget(button, row + 1, col)

        grid.addWidget(QLabel("gap_x"), 4, 0)
        self.gap_x_spin = QSpinBox(group)
        self.gap_x_spin.setRange(-2000, 4000)
        self.gap_x_spin.valueChanged.connect(self._on_gap_changed)
        grid.addWidget(self.gap_x_spin, 4, 1, 1, 2)

        grid.addWidget(QLabel("gap_y"), 5, 0)
        self.gap_y_spin = QSpinBox(group)
        self.gap_y_spin.setRange(-2000, 4000)
        self.gap_y_spin.valueChanged.connect(self._on_gap_changed)
        grid.addWidget(self.gap_y_spin, 5, 1, 1, 2)

        return group

    # -- file I/O -------------------------------------------------------

    def _load_file(self, path: Path) -> None:
        text = path.read_text(encoding="utf-8")
        self._current_path = path
        self._set_editor_text(text)
        self._sync_controls_from_text()
        self._update_lint()
        self.setWindowTitle(f"Conky Editor - {path.name}")

    def _set_editor_text(self, text: str) -> None:
        """Populate the editor without arming the preview debounce timer.

        A programmatic load isn't an edit -- it shouldn't queue a restart
        that could fire moments after the user first clicks Start Preview.
        """
        self.editor.blockSignals(True)
        self.editor.setPlainText(text)
        self.editor.blockSignals(False)
        self._debounce.stop()

    def _open_dialog(self) -> None:
        start_dir = str(self._current_path.parent) if self._current_path else ""
        filename, _ = QFileDialog.getOpenFileName(
            self, "Open conkyrc", start_dir,
            "Conky config (conkyrc *.conkyrc);;All files (*)",
        )
        if filename:
            self._load_file(Path(filename))

    def _save(self) -> None:
        if self._current_path is None:
            filename, _ = QFileDialog.getSaveFileName(
                self, "Save conkyrc", "",
                "Conky config (conkyrc *.conkyrc);;All files (*)",
            )
            if not filename:
                return
            self._current_path = Path(filename)
        self._current_path.write_text(self.editor.toPlainText(), encoding="utf-8")
        self.setWindowTitle(f"Conky Editor - {self._current_path.name}")

    # -- preview lifecycle -----------------------------------------------

    def _toggle_preview(self) -> None:
        if self._preview.running:
            self._preview.stop()
            self.preview_button.setText("Start Preview")
            self.status_label.setText("Preview stopped.")
        else:
            if not self._preview.conky_exe.exists():
                self.status_label.setText(
                    f"conky.exe not found at {self._preview.conky_exe} "
                    "-- pass --conky-exe PATH or place conky.exe next to this app."
                )
                return
            self._write_temp_file()
            self._preview.start(self._temp_path)
            self.preview_button.setText("Stop Preview")
            self.status_label.setText(f"Previewing {self._temp_path}")

    def _write_temp_file(self) -> None:
        self._temp_path.write_text(self.editor.toPlainText(), encoding="utf-8")

    def _apply_to_preview(self) -> None:
        if not self._preview.running:
            return
        self._write_temp_file()
        self._preview.restart(self._temp_path)

    def _on_debounce(self) -> None:
        self._update_lint()
        self._apply_to_preview()

    def _update_lint(self) -> None:
        report, _rewritten = backport_conkyrc.lint(self.editor.toPlainText())
        self.lint_list.clear()
        for finding in report.findings:
            item = QListWidgetItem(
                f"L{finding.line}  {finding.token}\n{finding.category}: {finding.message}"
            )
            color = _LINT_COLORS.get(finding.category, "#CCCCCC")
            item.setForeground(QColor(color))
            self.lint_list.addItem(item)
        self.lint_summary_label.setText(
            f"{report.ok_count} OK, {len(report.findings)} finding(s)"
        )

    # -- editor / control wiring -----------------------------------------

    def _on_text_changed(self) -> None:
        self._debounce.start()

    def _on_alignment_toggled(self, alignment: str):
        def handler(checked: bool) -> None:
            if not checked:
                return
            self._rewrite_text(lambda t: config_io.write_alignment(t, alignment))

        return handler

    def _on_monitor_changed(self, _index: int) -> None:
        head = self.monitor_combo.currentData()
        self._rewrite_text(lambda t: config_io.write_xinerama_head(t, head))

    def _on_gap_changed(self, _value: int) -> None:
        self._rewrite_text(
            lambda t: config_io.write_gap_y(
                config_io.write_gap_x(t, self.gap_x_spin.value()),
                self.gap_y_spin.value(),
            )
        )

    def _rewrite_text(self, transform) -> None:
        text = self.editor.toPlainText()
        new_text = transform(text)
        if new_text == text:
            return
        cursor_pos = self.editor.textCursor().position()
        self.editor.blockSignals(True)
        self.editor.setPlainText(new_text)
        self.editor.blockSignals(False)
        cursor = self.editor.textCursor()
        cursor.setPosition(min(cursor_pos, len(new_text)))
        self.editor.setTextCursor(cursor)
        self._debounce.start()

    def _sync_controls_from_text(self) -> None:
        text = self.editor.toPlainText()
        alignment = config_io.read_alignment(text)
        gap_x = config_io.read_gap_x(text)
        gap_y = config_io.read_gap_y(text)
        head = config_io.read_xinerama_head(text)
        if head is None:
            head = -1

        for name, button in self._alignment_buttons.items():
            button.blockSignals(True)
            button.setChecked(name == alignment)
            button.blockSignals(False)

        self.monitor_combo.blockSignals(True)
        combo_index = self.monitor_combo.findData(head)
        self.monitor_combo.setCurrentIndex(combo_index if combo_index >= 0 else 0)
        self.monitor_combo.blockSignals(False)

        self.gap_x_spin.blockSignals(True)
        self.gap_x_spin.setValue(gap_x if gap_x is not None else 0)
        self.gap_x_spin.blockSignals(False)

        self.gap_y_spin.blockSignals(True)
        self.gap_y_spin.setValue(gap_y if gap_y is not None else 0)
        self.gap_y_spin.blockSignals(False)

    # -- lifecycle ---------------------------------------------------------

    def closeEvent(self, event) -> None:
        self._preview.stop()
        try:
            self._temp_path.unlink(missing_ok=True)
        except OSError:
            pass
        super().closeEvent(event)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "config", nargs="?", type=Path, help="conkyrc file to open on startup"
    )
    parser.add_argument(
        "--conky-exe",
        type=Path,
        default=None,
        help="path to conky.exe (default: build/src/conky.exe in the repo, "
        "or conky.exe next to this app when packaged)",
    )
    args = parser.parse_args()

    if args.config is not None and not args.config.exists():
        print(f"error: {args.config} does not exist", file=sys.stderr)
        return 1

    app = QApplication(sys.argv)
    window = ConkyEditorWindow(args.config, args.conky_exe)
    window.show()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
