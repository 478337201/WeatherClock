#include "DeviceModel.h"
#include "FrameParser.h"
#include "WeatherText.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>

namespace {

const QByteArray kKnownFrame = QByteArray::fromHex("55AA01010600010000000000ADA9");

void appendU8(QByteArray *out, quint8 value)
{
    out->append(static_cast<char>(value));
}

void appendU16(QByteArray *out, quint16 value)
{
    appendU8(out, static_cast<quint8>(value));
    appendU8(out, static_cast<quint8>(value >> 8));
}

void appendI16(QByteArray *out, qint16 value)
{
    appendU16(out, static_cast<quint16>(value));
}

void appendStr(QByteArray *out, const QString &text)
{
    const QByteArray utf8 = text.toUtf8();
    appendU8(out, static_cast<quint8>(utf8.size()));
    out->append(utf8);
}

QByteArray wrapFrame(quint8 type, const QByteArray &payload)
{
    QByteArray raw;
    appendU8(&raw, 0x55);
    appendU8(&raw, 0xAA);
    appendU8(&raw, 1);
    appendU8(&raw, type);
    appendU16(&raw, static_cast<quint16>(payload.size()));
    raw.append(payload);
    appendU16(&raw, crc16Modbus(raw.mid(2)));
    return raw;
}

Frame feedOne(const QByteArray &raw)
{
    FrameParser parser;
    const QList<Frame> frames = parser.feed(raw);
    return frames.size() == 1 ? frames.first() : Frame();
}

QByteArray indoorPayload(qint16 tempX10, quint16 humX10)
{
    QByteArray payload;
    appendU16(&payload, 1);
    appendI16(&payload, tempX10);
    appendU16(&payload, humX10);
    return payload;
}

QString zh(const char *utf8)
{
    return QString::fromUtf8(utf8);
}

} // namespace

class FrameParserTest : public QObject
{
    Q_OBJECT

private slots:
    void knownVectorCrc()
    {
        const QByteArray covered = QByteArray::fromHex("01010600010000000000");
        QCOMPARE(crc16Modbus(covered), quint16(0xA9AD));
    }

    void knownVectorFrame()
    {
        QCOMPARE(wrapFrame(0x01, QByteArray::fromHex("010000000000")), kKnownFrame);

        FrameParser parser;
        const QList<Frame> frames = parser.feed(kKnownFrame);
        QCOMPARE(frames.size(), 1);
        QCOMPARE(frames[0].version, quint8(1));
        QCOMPARE(frames[0].type, quint8(0x01));
        QCOMPARE(frames[0].payload, QByteArray::fromHex("010000000000"));
        QCOMPARE(frames[0].raw, kKnownFrame);
        QCOMPARE(parser.bufferedBytes(), 0);
    }

    void badCrcIsDropped()
    {
        QByteArray bad = kKnownFrame;
        bad[bad.size() - 1] = static_cast<char>(static_cast<quint8>(bad.back()) ^ 0xFFu);
        QByteArray both = bad;
        both.append(kKnownFrame);

        FrameParser parser;
        const QList<Frame> frames = parser.feed(both);
        QCOMPARE(frames.size(), 1);
        QCOMPARE(frames[0].raw, kKnownFrame);
        QCOMPARE(parser.bufferedBytes(), 0);
    }

    void halfFrameStaysUntilComplete()
    {
        FrameParser parser;
        QCOMPARE(parser.feed(kKnownFrame.left(7)).size(), 0);
        QCOMPARE(parser.bufferedBytes(), 7);
        QCOMPARE(parser.feed(QByteArray()).size(), 0);
        QCOMPARE(parser.bufferedBytes(), 7);

        const QList<Frame> frames = parser.feed(kKnownFrame.mid(7));
        QCOMPARE(frames.size(), 1);
        QCOMPARE(frames[0].raw, kKnownFrame);
        QCOMPARE(parser.bufferedBytes(), 0);
    }

    void stickyFrames()
    {
        FrameParser parser;
        const QList<Frame> frames = parser.feed(kKnownFrame + kKnownFrame);
        QCOMPARE(frames.size(), 2);
        QCOMPARE(frames[0].type, quint8(0x01));
        QCOMPARE(frames[1].payload, frames[0].payload);
    }

    void leadingGarbage()
    {
        FrameParser parser;
        const QList<Frame> frames = parser.feed(QByteArray::fromHex("001155") + kKnownFrame);
        QCOMPARE(frames.size(), 1);
        QCOMPARE(frames[0].raw, kKnownFrame);
    }

