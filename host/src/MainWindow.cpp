#include "MainWindow.h"

#include "DeviceModel.h"
#include "TcpWorker.h"

#include <QFont>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {

QString cn(const char *utf8)
{
    return QString::fromUtf8(utf8);
}

} // namespace

MainWindow::MainWindow(DeviceModel *model, TcpWorker *worker, QWidget *parent)
    : QMainWindow(parent)
    , model_(model)
{
    setWindowTitle(cn("天气时钟"));

    auto *central = new QWidget(this);
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    auto *grid = new QGridLayout;
    grid->setColumnStretch(1, 1);
    ipEdit_ = new QLineEdit;
    ipEdit_->setPlaceholderText(cn("屏幕上的 IPv4"));
    ipEdit_->setClearButtonEnabled(true);
    portSpin_ = new QSpinBox;
    portSpin_->setRange(1, 65535);
    portSpin_->setValue(8080);
    portSpin_->setMinimumWidth(88);
    connectButton_ = new QPushButton(cn("连接"));
    connectButton_->setDefault(true);

    grid->addWidget(new QLabel(QStringLiteral("IP")), 0, 0);
    grid->addWidget(ipEdit_, 0, 1);
    grid->addWidget(new QLabel(cn("端口")), 0, 2);
    grid->addWidget(portSpin_, 0, 3);
    grid->addWidget(connectButton_, 0, 4);

    statusValue_ = new QLabel;
    lastFrameValue_ = new QLabel;
    statusValue_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    lastFrameValue_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    grid->addWidget(new QLabel(cn("状态")), 1, 0);
    grid->addWidget(statusValue_, 1, 1);
    grid->addWidget(new QLabel(cn("最后一帧")), 1, 2);
    grid->addWidget(lastFrameValue_, 1, 3, 1, 2);

    detail_ = new QLabel;
    detail_->setWordWrap(true);
    detail_->setMinimumHeight(detail_->fontMetrics().height());
    grid->addWidget(detail_, 2, 0, 1, 5);
    root->addLayout(grid);

    auto *timeBox = new QGroupBox(cn("时间"));
    auto *timeLayout = new QVBoxLayout(timeBox);
    timeValue_ = new QLabel;
    timeValue_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QFont clockFont = timeValue_->font();
    clockFont.setPointSize(clockFont.pointSize() + 10);
    clockFont.setBold(true);
    timeValue_->setFont(clockFont);
    timeLayout->addWidget(timeValue_);
    auto *timeForm = new QFormLayout;
    timeLayout->addLayout(timeForm);
    dateValue_ = addValue(timeForm, cn("日期"));
    weekdayValue_ = addValue(timeForm, cn("星期"));
    pageValue_ = addValue(timeForm, cn("页面"));
    root->addWidget(timeBox);

    auto *indoorBox = new QGroupBox(cn("室内"));
    auto *indoorForm = new QFormLayout(indoorBox);
    indoorTempValue_ = addValue(indoorForm, cn("温度"));
    indoorHumValue_ = addValue(indoorForm, cn("湿度"));
    root->addWidget(indoorBox);

    auto *outdoorBox = new QGroupBox(cn("室外"));
    auto *outdoorForm = new QFormLayout(outdoorBox);
    cityValue_ = addValue(outdoorForm, cn("城市"));
    outdoorTempValue_ = addValue(outdoorForm, cn("温度"));
    weatherValue_ = addValue(outdoorForm, cn("天气"));
    root->addWidget(outdoorBox);

    auto *wifiBox = new QGroupBox(QStringLiteral("Wi-Fi"));
    auto *wifiForm = new QFormLayout(wifiBox);
    wifiStateValue_ = addValue(wifiForm, cn("状态"));
    ssidValue_ = addValue(wifiForm, QStringLiteral("SSID"));
    rssiValue_ = addValue(wifiForm, QStringLiteral("RSSI"));
    channelValue_ = addValue(wifiForm, cn("信道"));
    root->addWidget(wifiBox);

    setCentralWidget(central);
    setMinimumWidth(520);
    adjustSize();

    QSettings settings;
    ipEdit_->setText(settings.value(QStringLiteral("connection/lastIp")).toString());

    connect(connectButton_, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    connect(model_, &DeviceModel::changed, this, &MainWindow::refresh);
    connect(this, &MainWindow::connectRequested, worker, &TcpWorker::connectToHost, Qt::QueuedConnection);
    connect(this, &MainWindow::disconnectRequested, worker, &TcpWorker::disconnectFromHost, Qt::QueuedConnection);
    connect(worker, &TcpWorker::connected, this, &MainWindow::onConnected, Qt::QueuedConnection);
    connect(worker, &TcpWorker::disconnected, this, &MainWindow::onDisconnected, Qt::QueuedConnection);
    connect(worker, &TcpWorker::errorOccurred, this, &MainWindow::onSocketError, Qt::QueuedConnection);  // 出现错误

    refresh();
    ipEdit_->setFocus();
}

QLabel *MainWindow::addValue(QFormLayout *form, const QString &name)
{
    auto *value = new QLabel(QStringLiteral("--"));
    value->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(name, value);
    return value;
}

void MainWindow::refresh()
{
    statusValue_->setText(model_->statusText());
    switch (model_->linkStatus()) {
    case DeviceModel::LinkStatus::Connected:
        statusValue_->setStyleSheet(QStringLiteral("color: #146c43; font-weight: 600;"));
        break;
    case DeviceModel::LinkStatus::DataTimeout:
        statusValue_->setStyleSheet(QStringLiteral("color: #9a6700; font-weight: 600;"));
        break;
    case DeviceModel::LinkStatus::Disconnected:
        statusValue_->setStyleSheet(QStringLiteral("color: #666666;"));
        break;
    }

    lastFrameValue_->setText(model_->lastFrameTimeText());
    timeValue_->setText(model_->timeText());
    dateValue_->setText(model_->dateText());
    weekdayValue_->setText(model_->weekdayText());
    pageValue_->setText(model_->pageText());
    indoorTempValue_->setText(model_->indoorTemperatureText());
    indoorHumValue_->setText(model_->indoorHumidityText());
    cityValue_->setText(model_->outdoorCityText());
    outdoorTempValue_->setText(model_->outdoorTemperatureText());
    weatherValue_->setText(model_->outdoorWeatherText());
    wifiStateValue_->setText(model_->wifiStateText());
    ssidValue_->setText(model_->wifiSsidText());
    rssiValue_->setText(model_->wifiRssiText());
    channelValue_->setText(model_->wifiChannelText());
}

void MainWindow::onConnectClicked()
{
    if (connecting_ || connected_) {
        detail_->clear();
        emit disconnectRequested();
        return;
    }

    const QString host = ipEdit_->text().trimmed();
    QHostAddress address;
    if (!address.setAddress(host) || address.protocol() != QAbstractSocket::IPv4Protocol) {
        detail_->setText(cn("请填写屏幕上的 IPv4"));
        return;
    }

    connecting_ = true;
    detail_->setText(cn("正在连接…"));
    updateChrome();
    emit connectRequested(host, static_cast<quint16>(portSpin_->value()));
}

void MainWindow::onConnected()
{
    connecting_ = false;
    connected_ = true;
    detail_->clear();
    QSettings settings;
    settings.setValue(QStringLiteral("connection/lastIp"), ipEdit_->text().trimmed());
    updateChrome();
}

void MainWindow::onDisconnected()
{
    connecting_ = false;
    connected_ = false;
    updateChrome();
}

void MainWindow::onSocketError(const QString &message)
{
    connecting_ = false;
    connected_ = false;
    detail_->setText(message);
    updateChrome();
}

void MainWindow::updateChrome()
{
    const bool busy = connecting_ || connected_;
    ipEdit_->setEnabled(!busy);
    portSpin_->setEnabled(!busy);
    connectButton_->setText(busy ? cn("断开") : cn("连接"));
}
