#include "ui/AdminTheme.h"

#include <QApplication>
#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QStyleFactory>
#include <QVariant>

namespace {

// 以 24 × 24 逻辑坐标绘制，避免依赖系统图标主题或额外的 SVG 插件。
void paintIcon(QPainter &painter, const QString &name)
{
    if (name == QStringLiteral("dashboard")) {
        for (const auto &rect : {QRectF(3, 3, 7, 7), QRectF(14, 3, 7, 7),
                                QRectF(3, 14, 7, 7), QRectF(14, 14, 7, 7)})
            painter.drawRoundedRect(rect, 1.5, 1.5);
    } else if (name == QStringLiteral("chargers") || name == QStringLiteral("charger")) {
        painter.drawRoundedRect(QRectF(5, 3, 10, 18), 2, 2);
        painter.drawLine(QPointF(3, 21), QPointF(17, 21));
        QPainterPath cable;
        cable.moveTo(15, 8);
        cable.cubicTo(22, 8, 21, 13, 21, 17);
        cable.cubicTo(21, 21, 17, 21, 17, 17);
        painter.drawPath(cable);
        QPainterPath bolt;
        bolt.moveTo(11, 6);
        bolt.lineTo(8, 11);
        bolt.lineTo(12, 11);
        bolt.lineTo(9, 16);
        painter.drawPath(bolt);
    } else if (name == QStringLiteral("stations") || name == QStringLiteral("station")) {
        QPainterPath roof;
        roof.moveTo(3, 9);
        roof.lineTo(12, 3);
        roof.lineTo(21, 9);
        painter.drawPath(roof);
        painter.drawRoundedRect(QRectF(5, 9, 14, 12), 1, 1);
        painter.drawRect(QRectF(9, 13, 6, 8));
    } else if (name == QStringLiteral("users") || name == QStringLiteral("user")) {
        painter.drawEllipse(QRectF(5, 3, 7, 7));
        QPainterPath people;
        people.moveTo(2, 21);
        people.lineTo(2, 18);
        people.cubicTo(2, 11, 15, 11, 15, 18);
        people.lineTo(15, 21);
        people.moveTo(16, 4);
        people.cubicTo(21, 4, 21, 10, 17, 11);
        people.moveTo(18, 14);
        people.cubicTo(22, 15, 22, 18, 22, 21);
        painter.drawPath(people);
    } else if (name == QStringLiteral("orders") || name == QStringLiteral("logs")) {
        painter.drawRoundedRect(QRectF(5, 3, 14, 18), 2, 2);
        painter.drawLine(QPointF(9, 8), QPointF(15, 8));
        painter.drawLine(QPointF(9, 12), QPointF(15, 12));
        painter.drawLine(QPointF(9, 16), QPointF(13, 16));
    } else if (name == QStringLiteral("alerts")) {
        QPainterPath warning;
        warning.moveTo(10.3, 3.9);
        warning.quadTo(12, 1.2, 13.7, 3.9);
        warning.lineTo(22, 18.5);
        warning.quadTo(23, 21, 20, 21);
        warning.lineTo(4, 21);
        warning.quadTo(1, 21, 2, 18.5);
        warning.closeSubpath();
        painter.drawPath(warning);
        painter.drawLine(QPointF(12, 9), QPointF(12, 13));
        painter.drawPoint(QPointF(12, 17));
    } else if (name == QStringLiteral("health")) {
        QPainterPath pulse;
        pulse.moveTo(2, 12);
        pulse.lineTo(7, 12);
        pulse.lineTo(10, 4);
        pulse.lineTo(14, 20);
        pulse.lineTo(17, 12);
        pulse.lineTo(22, 12);
        painter.drawPath(pulse);
    } else if (name == QStringLiteral("server")) {
        painter.drawRoundedRect(QRectF(3, 3, 18, 7), 2, 2);
        painter.drawRoundedRect(QRectF(3, 14, 18, 7), 2, 2);
        painter.drawPoint(QPointF(7, 6.5));
        painter.drawPoint(QPointF(7, 17.5));
        painter.drawLine(QPointF(13, 6.5), QPointF(17, 6.5));
        painter.drawLine(QPointF(13, 17.5), QPointF(17, 17.5));
    } else if (name == QStringLiteral("settings")) {
        painter.drawLine(QPointF(3, 6), QPointF(21, 6));
        painter.drawLine(QPointF(3, 12), QPointF(21, 12));
        painter.drawLine(QPointF(3, 18), QPointF(21, 18));
        painter.drawEllipse(QRectF(6, 4, 4, 4));
        painter.drawEllipse(QRectF(14, 10, 4, 4));
        painter.drawEllipse(QRectF(8, 16, 4, 4));
    } else if (name == QStringLiteral("search")) {
        painter.drawEllipse(QRectF(3, 3, 13, 13));
        painter.drawLine(QPointF(15, 15), QPointF(21, 21));
    } else if (name == QStringLiteral("refresh")) {
        painter.drawArc(QRectF(4, 4, 16, 16), 45 * 16, 285 * 16);
        QPainterPath arrow;
        arrow.moveTo(15, 3);
        arrow.lineTo(20, 4);
        arrow.lineTo(19, 9);
        painter.drawPath(arrow);
    } else if (name == QStringLiteral("plus")) {
        painter.drawLine(QPointF(12, 5), QPointF(12, 19));
        painter.drawLine(QPointF(5, 12), QPointF(19, 12));
    } else if (name == QStringLiteral("arrow-right")) {
        painter.drawLine(QPointF(4, 12), QPointF(20, 12));
        QPainterPath arrow;
        arrow.moveTo(14, 6);
        arrow.lineTo(20, 12);
        arrow.lineTo(14, 18);
        painter.drawPath(arrow);
    } else if (name == QStringLiteral("bolt")) {
        QPainterPath bolt;
        bolt.moveTo(14, 2);
        bolt.lineTo(4, 14);
        bolt.lineTo(11, 14);
        bolt.lineTo(10, 22);
        bolt.lineTo(20, 10);
        bolt.lineTo(13, 10);
        bolt.closeSubpath();
        painter.drawPath(bolt);
    } else if (name == QStringLiteral("shield")) {
        QPainterPath shield;
        shield.moveTo(12, 2);
        shield.lineTo(20, 5);
        shield.lineTo(20, 11);
        shield.cubicTo(20, 17, 15, 20, 12, 22);
        shield.cubicTo(9, 20, 4, 17, 4, 11);
        shield.lineTo(4, 5);
        shield.closeSubpath();
        painter.drawPath(shield);
        QPainterPath check;
        check.moveTo(8, 11);
        check.lineTo(11, 14);
        check.lineTo(16, 9);
        painter.drawPath(check);
    } else if (name == QStringLiteral("check")) {
        QPainterPath check;
        check.moveTo(4, 12);
        check.lineTo(9, 17);
        check.lineTo(20, 6);
        painter.drawPath(check);
    } else if (name == QStringLiteral("clock")) {
        painter.drawEllipse(QRectF(3, 3, 18, 18));
        painter.drawLine(QPointF(12, 7), QPointF(12, 12));
        painter.drawLine(QPointF(12, 12), QPointF(16, 14));
    } else if (name == QStringLiteral("close")) {
        painter.drawLine(QPointF(6, 6), QPointF(18, 18));
        painter.drawLine(QPointF(18, 6), QPointF(6, 18));
    } else if (name == QStringLiteral("chevron-down")) {
        QPainterPath arrow;
        arrow.moveTo(6, 9);
        arrow.lineTo(12, 15);
        arrow.lineTo(18, 9);
        painter.drawPath(arrow);
    } else if (name == QStringLiteral("chevron-right")) {
        QPainterPath arrow;
        arrow.moveTo(9, 6);
        arrow.lineTo(15, 12);
        arrow.lineTo(9, 18);
        painter.drawPath(arrow);
    } else {
        painter.drawEllipse(QRectF(4, 4, 16, 16));
    }
}

} // namespace