    void lonePrefixByteIsKept()
    {
        FrameParser parser;
        QCOMPARE(parser.feed(QByteArray(1, char(0x55))).size(), 0);
        QCOMPARE(parser.bufferedBytes(), 1);
        QCOMPARE(parser.feed(QByteArray::fromHex("00")).size(), 0);
        QCOMPARE(parser.bufferedBytes(), 0);
    }

    void byteAtATime()
    {
        FrameParser parser;
        QList<Frame> frames;
        for (char byte : kKnownFrame)
            frames += parser.feed(QByteArray(1, byte));
        QCOMPARE(frames.size(), 1);
        QCOMPARE(frames[0].raw, kKnownFrame);
    }

    void clearDropsPartialFrame()
    {
        FrameParser parser;
        parser.feed(kKnownFrame.left(9));
        parser.clear();
        QCOMPARE(parser.bufferedBytes(), 0);
        QCOMPARE(parser.feed(kKnownFrame.mid(9)).size(), 0);
    }

    void rejectsBadVersionAndLengthThenResyncs()
    {
        FrameParser parser;
        const QByteArray version = QByteArray::fromHex("55AA02010000FFFF") + kKnownFrame;
        const QList<Frame> fromVersion = parser.feed(version);
        QCOMPARE(fromVersion.size(), 1);
        QCOMPARE(fromVersion[0].raw, kKnownFrame);

        const QByteArray length = QByteArray::fromHex("55AA0101F100") + kKnownFrame;
        const QList<Frame> fromLength = parser.feed(length);
        QCOMPARE(fromLength.size(), 1);
        QCOMPARE(fromLength[0].type, quint8(0x01));
    }

    void bufferKeepsFrameAtTheTail()
    {
        FrameParser parser;
        QByteArray chunk(5000, char(0x11));
        chunk.append(kKnownFrame);
        const QList<Frame> frames = parser.feed(chunk);
        QCOMPARE(frames.size(), 1);
        QCOMPARE(frames[0].raw, kKnownFrame);
        QVERIFY(parser.bufferedBytes() <= 4096);

        QByteArray split(4090, char(0x11));
        split.append(kKnownFrame.left(10));
        QCOMPARE(parser.feed(split).size(), 0);
        QCOMPARE(parser.bufferedBytes(), 10);
        const QList<Frame> rest = parser.feed(kKnownFrame.mid(10));
        QCOMPARE(rest.size(), 1);
        QCOMPARE(rest[0].raw, kKnownFrame);
    }
};

class WeatherTextTest : public QObject
{
    Q_OBJECT

private slots:
    void matchesLcdBranches()
    {
        const struct
        {
            int code;
            const char *text;
        } cases[] = {
            {0, "晴"}, {2, "晴"}, {38, "晴"},
            {1, "夜间"}, {3, "夜间"},
            {4, "阴"}, {9, "阴"},
            {5, "多云"}, {6, "多云"}, {7, "多云"}, {8, "多云"},
            {10, "雨"}, {13, "雨"}, {16, "雨"}, {19, "雨"},
            {11, "雷阵雨"}, {12, "雷阵雨"},
            {20, "雪"}, {21, "雪"}, {24, "雪"}, {25, "雪"},
            {-1, "其他"}, {26, "其他"}, {39, "其他"},
        };
        for (const auto &item : cases)
            QCOMPARE(weatherTextFromCode(item.code), zh(item.text));
    }
};

class DeviceModelTest : public QObject
{
    Q_OBJECT

private slots:
    void missingFieldsAreDash()
    {
        DeviceModel model;
        QCOMPARE(model.statusText(), zh("未连接"));
        QCOMPARE(model.timeText(), QStringLiteral("--"));
        QCOMPARE(model.dateText(), QStringLiteral("--"));
        QCOMPARE(model.weekdayText(), QStringLiteral("--"));
        QCOMPARE(model.pageText(), QStringLiteral("--"));
        QCOMPARE(model.indoorTemperatureText(), QStringLiteral("--"));
        QCOMPARE(model.indoorHumidityText(), QStringLiteral("--"));
        QCOMPARE(model.outdoorCityText(), QStringLiteral("--"));
        QCOMPARE(model.outdoorTemperatureText(), QStringLiteral("--"));
        QCOMPARE(model.outdoorWeatherText(), QStringLiteral("--"));
        QCOMPARE(model.wifiStateText(), QStringLiteral("--"));
        QCOMPARE(model.wifiSsidText(), QStringLiteral("--"));
        QCOMPARE(model.wifiRssiText(), QStringLiteral("--"));
        QCOMPARE(model.wifiChannelText(), QStringLiteral("--"));
        QCOMPARE(model.lastFrameTimeText(), QStringLiteral("--"));
    }

