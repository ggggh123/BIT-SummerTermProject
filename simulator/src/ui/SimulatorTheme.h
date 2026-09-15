#pragma once

#include <QFont>
#include <QPalette>
#include <QWidget>

namespace ev::simulator {
inline void applySimulatorTheme(QWidget *window)
{
    QFont font(QStringLiteral("Noto Sans CJK SC"));
    font.setPixelSize(13);
    window->setFont(font);
    QPalette palette = window->palette();
    palette.setColor(QPalette::Window, QColor("#102536"));
    palette.setColor(QPalette::Base, QColor("#102536"));
    palette.setColor(QPalette::Text, QColor("#eaf3f6"));
    palette.setColor(QPalette::WindowText, QColor("#eaf3f6"));
    palette.setColor(QPalette::Highlight, QColor("#234858"));
    palette.setColor(QPalette::HighlightedText, QColor("#eaf3f6"));
    window->setPalette(palette);
    window->setAutoFillBackground(true);
    // 限定在当前窗口，不覆盖集成测试中同进程的用户端。
    window->setStyleSheet(QStringLiteral(R"QSS(
QWidget { color: #eaf3f6; font-family: "Noto Sans CJK SC"; font-size: 13px; }
QLabel { background: transparent; border: none; }
QWidget#simRail { background: #0b1d2b; }
QWidget#simBody, QScrollArea#simViewport { background: #102536; border: none; }
QFrame#simRule { background: #2c4251; border: none; }
QLabel[role="brand"] { font-size: 20px; font-weight: 600; }
QLabel[role="title"] { font-size: 25px; font-weight: 600; }
QLabel[role="section"] { font-size: 16px; font-weight: 500; }
QLabel[role="muted"] { color: #97adbc; font-size: 12px; }
QLabel[role="micro"] { color: #97adbc; font-size: 11px; }
QLabel[role="number"] { font-size: 27px; }
QLabel[role="state"] { font-size: 30px; font-weight: 500; }
QLabel[role="power"] { color: #a5f1df; font-size: 49px; font-weight: 500; }
QLabel[role="unit"] { color: #97adbc; font-size: 14px; }
QLabel[role="badge"] { background: #193444; border: 1px solid #304b5b; border-radius: 7px; padding: 7px 16px; color: #97adbc; }
QLabel[tone="good"] { color: #72e0c7; }
QLabel[tone="warn"] { color: #f0af8e; }
QLabel[tone="muted"] { color: #97adbc; }
QPushButton { background: #193444; color: #c6d8e2; border: 1px solid #304b5b; border-radius: 7px; padding: 7px 12px; min-height: 22px; }
QPushButton:hover { background: #234858; border-color: #456779; }
QPushButton:pressed { background: #2b5260; }
QPushButton:focus { border-color: #a5f1df; }
QPushButton:disabled { background: #152d3b; color: #7692a4; border-color: #2c4251; }
QPushButton[role="primary"] { background: #72ddc3; border-color: #72ddc3; color: #102d35; font-weight: 600; }
QPushButton[role="primary"]:hover { background: #8ae5d0; }
QPushButton[role="danger"] { background: #3d3037; border-color: #765052; color: #f1ae98; }
QPushButton[role="danger"]:hover { background: #49333a; }
QPushButton[role="danger"]:disabled { background: #232b35; border-color: #40404a; color: #927e89; }
QTableWidget, QListWidget { background: #102536; alternate-background-color: #142c3c; border: none; selection-background-color: #234858; selection-color: #eaf3f6; outline: 0; }
QTableWidget::item { padding: 5px 9px; border-bottom: 1px solid #213c4b; }
QTableWidget::item:selected { background: #234858; color: #eaf3f6; }
QTableWidget:focus { border: 1px solid #456779; }
QHeaderView { background: #193444; }
QHeaderView::section { background: #193444; color: #97adbc; padding: 9px; border: none; font-size: 12px; }
QListWidget::item { padding: 8px 0; border-bottom: 1px solid #213c4b; }
QListWidget::item:selected { background: #234858; }
QScrollBar:vertical { background: #102536; width: 7px; margin: 0; }
QScrollBar::handle:vertical { background: #355161; min-height: 26px; border-radius: 3px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
QToolTip { background: #193444; color: #eaf3f6; border: 1px solid #456779; padding: 6px; }
)QSS"));
}
} // namespace ev::simulator
