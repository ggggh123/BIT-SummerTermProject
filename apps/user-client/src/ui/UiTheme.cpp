#include "ui/UiTheme.h"

#include <QApplication>
#include <QFont>

#include <algorithm>

namespace UiTheme {

void apply(QApplication &application)
{
    QFont font(QStringLiteral("Noto Sans CJK SC"));
    font.setPixelSize(14);
    application.setFont(font);
    application.setStyleSheet(QStringLiteral(R"QSS(
        * {
            font-family: "Noto Sans CJK SC", "Noto Sans SC", sans-serif;
        }
        QMainWindow, QWidget {
            background: #102536;
            color: #eaf3f6;
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
            color: #eaf3f6;
            font-size: 26px;
            font-weight: 700;
        }
        QLabel[role="sectionTitle"] {
            color: #eaf3f6;
            font-size: 20px;
            font-weight: 700;
        }
        QLabel[role="secondary"] {
            color: #97adbc;
            font-size: 14px;
        }
        QLabel[role="danger"] {
            color: #f1ae98;
            font-size: 14px;
        }
        QLabel[role="price"] {
            color: #72ddc3;
            font-size: 26px;
            font-weight: 700;
        }
        QLabel[role="chargeMetric"] {
            color: #72ddc3;
            font-size: 44px;
            font-weight: 700;
        }
        QLabel[role="chargeSubMetric"] {
            color: #eaf3f6;
            font-size: 22px;
            font-weight: 700;
        }
        QLabel[role="chargeNotice"] {
            color: #a2f3df;
            background: #193d42;
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
            color: #97adbc;
            font-size: 16px;
            font-weight: 400;
        }
        QLabel[role="available"] {
            color: #72ddc3;
            font-size: 14px;
        }
        QLabel[role="usageMetric"] {
            color: #a2f3df;
            font-size: 26px;
            font-weight: 600;
        }
        QLabel[status="ok"] {
            color: #72ddc3;
        }
        QLabel[status="error"] {
            color: #f1ae98;
        }
        QWidget[role="card"], QFrame[role="card"] {
            background: #193444;
            border: 1px solid #2c4251;
            border-radius: 12px;
        }
        QFrame[role="loginHero"] {
            background: #102536;
            border: none;
            border-radius: 0px;
        }
        QFrame[role="chargerRow"] {
            background: transparent;
            border: none;
            border-bottom: 1px solid #2c4251;
            border-radius: 0px;
        }
        QFrame[role="chargerRow"][last="true"] {
            border-bottom: none;
        }
        QFrame[role="loginHero"] QLabel {
            background: transparent;
        }
        QLabel#loginBrand {
            color: #72ddc3;
        }
        QLineEdit, QComboBox {
            min-height: 24px;
            padding: 11px 12px;
            color: #eaf3f6;
            background: #193444;
            border: 1px solid #2c4251;
            border-radius: 10px;
            selection-background-color: #72ddc3;
        }
        QLineEdit:focus, QComboBox:focus {
            border: 2px solid #72ddc3;
            padding: 10px 11px;
        }
        QLineEdit:disabled, QComboBox:disabled {
            color: #718b9d;
            background: #173040;
        }
        QLineEdit#profileMobile {
            min-height: 22px;
            padding: 0px;
            color: #97adbc;
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
        QListWidget#historyList::item, QListWidget#historyList::item:selected {
            background: transparent;
            border: none;
        }
        QFrame#historyOrderCard[orderSelected="true"] {
            background: #214655;
            border: 1px solid #72ddc3;
        }
        QComboBox#addressBox::drop-down {
            subcontrol-origin: padding;
            subcontrol-position: top right;
            width: 30px;
            border: none;
        }
        QComboBox#addressBox::down-arrow {
            image: url(:/ui/expand-more.svg);
            width: 20px;
            height: 20px;
        }
        QPushButton {
            min-height: 40px;
            padding: 3px 14px;
            color: #eaf3f6;
            background: #193444;
            border: 1px solid #2c4251;
            border-radius: 10px;
        }
        QPushButton:hover {
            border-color: #72ddc3;
        }
        QPushButton[role="routeMode"]:checked {
            color: #102d35;
            background: #72ddc3;
            border-color: #72ddc3;
            font-weight: 700;
        }
        QMenu {
            color: #eaf3f6;
            background: #193444;
            border: 1px solid #456574;
            padding: 6px;
        }
        QMenu::item { padding: 12px 18px; }
        QMenu::item:selected { background: #285565; color: #a2f3df; }
        QMenu::item:disabled { color: #718b9d; }
        QPushButton#accountMenuButton { padding-right: 22px; }
        QPushButton#accountMenuButton::menu-indicator {
            image: url(:/ui/expand-more.svg);
            subcontrol-origin: padding;
            subcontrol-position: center right;
            width: 16px;
            height: 16px;
            right: 4px;
        }
        QPushButton:focus {
            border: 2px solid #a2f3df;
            padding: 2px 13px;
        }
        QPushButton:disabled {
            color: #718b9d;
            background: #193444;
            border-color: #2c4251;
        }
        QPushButton[role="primary"] {
            min-height: 48px;
            color: #102d35;
            background: #72ddc3;
            border: none;
            border-radius: 10px;
            font-weight: 700;
        }
        QPushButton[role="primary"]:hover {
            background: #8ae5d0;
        }
        QPushButton[role="primary"]:focus {
            border: 2px solid #c6f7e9;
            padding: 1px 12px;
        }
        QPushButton[role="primary"]:disabled {
            color: #789d9e;
            background: #274c53;
        }
        QPushButton#nearbySearchButton {
            min-height: 48px;
            padding: 0px;
        }
        QPushButton[role="danger"] {
            min-height: 44px;
            color: #f1ae98;
            background: #193444;
            border-color: #f1ae98;
        }
        QPushButton[role="outline"] {
            min-height: 42px;
            padding: 3px 8px;
            color: #72ddc3;
            border-color: #72ddc3;
        }
        QPushButton[role="outline"]:disabled {
            color: #718b9d;
            background: #102536;
            border-color: #2c4251;
        }
        QPushButton[role="textAction"] {
            color: #72ddc3;
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
            border-top: 1px solid #2c4251;
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
            border: 2px solid #a2f3df;
            padding: 0px;
        }
        QWidget#authenticatedNavigation {
            background: #193444;
            border-top: 1px solid #2c4251;
        }
        QPushButton[role="tab"] {
            min-height: 52px;
            padding: 5px 4px;
            color: #97adbc;
            background: transparent;
            border: none;
            border-radius: 8px;
            font-size: 13px;
        }
        QPushButton[role="tab"][selected="true"] {
            color: #72ddc3;
            background: transparent;
            font-weight: 700;
        }
        QScrollBar:vertical {
            width: 8px;
            margin: 2px;
            background: transparent;
        }
        QScrollBar::handle:vertical {
            min-height: 28px;
            background: #3b5b6c;
            border-radius: 4px;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0px;
        }
        QWidget#mobileBrand { background: #102536; border-bottom: 1px solid #2c4251; }
        QLabel#mobileBrandName { font-size: 16px; font-weight: 600; }
        QLabel#mobileBrandMode { color: #97adbc; font-size: 11px; }
        QWidget#authenticatedNavigation { background: #102536; }
        QPushButton[role="tab"] { font-size: 11px; border-radius: 0; min-height: 44px; padding: 0; }
        QPushButton[role="tab"][selected="true"] { background: transparent; font-weight: 400; }
        QLabel#chargeTitle { font-size: 25px; font-weight: 600; }
        QLabel#chargeStatus { font-size: 12px; font-weight: 400; color: #7be2c8; background: #173d42; border-radius: 13px; padding: 5px 9px; }
        QLabel#chargeOrderIdentity { font-size: 13px; color: #97adbc; }
        QLabel#chargeMeter { font-size: 42px; font-weight: 600; color: #f6fafc; }
        QLabel#chargeMetricCaption, QLabel#chargeMetricUnit { font-size: 12px; color: #97adbc; }
        QLabel#chargePower { font-size: 26px; font-weight: 600; color: #8ae5d0; }
        QLabel#chargeDuration, QLabel#chargeSecondaryMetric { font-size: 20px; font-weight: 500; }
        QLabel[role="pulseCaption"] { font-size: 12px; color: #97adbc; }
        QLabel[role="pulseFine"] { font-size: 10px; color: #89a8ba; }
        QFrame#chargeMeterRow { border: 0; border-bottom: 1px solid #2f4757; background: transparent; }
        QFrame#chargeSignalRow { border: 0; border-bottom: 1px solid #2c4454; background: transparent; }
        QPushButton#chargeStopButton { min-height: 48px; background: #72ddc3; color: #102d35; border: 0; border-radius: 6px; text-align: left; padding: 3px 17px; font-weight: 500; }
        QPushButton#chargeStopButton:hover { background: #8ae5d0; }
        QPushButton#chargeStopButton:disabled { background: #274c53; color: #789d9e; }
        QPushButton#chargeStopButton:focus { border: 2px solid #c6f7e9; padding: 1px 15px; }
        QPushButton#chargeDetailsButton, QPushButton#chargeLiveButton { min-height: 16px; font-size: 11px; background: transparent; border: 0; padding: 0; color: #8ae5d0; }
        QPushButton#chargeDetailsButton:focus, QPushButton#chargeLiveButton:focus { border: 1px solid #8ae5d0; }
        QLabel#chargeNotice { padding: 6px; font-size: 12px; }
        QLabel#chargeSummary { font-size: 11px; }
        QLabel#reservationSuccessTitle { font-size: 27px; font-weight: 600; color: #a2f3df; }
        QLabel#reservationSuccessHint { font-size: 14px; color: #97adbc; }
        QLabel#profileAccountState { font-size: 12px; padding: 5px 9px; border-radius: 6px; }
        QLabel#profileAccountState[frozen="false"] { background: #173d42; color: #8ae5d0; }
        QLabel#profileAccountState[frozen="true"] { background: #3d3037; color: #f1ae98; }
        QFrame#profileFrozenNotice { background: #302d35; border: 1px solid #73554d; border-radius: 10px; }
        QLabel#profileFrozenTitle { color: #f1ae98; font-size: 17px; font-weight: 600; }
        QLabel#profileFrozenDescription { color: #dec5bc; font-size: 13px; }
        QPushButton#profileStateRefreshButton { min-height: 22px; padding: 4px 9px; font-size: 12px; }
        QToolTip { background: #193444; color: #eaf3f6; border: 1px solid #345060; padding: 7px; }
    )QSS"));
}

QSize initialWindowSize(const QRect &availableGeometry)
{
    const int height = std::max(1, std::min(844, availableGeometry.height() - 64));
    return {std::min(390, availableGeometry.width()), height};
}

} // namespace UiTheme