    void indoorOneDecimalIncludingZero()
    {
        DeviceModel model;
        model.applyFrame(feedOne(wrapFrame(0x03, indoorPayload(264, 452))));
        QCOMPARE(model.indoorTemperatureText(), QStringLiteral("26.4"));
        QCOMPARE(model.indoorHumidityText(), QStringLiteral("45.2"));
        QCOMPARE(model.statusText(), zh("未连接"));

        model.applyFrame(feedOne(wrapFrame(0x03, indoorPayload(0, 0))));
        QCOMPARE(model.indoorTemperatureText(), QStringLiteral("0.0"));
        QCOMPARE(model.indoorHumidityText(), QStringLiteral("0.0"));

        model.applyFrame(feedOne(wrapFrame(0x03, indoorPayload(-4, 1000))));
        QCOMPARE(model.indoorTemperatureText(), QStringLiteral("-0.4"));
        QCOMPARE(model.indoorHumidityText(), QStringLiteral("100.0"));

        model.applyFrame(feedOne(wrapFrame(0x03, indoorPayload(-264, 1))));
        QCOMPARE(model.indoorTemperatureText(), QStringLiteral("-26.4"));
    }

    void timeWeekdayAndPage()
    {
        DeviceModel model;
        QByteArray time;
        appendU16(&time, 4);
        appendU16(&time, 2026);
        appendU8(&time, 10);
        appendU8(&time, 18);
        appendU8(&time, 21);
        appendU8(&time, 3);
        appendU8(&time, 8);
        appendU8(&time, 7);
        model.applyFrame(feedOne(wrapFrame(0x02, time)));
        QCOMPARE(model.timeText(), QStringLiteral("21:03:08"));
        QCOMPARE(model.dateText(), QStringLiteral("2026-10-18"));
        QCOMPARE(model.weekdayText(), zh("星期日"));

        time[time.size() - 1] = 1;
        model.applyFrame(feedOne(wrapFrame(0x02, time)));
        QCOMPARE(model.weekdayText(), zh("星期一"));
        QCOMPARE(model.timeText(), QStringLiteral("21:03:08"));

        time[time.size() - 1] = 0;
        model.applyFrame(feedOne(wrapFrame(0x02, time)));
        QCOMPARE(model.weekdayText(), QStringLiteral("--"));
        QCOMPARE(model.timeText(), QStringLiteral("21:03:08"));

        QByteArray page;
        appendU16(&page, 1);
        appendU8(&page, 0);
        appendStr(&page, QString());
        model.applyFrame(feedOne(wrapFrame(0x06, page)));
        QCOMPARE(model.pageText(), zh("主页"));

        page[2] = 1;
        model.applyFrame(feedOne(wrapFrame(0x06, page)));
        QCOMPARE(model.pageText(), zh("错误页"));
    }

    void outdoorUsesCodeNotRawText()
    {
        DeviceModel model;
        QByteArray payload;
        appendU16(&payload, 3);
        appendI16(&payload, 11);
        appendI16(&payload, 264);
        appendStr(&payload, zh("西安"));
        appendStr(&payload, zh("强对流"));
        model.applyFrame(feedOne(wrapFrame(0x04, payload)));
        QCOMPARE(model.outdoorCityText(), zh("西安"));
        QCOMPARE(model.outdoorTemperatureText(), QStringLiteral("26.4"));
        QCOMPARE(model.outdoorWeatherText(), zh("雷阵雨"));

        payload.clear();
        appendU16(&payload, 4);
        appendI16(&payload, -1);
        appendI16(&payload, 0);
        appendStr(&payload, zh("西安"));
        appendStr(&payload, zh("未知"));
        model.applyFrame(feedOne(wrapFrame(0x04, payload)));
        QCOMPARE(model.outdoorWeatherText(), zh("其他"));
        QCOMPARE(model.outdoorTemperatureText(), QStringLiteral("0.0"));
    }

