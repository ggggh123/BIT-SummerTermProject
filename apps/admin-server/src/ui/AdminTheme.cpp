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
    palette.setColor(QPalette::Window, QColor("#F5F6F2"));
    palette.setColor(QPalette::WindowText, QColor("#18352D"));
    palette.setColor(QPalette::Base, QColor("#FFFFFF"));
    palette.setColor(QPalette::AlternateBase, QColor("#FAFBF8"));
    palette.setColor(QPalette::Text, QColor("#18352D"));
    palette.setColor(QPalette::Button, QColor("#FFFFFF"));
    palette.setColor(QPalette::ButtonText, QColor("#18352D"));
    palette.setColor(QPalette::Highlight, QColor("#00856A"));
    palette.setColor(QPalette::HighlightedText, QColor("#FFFFFF"));
    palette.setColor(QPalette::PlaceholderText, QColor("#87958D"));
    palette.setColor(QPalette::ToolTipBase, QColor("#18352D"));
    palette.setColor(QPalette::ToolTipText, QColor("#FFFFFF"));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#94A198"));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#94A198"));
    application.setPalette(palette);

    application.setStyleSheet(QString::fromUtf8(R"QSS(
QWidget { font-family: "Noto Sans CJK SC"; font-size: 14px; color: #18352D; }
QMainWindow, QDialog { background: #F5F6F2; }
QLabel { background: transparent; border: none; }
QWidget[role="sidebar"] { background: #102D29; border: none; }
QWidget[role="sidebar"] QLabel { color: #D3E3D9; }
QWidget[role="sidebar"] QLabel[role="secondary"] { color: #90ABA0; }
QPushButton[role="navButton"] {
    background: transparent; color: #B9CEC3; border: 1px solid transparent;
    border-radius: 8px; text-align: left; padding: 11px 14px; min-height: 22px;
}
QPushButton[role="navButton"]:hover { background: #193D34; color: #FFFFFF; }
QPushButton[role="navButton"]:checked { background: #214B40; color: #FFFFFF; font-weight: 600; }
QPushButton[role="navButton"]:focus { border-color: #80B9A2; }
QLabel[role="pageTitle"] { font-size: 26px; font-weight: 700; color: #18352D; }
QLabel[role="pageSubtitle"], QLabel[role="secondary"] { font-size: 12px; color: #718078; }
QLabel[role="sectionTitle"] { font-size: 16px; font-weight: 600; color: #18352D; }
QLabel[role="metricValue"] { font-size: 32px; font-weight: 600; color: #18352D; }
QWidget[role="card"] { background: #FFFFFF; border: 1px solid #E2E8E1; border-radius: 14px; }
QWidget[role="toolbar"] { background: #FFFFFF; border: 1px solid #E2E8E1; border-radius: 10px; }
QPushButton {
    background: #FFFFFF; border: 1px solid #DCE4DB; border-radius: 8px;
    padding: 8px 16px; min-height: 20px; color: #355045;
}
QPushButton:hover { background: #F1F6F0; border-color: #B8CDBE; }
QPushButton:pressed { background: #E6EFE5; }
QPushButton:focus { border: 1px solid #00856A; }
QPushButton:disabled { background: #F2F4EF; border-color: #E2E8E1; color: #95A197; }
QPushButton[role="primary"] { background: #00856A; border-color: #00856A; color: #FFFFFF; font-weight: 600; }
QPushButton[role="primary"]:hover { background: #00775E; border-color: #00775E; }
QPushButton[role="primary"]:pressed { background: #00664F; border-color: #00664F; }
QPushButton[role="primary"]:focus { border: 2px solid #6ABAA0; padding: 7px 15px; }
QPushButton[role="primary"]:disabled { background: #94C2B0; border-color: #94C2B0; color: #FFFFFF; }
QPushButton[role="danger"] { background: #FFF5F3; border-color: #EACEC8; color: #B94B43; }
QPushButton[role="danger"]:hover { background: #FDE8E3; border-color: #D7A99F; }
QPushButton[role="danger"]:disabled { background: #F5F1EF; border-color: #E4DCD8; color: #B5A6A1; }
QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox, QDateEdit, QDateTimeEdit {
    background: #FFFFFF; border: 1px solid #DDE5DC; border-radius: 8px;
    padding: 8px 12px; min-height: 20px; selection-background-color: #D4ECE0; selection-color: #18352D;
}
QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus,
QDateEdit:focus, QDateTimeEdit:focus { border-color: #00856A; }
QLineEdit:disabled, QComboBox:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled {
    background: #F2F5F0; color: #95A197;
}
QComboBox { padding-right: 30px; }
QComboBox::drop-down { border: none; width: 27px; }
QComboBox::down-arrow { image: url(:/admin-ui/chevron-down.xpm); width: 12px; height: 8px; }
QComboBox QAbstractItemView { background: #FFFFFF; border: 1px solid #DDE5DC; selection-background-color: #E6F4EC; selection-color: #18352D; }
QSpinBox, QDoubleSpinBox { padding-right: 24px; }
QSpinBox::up-button, QDoubleSpinBox::up-button { subcontrol-origin: border; subcontrol-position: top right; width: 22px; border: none; }
QSpinBox::down-button, QDoubleSpinBox::down-button { subcontrol-origin: border; subcontrol-position: bottom right; width: 22px; border: none; }
QSpinBox::up-arrow, QDoubleSpinBox::up-arrow { image: url(:/admin-ui/chevron-up.xpm); width: 12px; height: 8px; }
QSpinBox::down-arrow, QDoubleSpinBox::down-arrow { image: url(:/admin-ui/chevron-down.xpm); width: 12px; height: 8px; }
QGroupBox { background: #FFFFFF; border: 1px solid #E2E8E1; border-radius: 14px; margin-top: 15px; padding: 19px 15px 15px; font-weight: 600; }
QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; left: 18px; padding: 0 6px; color: #294C3F; background: #FFFFFF; }
QTableView, QTreeView, QListView {
    background: #FFFFFF; alternate-background-color: #FAFBF8; border: 1px solid #E2E8E1;
    border-radius: 10px; gridline-color: #EDF1E9; selection-background-color: #E6F4EC; selection-color: #18352D;
}
QTableView::item { border: none; padding: 5px 10px; }
QTableView::item:selected { background: #E6F4EC; color: #18352D; }
QTableView::item:focus { outline: none; }
QHeaderView { background: #FFFFFF; border: none; }
QHeaderView::section {
    background: #F8FAF6; color: #718078; border: none; border-bottom: 1px solid #E2E8E1;
    padding: 11px 10px; font-size: 12px; font-weight: 500;
}
QTableCornerButton::section { background: #F8FAF6; border: none; }
QTabWidget::pane { border: none; background: #F5F6F2; }
QTabBar::tab { background: #EDF2EA; border: none; color: #718078; padding: 10px 18px; }
QTabBar::tab:selected { background: #FFFFFF; color: #00856A; }
QScrollArea { background: transparent; border: none; }
QScrollBar:vertical { background: transparent; width: 8px; margin: 2px; }
QScrollBar::handle:vertical { background: #CCD6CC; min-height: 26px; border-radius: 3px; }
QScrollBar:horizontal { background: transparent; height: 8px; margin: 2px; }
QScrollBar::handle:horizontal { background: #CCD6CC; min-width: 26px; border-radius: 3px; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
QStatusBar { background: #FFFFFF; border-top: 1px solid #E2E8E1; color: #718078; font-size: 12px; }
QStatusBar::item { border: none; }
QStatusBar QLabel { color: #718078; font-size: 12px; }
QLabel[role="statusGood"] { background: #E6F4EC; color: #24734D; padding: 4px 9px; border-radius: 6px; font-size: 12px; }
QLabel[role="statusWarning"] { background: #FFF3DF; color: #A86619; padding: 4px 9px; border-radius: 6px; font-size: 12px; }
QLabel[role="statusError"] { background: #FCEBE7; color: #B94B43; padding: 4px 9px; border-radius: 6px; font-size: 12px; }
QToolTip { background: #18352D; color: #FFFFFF; border: none; padding: 6px 9px; font-size: 12px; }
QDialog#adminLogin { background: #FFFFFF; }
QWidget#loginBrandPanel { background: #102D29; }
QWidget#loginFormPanel { background: #FFFFFF; }
QLabel#loginBrandName { color: #E9F2EB; font-size: 16px; font-weight: 600; }
QLabel#loginBrandOverline { color: #79B69B; font-size: 11px; }
QLabel#loginBrandTitle { color: #FFFFFF; font-size: 29px; font-weight: 600; }
QLabel#loginBrandDescription { color: #A7C3B4; font-size: 13px; }
QLabel#loginBrandFooter { color: #7F9F8F; font-size: 11px; }
QLabel#loginEyebrow { color: #00856A; font-size: 11px; font-weight: 600; }
QLabel#loginFieldLabel { color: #355045; font-size: 13px; font-weight: 500; }
QLabel#loginFeedback { color: #B94B43; font-size: 12px; }
QPushButton#adminLoginButton { min-height: 26px; }
QLineEdit#adminUsername, QLineEdit#adminPassword { min-height: 26px; padding: 9px 13px; }
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
