#pragma once

#include <QByteArray>
#include <QList>
#include <QMetaType>

/**
 * @brief 解析后的数据帧
 */
struct Frame
{
    quint8 version = 0;  // 版本
    quint8 type = 0;  // 类型
    QByteArray payload;  // 业务数据
    QByteArray raw;  // 原始数据
};
Q_DECLARE_METATYPE(Frame)  // 将Frame注册为元类型

quint16 crc16Modbus(const QByteArray &data);  // 计算 Modbus CRC16

/**
 * @brief 数据解析器
 */
class FrameParser
{
public:
    QList<Frame> feed(const QByteArray &chunk);  // 解析数据
    void clear();  // 清空缓冲区
    int bufferedBytes() const;  // 获取缓冲区大小

private:
    QByteArray buffer_;  // 缓冲区
};
