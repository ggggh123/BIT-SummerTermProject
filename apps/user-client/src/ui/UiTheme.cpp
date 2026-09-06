#include "ui/UiTheme.h"

#include <QApplication>
#include <QFont>

#include <algorithm>

namespace UiTheme {

void apply(QApplication &application)
{
    QFont font(QStringLiteral("Noto Sans CJK SC"));
    font.setPixelSize(16);
    application.setFont(font);
    application.setStyleSheet(QStringLiteral(R"QSS(
        * {
            font-family: "Noto Sans CJK SC", "Noto Sans SC", sans-serif;
        }
        QMainWindow, QWidget {
            background: #F5F7F9;
            color: #172B3A;
            font-size: 16px;
        }
        QScrollArea, QScrollArea > QWidget > QWidget,
        QStackedWidget, QStackedWidget > QWidget {
            border: none;
            background: transparent;
        }
        QLabel {
            background: transparent;
        }
        QLabel[role="pageTitle"] {
            color: #172B3A;
            font-size: 26px;
            font-weight: 700;
        }
        QLabel[role="sectionTitle"] {
            color: #172B3A;
            font-size: 20px;
            font-weight: 700;
        }
        QLabel[role="secondary"] {
            color: #61717B;
            font-size: 14px;
        }
        QLabel[role="danger"] {
            color: #BE4B42;
            font-size: 14px;
        }
        QLabel[role="price"] {
            color: #00856A;
            font-size: 26px;
            font-weight: 700;
        }
        QLabel[role="chargeMetric"] {
            color: #00856A;
            font-size: 44px;
            font-weight: 700;
        }
        QLabel[role="chargeSubMetric"] {
            color: #172B3A;
            font-size: 22px;
            font-weight: 700;
        }
        QLabel[role="chargeNotice"] {
            color: #006F59;
            background: #EAF6F2;
            padding: 10px;
            border-radius: 8px;
            font-size: 14px;
        }
        QWidget#chargeMetrics {
            background: transparent;
        }
        QWidget[role="priceGroup"] {
            background: transparent;
        }
        QLabel[role="priceUnit"] {
            color: #61717B;
            font-size: 16px;
            font-weight: 400;
        }
        QLabel[role="available"] {
            color: #00856A;
            font-size: 14px;
        }
        QLabel[status="ok"] {
            color: #00856A;
        }
        QLabel[status="error"] {
            color: #BE4B42;
        }
        QWidget[role="card"], QFrame[role="card"] {
            background: #FFFFFF;
            border: 1px solid #DFE5E9;
            border-radius: 12px;
        }
        QFrame[role="loginHero"] {
            background: #F0F9F7;
            border: none;
            border-radius: 0px;
        }
        QFrame[role="chargerRow"] {
            background: transparent;
            border: none;
            border-bottom: 1px solid #DFE5E9;
            border-radius: 0px;
        }
        QFrame[role="chargerRow"][last="true"] {
            border-bottom: none;
        }
        QFrame[role="loginHero"] QLabel {
            background: transparent;
        }
        QLabel#loginBrand {
            color: #00856A;
        }
        QLineEdit, QComboBox {
            min-height: 24px;
            padding: 11px 12px;
            color: #172B3A;
            background: #FFFFFF;
            border: 1px solid #DFE5E9;
            border-radius: 10px;
            selection-background-color: #00856A;
        }
        QLineEdit:focus, QComboBox:focus {
            border: 2px solid #00856A;
            padding: 10px 11px;
        }
        QLineEdit:disabled, QComboBox:disabled {
            color: #89979F;
            background: #EEF2F4;
        }
        QLineEdit#profileMobile {
            min-height: 22px;
            padding: 0px;
            color: #61717B;
            background: transparent;
            border: none;
            font-size: 14px;
        }
        QListWidget#historyList {
            background: transparent;
            border: 2px solid transparent;
            border-radius: 8px;
            outline: none;
        }
        QListWidget#historyList:focus {
            border-color: #006F59;
        }
        QListWidget#historyList::item {
            background: transparent;
            border: none;
        }
        QComboBox#addressBox::drop-down, QComboBox#routeModeBox::drop-down {
            subcontrol-origin: padding;
            subcontrol-position: top right;
            width: 30px;
            border: none;
        }
        QComboBox#addressBox::down-arrow, QComboBox#routeModeBox::down-arrow {
            image: url(:/ui/expand-more.svg);
            width: 20px;
            height: 20px;
        }
        QPushButton {
            min-height: 40px;
            padding: 3px 14px;
            color: #172B3A;
            background: #FFFFFF;
            border: 1px solid #DFE5E9;
            border-radius: 10px;
        }
        QPushButton:hover {
            border-color: #00856A;
        }
        QPushButton:focus {
            border: 2px solid #006F59;
            padding: 2px 13px;
        }
        QPushButton:disabled {
            color: #89979F;
            background: #E9EEF0;
            border-color: #DFE5E9;
        }
        QPushButton[role="primary"] {
            min-height: 48px;
            color: #FFFFFF;
            background: #00856A;
            border: none;
            border-radius: 10px;
            font-weight: 700;
        }
        QPushButton[role="primary"]:hover {
            background: #00755E;
        }
        QPushButton[role="primary"]:focus {
            border: 2px solid #005E4B;
            padding: 1px 12px;
        }
        QPushButton[role="primary"]:disabled {
            color: #E9F5F2;
            background: #72B7A8;
        }
        QPushButton#nearbySearchButton {
            min-height: 48px;
            padding: 0px;
        }
        QPushButton[role="danger"] {
            min-height: 44px;
            color: #BE4B42;
            background: #FFFFFF;
            border-color: #BE4B42;
        }
        QPushButton[role="outline"] {
            min-height: 42px;
            padding: 3px 8px;
            color: #00856A;
            border-color: #00856A;
        }
        QPushButton[role="outline"]:disabled {
            color: #89979F;
            background: #F5F7F9;
            border-color: #DFE5E9;
        }
        QPushButton[role="textAction"] {
            color: #00856A;
            font-size: 16px;
            font-weight: 700;
            padding: 0px;
            border: none;
            background: transparent;
        }
        QPushButton[role="cardAction"] {
            min-height: 44px;
            padding: 0px;
            text-align: left;
            border: none;
            border-top: 1px solid #DFE5E9;
            border-radius: 0px;
            background: transparent;
        }
        QPushButton[role="back"] {
            min-height: 44px;
            padding: 0px;
            border: none;
            background: transparent;
        }
        QPushButton[role="back"]:focus {
            border: 2px solid #006F59;
            padding: 0px;
        }
        QWidget#authenticatedNavigation {
            background: #FFFFFF;
            border-top: 1px solid #DFE5E9;
        }
        QPushButton[role="tab"] {
            min-height: 52px;
            padding: 5px 4px;
            color: #61717B;
            background: transparent;
            border: none;
            border-radius: 8px;
            font-size: 13px;
        }
        QPushButton[role="tab"][selected="true"] {
            color: #00856A;
            background: #E6F7F1;
            font-weight: 700;
        }
        QScrollBar:vertical {
            width: 8px;
            margin: 2px;
            background: transparent;
        }
        QScrollBar::handle:vertical {
            min-height: 28px;
            background: #B9C7CD;
            border-radius: 4px;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0px;
        }
    )QSS"));
}

QSize initialWindowSize(const QRect &availableGeometry)
{
    const int height = std::max(1, std::min(844, availableGeometry.height() - 64));
    return {std::min(390, availableGeometry.width()), height};
}

} // namespace UiTheme
