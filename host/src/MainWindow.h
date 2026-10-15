#pragma once

#include <QMainWindow>

class DeviceModel;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class TcpWorker;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow(DeviceModel *model, TcpWorker *worker, QWidget *parent = nullptr);

signals:
    void connectRequested(const QString &host, quint16 port);
    void disconnectRequested();

private slots:
    void refresh();
    void onConnectClicked();
    void onConnected();
    void onDisconnected();
    void onSocketError(const QString &message);

private:
    void updateChrome();
    QLabel *addValue(class QFormLayout *form, const QString &name);

    DeviceModel *model_ = nullptr;
    bool connecting_ = false;
    bool connected_ = false;

    QLineEdit *ipEdit_ = nullptr;
    QSpinBox *portSpin_ = nullptr;
    QPushButton *connectButton_ = nullptr;
    QLabel *statusValue_ = nullptr;
    QLabel *lastFrameValue_ = nullptr;
    QLabel *detail_ = nullptr;

    QLabel *timeValue_ = nullptr;
    QLabel *dateValue_ = nullptr;
    QLabel *weekdayValue_ = nullptr;
    QLabel *pageValue_ = nullptr;
    QLabel *indoorTempValue_ = nullptr;
    QLabel *indoorHumValue_ = nullptr;
    QLabel *cityValue_ = nullptr;
    QLabel *outdoorTempValue_ = nullptr;
    QLabel *weatherValue_ = nullptr;
    QLabel *wifiStateValue_ = nullptr;
    QLabel *ssidValue_ = nullptr;
    QLabel *rssiValue_ = nullptr;
    QLabel *channelValue_ = nullptr;
};
