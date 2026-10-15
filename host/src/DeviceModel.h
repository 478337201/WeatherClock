#pragma once

#include "FrameParser.h"

#include <QDateTime>
#include <QObject>
#include <QTimer>

class DeviceModel : public QObject
{
    Q_OBJECT
public:
    static constexpr int kDefaultDataTimeoutMs = 8000;  // 默认超时时间

    enum class LinkStatus
    {
        Disconnected,  // 未连接
        Connected,  // 连接
        DataTimeout  // 数据超时
    };  // 连接状态

    explicit DeviceModel(QObject *parent = nullptr, int dataTimeoutMs = kDefaultDataTimeoutMs);

    LinkStatus linkStatus() const;
    QString statusText() const;
    QString lastFrameTimeText() const;

    QString timeText() const;
    QString dateText() const;
    QString weekdayText() const;
    QString pageText() const;

    QString indoorTemperatureText() const;
    QString indoorHumidityText() const;

    QString outdoorCityText() const;
    QString outdoorTemperatureText() const;
    QString outdoorWeatherText() const;

    QString wifiStateText() const;
    QString wifiSsidText() const;
    QString wifiRssiText() const;
    QString wifiChannelText() const;

public slots:
    void applyFrame(const Frame &frame);
    void onLinkUp();
    void onLinkLost();

signals:
    void changed();

private slots:
    void onDataTimeout();
    void onClockTick();

private:
    void noteActivity();
    void startClock();
    QDateTime shownTime() const;
    bool applyTime(const QByteArray &payload);
    bool applyIndoor(const QByteArray &payload);
    bool applyOutdoor(const QByteArray &payload);
    bool applyWifi(const QByteArray &payload);
    bool applyPage(const QByteArray &payload);
    bool acceptFixed(const QByteArray &payload, int size);
    static QString dash();

    QTimer timer_;  // 数据超时
    QTimer clockTimer_;  // 时间帧间隔里把秒往前走
    int timeoutMs_ = kDefaultDataTimeoutMs;  // 超时时间
    LinkStatus status_ = LinkStatus::Disconnected;  // 连接状态
    bool linkUp_ = false;  // 是否连接

    bool hasLastFrame_ = false;  // 是否有最后一帧
    QDateTime lastFrameAt_;  // 最后一帧时间

    bool hasTime_ = false;  // 是否有时间
    int year_ = 0;  // 年
    int month_ = 0;  // 月
    int day_ = 0;  // 日
    int hour_ = 0;  // 小时
    int minute_ = 0;  // 分钟
    int second_ = 0;  // 秒
    int weekday_ = 0;  // 星期几
    QDateTime timeAt_;  // 收到这帧时间的本机时刻

    bool hasIndoor_ = false;  // 是否有室内
    int indoorTempX10_ = 0;  // 室内温度
    int indoorHumX10_ = 0;  // 室内湿度

    bool hasOutdoor_ = false;  // 是否有室外
    int outdoorCode_ = -1;  // 室外天气代码
    int outdoorTempX10_ = 0;  // 室外温度
    QString outdoorCity_;  // 城市名称

    bool hasWifi_ = false;  // 是否有WIFI
    bool wifiConnected_ = false;  // WIFI是否连接
    int wifiRssi_ = 0;  // WIFI信号强度
    int wifiChannel_ = 0;  // WIFI频道
    QString wifiSsid_;  // WIFI名称

    bool hasPage_ = false;  // 是否有页面
    int page_ = 0;  // 页面编号
};