namespace AdminTheme {

void apply(QApplication &application)
{
    if (application.property("adminThemeApplied").toBool())
        return;
    application.setProperty("adminThemeApplied", true);
    application.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QFont font(QStringLiteral("Noto Sans CJK SC"));
    font.setPixelSize(14);
    application.setFont(font);

    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#102536"));
    palette.setColor(QPalette::WindowText, QColor("#eaf3f6"));
    palette.setColor(QPalette::Base, QColor("#193444"));
    palette.setColor(QPalette::AlternateBase, QColor("#173040"));
    palette.setColor(QPalette::Text, QColor("#eaf3f6"));
    palette.setColor(QPalette::Button, QColor("#193444"));
    palette.setColor(QPalette::ButtonText, QColor("#eaf3f6"));
    palette.setColor(QPalette::Highlight, QColor("#72ddc3"));
    palette.setColor(QPalette::HighlightedText, QColor("#102d35"));
    palette.setColor(QPalette::PlaceholderText, QColor("#8fa9ba"));
    palette.setColor(QPalette::ToolTipBase, QColor("#193444"));
    palette.setColor(QPalette::ToolTipText, QColor("#eaf3f6"));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#718b9d"));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#718b9d"));
    application.setPalette(palette);

    application.setStyleSheet(QString::fromUtf8(R"QSS(
QWidget { font-family: "Noto Sans CJK SC"; font-size: 14px; color: #eaf3f6; }
QMainWindow, QDialog { background: #102536; }
QLabel { background: transparent; border: none; }
QWidget[role="sidebar"] { background: #0b1d2b; border: none; }
QWidget[role="sidebar"] QLabel { color: #c6d8e2; }
QWidget[role="sidebar"] QLabel[role="secondary"] { color: #86a1b5; }
QPushButton[role="navButton"] {
    background: transparent; color: #8ca6b8; border: 1px solid transparent;
    border-radius: 8px; text-align: left; padding: 11px 14px; min-height: 22px;
}
QPushButton[role="navButton"]:hover { background: #173040; color: #FFFFFF; }
QPushButton[role="navButton"]:checked { background: #193849; color: #FFFFFF; font-weight: 600; }
QPushButton[role="navButton"]:focus { border-color: #8de6cf; }
QLabel[role="pageTitle"] { font-size: 26px; font-weight: 700; color: #eaf3f6; }
QLabel[role="pageSubtitle"], QLabel[role="secondary"] { font-size: 12px; color: #97adbc; }
QLabel[role="sectionTitle"] { font-size: 16px; font-weight: 600; color: #eaf3f6; }
QLabel[role="metricValue"] { font-size: 32px; font-weight: 600; color: #eaf3f6; }
QWidget[role="card"] { background: #193444; border: 1px solid #2c4251; border-radius: 14px; }
QWidget[role="toolbar"] { background: #193444; border: 1px solid #2c4251; border-radius: 10px; }
QPushButton {
    background: #193444; border: 1px solid #304b5b; border-radius: 8px;
    padding: 8px 16px; min-height: 20px; color: #c6d8e2;
}
QPushButton:hover { background: #1b3d4d; border-color: #456779; }
QPushButton:pressed { background: #22485a; }
QPushButton:focus { border: 1px solid #72ddc3; }
QPushButton:disabled { background: #193444; border-color: #2c4251; color: #7692a4; }
QPushButton[role="primary"] { background: #72ddc3; border-color: #72ddc3; color: #FFFFFF; font-weight: 600; }
QPushButton[role="primary"]:hover { background: #8ae5d0; border-color: #8ae5d0; }
QPushButton[role="primary"]:pressed { background: #59c7b0; border-color: #59c7b0; }
QPushButton[role="primary"]:focus { border: 2px solid #c6f7e9; padding: 7px 15px; }
QPushButton[role="primary"]:disabled { background: #274c53; border-color: #274c53; color: #FFFFFF; }
QPushButton[role="danger"] { background: #3d3037; border-color: #765052; color: #f1ae98; }
QPushButton[role="danger"]:hover { background: #49333a; border-color: #a37470; }
QPushButton[role="danger"]:disabled { background: #2e3039; border-color: #52414a; color: #927e89; }
QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox, QDateEdit, QDateTimeEdit {
    background: #193444; border: 1px solid #304b5b; border-radius: 8px;
    padding: 8px 12px; min-height: 20px; selection-background-color: #1b3d4d; selection-color: #eaf3f6;
}
QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus,
QDateEdit:focus, QDateTimeEdit:focus { border-color: #72ddc3; }
QLineEdit:disabled, QComboBox:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled {
    background: #193444; color: #7692a4;
}
QComboBox { padding-right: 30px; }
QComboBox::drop-down { border: none; width: 27px; }
QComboBox::down-arrow { image: url(:/admin-ui/chevron-down.xpm); width: 12px; height: 8px; }
QComboBox QAbstractItemView { background: #193444; border: 1px solid #304b5b; selection-background-color: #1b3d4d; selection-color: #eaf3f6; }
QSpinBox, QDoubleSpinBox { padding-right: 24px; }
QSpinBox::up-button, QDoubleSpinBox::up-button { subcontrol-origin: border; subcontrol-position: top right; width: 22px; border: none; }
QSpinBox::down-button, QDoubleSpinBox::down-button { subcontrol-origin: border; subcontrol-position: bottom right; width: 22px; border: none; }
QSpinBox::up-arrow, QDoubleSpinBox::up-arrow { image: url(:/admin-ui/chevron-up.xpm); width: 12px; height: 8px; }
QSpinBox::down-arrow, QDoubleSpinBox::down-arrow { image: url(:/admin-ui/chevron-down.xpm); width: 12px; height: 8px; }
QGroupBox { background: #193444; border: 1px solid #2c4251; border-radius: 14px; margin-top: 15px; padding: 19px 15px 15px; font-weight: 600; }
QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; left: 18px; padding: 0 6px; color: #c6d8e2; background: #193444; }
QTableView, QTreeView, QListView {
    background: #193444; alternate-background-color: #173040; border: 1px solid #2c4251;
    border-radius: 10px; gridline-color: #2c4251; selection-background-color: #1b3d4d; selection-color: #eaf3f6;
}
QTableView::item { border: none; padding: 5px 10px; }
QTableView::item:selected { background: #1b3d4d; color: #eaf3f6; }
QTableView::item:focus { outline: none; }
QHeaderView { background: #193444; border: none; }
QHeaderView::section {
    background: #173040; color: #97adbc; border: none; border-bottom: 1px solid #2c4251;
    padding: 11px 10px; font-size: 12px; font-weight: 500;
}
QTableCornerButton::section { background: #173040; border: none; }
QTabWidget::pane { border: none; background: #102536; }
QTabBar::tab { background: #173040; border: none; color: #97adbc; padding: 10px 18px; }
QTabBar::tab:selected { background: #193444; color: #72ddc3; }
QScrollArea { background: transparent; border: none; }
QScrollBar:vertical { background: transparent; width: 8px; margin: 2px; }
QScrollBar::handle:vertical { background: #3b5b6c; min-height: 26px; border-radius: 3px; }
QScrollBar:horizontal { background: transparent; height: 8px; margin: 2px; }
QScrollBar::handle:horizontal { background: #3b5b6c; min-width: 26px; border-radius: 3px; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
QStatusBar { background: #193444; border-top: 1px solid #2c4251; color: #97adbc; font-size: 12px; }
QStatusBar::item { border: none; }
QStatusBar QLabel { color: #97adbc; font-size: 12px; }
QLabel[role="statusGood"] { background: #1b3d4d; color: #8de6cf; padding: 4px 9px; border-radius: 6px; font-size: 12px; }
QLabel[role="statusWarning"] { background: #3d3830; color: #e9bc87; padding: 4px 9px; border-radius: 6px; font-size: 12px; }
QLabel[role="statusError"] { background: #3d3037; color: #f1ae98; padding: 4px 9px; border-radius: 6px; font-size: 12px; }
QToolTip { background: #eaf3f6; color: #FFFFFF; border: none; padding: 6px 9px; font-size: 12px; }
QDialog#adminLogin { background: #193444; }
QWidget#loginBrandPanel { background: #0b1d2b; }
QWidget#loginFormPanel { background: #193444; }
QLabel#loginBrandName { color: #eaf3f6; font-size: 16px; font-weight: 600; }
QLabel#loginBrandOverline { color: #8de6cf; font-size: 11px; }
QLabel#loginBrandTitle { color: #FFFFFF; font-size: 29px; font-weight: 600; }
QLabel#loginBrandDescription { color: #97adbc; font-size: 13px; }
QLabel#loginBrandFooter { color: #86a1b5; font-size: 11px; }
QLabel#loginEyebrow { color: #72ddc3; font-size: 11px; font-weight: 600; }
QLabel#loginFieldLabel { color: #c6d8e2; font-size: 13px; font-weight: 500; }
QLabel#loginFeedback { color: #f1ae98; font-size: 12px; }
QPushButton#adminLoginButton { min-height: 26px; }
QLineEdit#adminUsername, QLineEdit#adminPassword { min-height: 26px; padding: 9px 13px; }
QWidget[role="sidebar"] { background: #0b1d2b; }
QPushButton[role="navButton"] { font-size:12px; padding:10px 9px; border-radius:6px; }
QPushButton[role="navButton"]:checked { color:#8de6cf; font-weight:500; }
QLabel[role="pageTitle"] { font-size:24px; font-weight:600; }
QWidget[role="card"] { border-radius:10px; }
QPushButton[role="primary"] { color:#102d35; border-radius:6px; }
QPushButton[role="primary"]:disabled { color:#789d9e; }
QToolTip { background:#193444; color:#eaf3f6; }
QStackedWidget { background:#102536; }
QStackedWidget#healthDetailsViews { background:transparent; border:none; }
)QSS"));
}

QIcon icon(const QString &name, const QColor &color)
{
    QIcon result;
    for (const int size : {16, 20, 24, 32}) {
        QPixmap pixmap(size * 2, size * 2);
        pixmap.setDevicePixelRatio(2.0);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.scale(size / 24.0, size / 24.0);
        painter.setPen(QPen(color, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        paintIcon(painter, name.toLower());
        painter.end();
        result.addPixmap(pixmap);
    }
    return result;
}

} // namespace AdminTheme
