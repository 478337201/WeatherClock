#include "DeviceModel.h"
#include "WeatherText.h"

#include <QtGlobal>

namespace
{
    class PayloadCursor
    {
    public:
        explicit PayloadCursor(const QByteArray &payload)
            : payload_(payload)
        {
        }

        bool ok() const
        {
            return ok_ && index_ == payload_.size();
        }

        quint8 u8()
        {
            if (!require(1))
                return 0;
            return static_cast<quint8>(payload_.at(index_++));
        }

        quint16 u16()
        {
            if (!require(2))
                return 0;
            const quint16 value = static_cast<quint8>(payload_.at(index_))
                | (static_cast<quint16>(static_cast<quint8>(payload_.at(index_ + 1))) << 8);
            index_ += 2;
            return value;
        }

        quint32 u32()
        {
            if (!require(4))
                return 0;
            const quint32 value = static_cast<quint8>(payload_.at(index_))
                | (static_cast<quint32>(static_cast<quint8>(payload_.at(index_ + 1))) << 8)
                | (static_cast<quint32>(static_cast<quint8>(payload_.at(index_ + 2))) << 16)
                | (static_cast<quint32>(static_cast<quint8>(payload_.at(index_ + 3))) << 24);
            index_ += 4;
            return value;
        }

        qint16 i16()
        {
            return static_cast<qint16>(u16());
        }

        QString str()
        {
            const int length = u8();
            if (!ok_ || !require(length))
                return {};
            const QString text = QString::fromUtf8(payload_.constData() + index_, length);
            index_ += length;
            return text;
        }

    private:
        bool require(int length)
        {
            if (!ok_ || length < 0 || index_ + length > payload_.size()) {
                ok_ = false;
                return false;
            }
            return true;
        }

        const QByteArray &payload_;
        int index_ = 0;
        bool ok_ = true;
    };

    QString formatTenths(int value)
    {
        const bool negative = value < 0;
        const int absValue = negative ? -value : value;
        return QString::asprintf("%s%d.%d", negative ? "-" : "", absValue / 10, absValue % 10);
    }
}  // namespace


DeviceModel::DeviceModel(QObject *parent, int dataTimeoutMs) : QObject(parent)
    , timeoutMs_(dataTimeoutMs > 0 ? dataTimeoutMs : kDefaultDataTimeoutMs)
{
    timer_.setSingleShot(true);  // 单次触发
    connect(&timer_, &QTimer::timeout, this, &DeviceModel::onDataTimeout);  // 超时触发
    clockTimer_.setInterval(1000);
    connect(&clockTimer_, &QTimer::timeout, this, &DeviceModel::onClockTick);
}

DeviceModel::LinkStatus DeviceModel::linkStatus() const
{
    return status_;
}

QString DeviceModel::statusText() const
{
    switch (status_) {
    case LinkStatus::Connected:
        return QString::fromUtf8("已连接");
    case LinkStatus::DataTimeout:
        return QString::fromUtf8("数据超时");
    case LinkStatus::Disconnected:
        break;
    }
    return QString::fromUtf8("未连接");
}

