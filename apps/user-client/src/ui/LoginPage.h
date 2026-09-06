#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;

class LoginPage final : public QWidget
{
    Q_OBJECT

public:
    explicit LoginPage(QWidget *parent = nullptr);

    void setPending(bool pending);
    void setConnectionAvailable(bool available);
    void setError(const QString &message);

signals:
    void loginRequested(QString mobile);

private:
    QLineEdit *phoneEdit_;
    QPushButton *loginButton_;
    QLabel *connectionBanner_;
    QLabel *errorMessage_;
    bool pending_ = false;
};