    void wifiFields()
    {
        DeviceModel model;
        QByteArray payload;
        appendU16(&payload, 2);
        appendU8(&payload, 1);
        appendU8(&payload, static_cast<quint8>(qint8(-62)));
        appendU8(&payload, 6);
        appendStr(&payload, QStringLiteral("lab"));
        model.applyFrame(feedOne(wrapFrame(0x05, payload)));
        QCOMPARE(model.wifiStateText(), zh("已连接"));
        QCOMPARE(model.wifiRssiText(), QStringLiteral("-62 dBm"));
        QCOMPARE(model.wifiChannelText(), QStringLiteral("6"));
        QCOMPARE(model.wifiSsidText(), QStringLiteral("lab"));

        payload[2] = 0;
        payload[3] = static_cast<char>(static_cast<quint8>(qint8(-70)));
        model.applyFrame(feedOne(wrapFrame(0x05, payload)));
        QCOMPARE(model.wifiStateText(), zh("未连接"));
        QCOMPARE(model.wifiRssiText(), QStringLiteral("-70 dBm"));
    }

    void knownHeartbeatDoesNotInventValues()
    {
        DeviceModel model(nullptr, 80);
        model.onLinkUp();
        QCOMPARE(model.statusText(), zh("已连接"));
        model.applyFrame(feedOne(kKnownFrame));
        QCOMPARE(model.indoorTemperatureText(), QStringLiteral("--"));
        QCOMPARE(model.timeText(), QStringLiteral("--"));
        QVERIFY(model.lastFrameTimeText() != QStringLiteral("--"));
        QCOMPARE(model.statusText(), zh("已连接"));
    }

    void malformedPayloadKeepsPreviousValue()
    {
        DeviceModel model(nullptr, 80);
        model.onLinkUp();
        model.applyFrame(feedOne(wrapFrame(0x03, indoorPayload(264, 452))));

        Frame bad;
        bad.version = 1;
        bad.type = 0x03;
        bad.payload = QByteArray::fromHex("0100");
        bad.raw = QByteArray::fromHex("55AA");
        QSignalSpy spy(&model, &DeviceModel::changed);
        model.applyFrame(bad);
        QCOMPARE(spy.count(), 0);
        QCOMPARE(model.indoorTemperatureText(), QStringLiteral("26.4"));
        QCOMPARE(model.indoorHumidityText(), QStringLiteral("45.2"));

        QVERIFY(spy.wait(1000));
        QCOMPARE(model.statusText(), zh("数据超时"));
        QCOMPARE(model.indoorTemperatureText(), QStringLiteral("26.4"));
    }

    void disconnectKeepsValues()
    {
        DeviceModel model(nullptr, 80);
        model.onLinkUp();
        model.applyFrame(feedOne(wrapFrame(0x03, indoorPayload(264, 452))));
        const QString stamp = model.lastFrameTimeText();
        QVERIFY(stamp != QStringLiteral("--"));

        model.onLinkLost();
        QCOMPARE(model.statusText(), zh("未连接"));
        QCOMPARE(model.indoorTemperatureText(), QStringLiteral("26.4"));
        QCOMPARE(model.lastFrameTimeText(), stamp);
        QTest::qWait(200);
        QCOMPARE(model.statusText(), zh("未连接"));
        QCOMPARE(model.indoorTemperatureText(), QStringLiteral("26.4"));
    }

    void dataTimeoutThenAnyFrameRestoresLink()
    {
        DeviceModel model(nullptr, 80);
        model.onLinkUp();
        model.applyFrame(feedOne(wrapFrame(0x03, indoorPayload(264, 452))));
        QSignalSpy spy(&model, &DeviceModel::changed);
        QVERIFY(spy.wait(1000));
        QCOMPARE(model.statusText(), zh("数据超时"));
        QCOMPARE(model.indoorTemperatureText(), QStringLiteral("26.4"));

        model.applyFrame(feedOne(kKnownFrame));
        QCOMPARE(model.statusText(), zh("已连接"));
        QCOMPARE(model.indoorTemperatureText(), QStringLiteral("26.4"));

        model.onLinkLost();
        QCOMPARE(model.statusText(), zh("未连接"));
        QCOMPARE(model.indoorTemperatureText(), QStringLiteral("26.4"));
    }
};

int main(int argc, char **argv)
{
    // 自己持有事件循环。多次 qExec 各自创建再销毁 QCoreApplication 时，定时器测试会空转。
    QCoreApplication app(argc, argv);
    int status = 0;
    {
        FrameParserTest test;
        status |= QTest::qExec(&test, argc, argv);
    }
    {
        WeatherTextTest test;
        status |= QTest::qExec(&test, argc, argv);
    }
    {
        DeviceModelTest test;
        status |= QTest::qExec(&test, argc, argv);
    }
    return status;
}

#include "host_tests.moc"