QString DeviceModel::lastFrameTimeText() const
{
    if (!hasLastFrame_)
        return dash();
    return lastFrameAt_.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

QString DeviceModel::timeText() const
{
    const QDateTime shown = shownTime();
    if (!shown.isValid())
        return dash();
    return shown.time().toString(QStringLiteral("HH:mm:ss"));
}

QString DeviceModel::dateText() const
{
    const QDateTime shown = shownTime();
    if (!shown.isValid())
        return dash();
    return shown.date().toString(QStringLiteral("yyyy-MM-dd"));
}

QString DeviceModel::weekdayText() const
{
    if (!hasTime_)
        return dash();

    int weekday = weekday_;
    const QDate base(year_, month_, day_);
    const QDateTime shown = shownTime();
    if (base.isValid() && shown.isValid() && weekday_ >= 1 && weekday_ <= 7) {
        const int dayDelta = base.daysTo(shown.date());
        if (dayDelta != 0)
            weekday = ((weekday_ - 1 + dayDelta) % 7 + 7) % 7 + 1;
    }
    switch (weekday) {
    case 1: return QString::fromUtf8("星期一");
    case 2: return QString::fromUtf8("星期二");
    case 3: return QString::fromUtf8("星期三");
    case 4: return QString::fromUtf8("星期四");
    case 5: return QString::fromUtf8("星期五");
    case 6: return QString::fromUtf8("星期六");
    case 7: return QString::fromUtf8("星期日");
    default: return dash();
    }
}

QString DeviceModel::pageText() const
{
    if (!hasPage_)
        return dash();
    // 固件 tcp.h：0 主页，1 错误页。欢迎页和 Wi-Fi 页发生在监听之前，不会上报。
    if (page_ == 0)
        return QString::fromUtf8("主页");
    if (page_ == 1)
        return QString::fromUtf8("错误页");
    return QString::fromUtf8("页面 %1").arg(page_);
}

QString DeviceModel::indoorTemperatureText() const
{
    if (!hasIndoor_)
        return dash();
    return formatTenths(indoorTempX10_);
}

QString DeviceModel::indoorHumidityText() const
{
    if (!hasIndoor_)
        return dash();
    return formatTenths(indoorHumX10_);
}

QString DeviceModel::outdoorCityText() const
{
    if (!hasOutdoor_)
        return dash();
    return outdoorCity_;
}

QString DeviceModel::outdoorTemperatureText() const
{
    if (!hasOutdoor_)
        return dash();
    return formatTenths(outdoorTempX10_);
}

QString DeviceModel::outdoorWeatherText() const
{
    if (!hasOutdoor_)
        return dash();
    return weatherTextFromCode(outdoorCode_);
}

QString DeviceModel::wifiStateText() const
{
    if (!hasWifi_)
        return dash();
    return wifiConnected_ ? QString::fromUtf8("已连接") : QString::fromUtf8("未连接");
}

QString DeviceModel::wifiSsidText() const
{
    if (!hasWifi_)
        return dash();
    return wifiSsid_;
}

QString DeviceModel::wifiRssiText() const
{
    if (!hasWifi_)
        return dash();
    return QStringLiteral("%1 dBm").arg(wifiRssi_);
}

QString DeviceModel::wifiChannelText() const
{
    if (!hasWifi_)
        return dash();
    return QString::number(wifiChannel_);
}

void DeviceModel::applyFrame(const Frame &frame)
{
    bool accepted = false;
    switch (frame.type) {
    case 0x01:
        accepted = acceptFixed(frame.payload, 6);
        break;
    case 0x02:
        accepted = applyTime(frame.payload);
        break;
    case 0x03:
        accepted = applyIndoor(frame.payload);
        break;
    case 0x04:
        accepted = applyOutdoor(frame.payload);
        break;
    case 0x05:
        accepted = applyWifi(frame.payload);
        break;
    case 0x06:
        accepted = applyPage(frame.payload);
        break;
    default:
        // 版本和 CRC 已经由解析器放过。不认识的类型不改字段，但说明链路还有数据。
        accepted = true;
        break;
    }
    if (!accepted)
        return;

    noteActivity();
}

void DeviceModel::onLinkUp()
{
    linkUp_ = true;
    status_ = LinkStatus::Connected;
    timer_.start(timeoutMs_);
    startClock();
    emit changed();
}

void DeviceModel::onLinkLost()
{
    linkUp_ = false;
    timer_.stop();
    clockTimer_.stop();
    status_ = LinkStatus::Disconnected;
    emit changed();
}

void DeviceModel::onClockTick()
{
    if (!linkUp_ || !hasTime_)
        return;
    emit changed();
}

void DeviceModel::startClock()
{
    if (linkUp_ && hasTime_)
        clockTimer_.start();
}

QDateTime DeviceModel::shownTime() const
{
    const QDate date(year_, month_, day_);
    const QTime time(hour_, minute_, second_);
    if (!hasTime_ || !date.isValid() || !time.isValid())
        return {};

    const QDateTime base(date, time);
    if (!linkUp_ || !timeAt_.isValid())
        return base;

    const qint64 elapsed = timeAt_.secsTo(QDateTime::currentDateTime());
    if (elapsed <= 0 || elapsed > 30)
        return base;
    return base.addSecs(elapsed);
}

void DeviceModel::onDataTimeout()
{
    if (!linkUp_)
        return;
    status_ = LinkStatus::DataTimeout;
    emit changed();
}

void DeviceModel::noteActivity()
{
    lastFrameAt_ = QDateTime::currentDateTime();
    hasLastFrame_ = true;
    if (linkUp_) {
        status_ = LinkStatus::Connected;
        timer_.start(timeoutMs_);
    }
    emit changed();
}

bool DeviceModel::applyTime(const QByteArray &payload)
{
    PayloadCursor cursor(payload);
    cursor.u16();
    const int year = cursor.u16();
    const int month = cursor.u8();
    const int day = cursor.u8();
    const int hour = cursor.u8();
    const int minute = cursor.u8();
    const int second = cursor.u8();
    const int weekday = cursor.u8();
    if (!cursor.ok())
        return false;

    hasTime_ = true;
    year_ = year;
    month_ = month;
    day_ = day;
    hour_ = hour;
    minute_ = minute;
    second_ = second;
    weekday_ = weekday;
    timeAt_ = QDateTime::currentDateTime();
    startClock();
    return true;
}

bool DeviceModel::applyIndoor(const QByteArray &payload)
{
    PayloadCursor cursor(payload);
    cursor.u16();
    const int temperature = cursor.i16();
    const int humidity = cursor.u16();
    if (!cursor.ok())
        return false;

    hasIndoor_ = true;
    indoorTempX10_ = temperature;
    indoorHumX10_ = humidity;
    return true;
}

bool DeviceModel::applyOutdoor(const QByteArray &payload)
{
    PayloadCursor cursor(payload);
    cursor.u16();
    const int code = cursor.i16();
    const int temperature = cursor.i16();
    const QString city = cursor.str();
    cursor.str();
    if (!cursor.ok())
        return false;

    hasOutdoor_ = true;
    outdoorCode_ = code;
    outdoorTempX10_ = temperature;
    outdoorCity_ = city;
    return true;
}

bool DeviceModel::applyWifi(const QByteArray &payload)
{
    PayloadCursor cursor(payload);
    cursor.u16();
    const bool connected = cursor.u8() != 0;
    const int rssi = static_cast<qint8>(cursor.u8());
    const int channel = cursor.u8();
    const QString ssid = cursor.str();
    if (!cursor.ok())
        return false;

    hasWifi_ = true;
    wifiConnected_ = connected;
    wifiRssi_ = rssi;
    wifiChannel_ = channel;
    wifiSsid_ = ssid;
    return true;
}

bool DeviceModel::applyPage(const QByteArray &payload)
{
    PayloadCursor cursor(payload);
    cursor.u16();
    const int page = cursor.u8();
    cursor.str();
    if (!cursor.ok())
        return false;

    hasPage_ = true;
    page_ = page;
    return true;
}

bool DeviceModel::acceptFixed(const QByteArray &payload, int size)
{
    return payload.size() == size;
}

QString DeviceModel::dash()
{
    return QStringLiteral("--");
}
