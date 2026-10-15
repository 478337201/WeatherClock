#include "FrameParser.h"

namespace
{
    constexpr int kMaxBufferBytes = 4096;  // 最大缓冲区大小
    constexpr int kMaxPayload = 240;  // 最大业务数据大小
    constexpr quint8 kVersion = 1;  // 版本
}  // namespace

/**
 * @brief 计算 Modbus CRC16
 * @param data 数据
 * @return CRC16
 */
quint16 crc16Modbus(const QByteArray &data)
{
    quint16 crc = 0xFFFF;
    for (qsizetype i = 0; i < data.size(); ++i)
    {
        crc ^= static_cast<quint8>(data.at(i));
        for (int bit = 0; bit < 8; ++bit)
        {
            if ((crc & 0x0001u) != 0)
                crc = static_cast<quint16>((crc >> 1) ^ 0xA001u);
            else
                crc = static_cast<quint16>(crc >> 1);
        }
    }
    return crc;
}

/**
 * @brief 解析数据（处理TCP粘包与拆包）
 * @param chunk 数据
 * @return 解析后的数据
 */
QList<Frame> FrameParser::feed(const QByteArray &chunk)
{
    if (!chunk.isEmpty())  // 有数据
        buffer_.append(chunk);  // 添加到缓冲区

    if (buffer_.size() > kMaxBufferBytes)  // 缓冲区大小超过最大值
        buffer_.remove(0, buffer_.size() - kMaxBufferBytes);  // 移除超过最大值的数据

    QList<Frame> frames;  // 解析后的数据
    while (true)
    {
        int magic = -1;  // 帧头位置
        const qsizetype size = buffer_.size(); // 缓冲区大小
        for (qsizetype i = 0; i + 1 < size; ++i) // 遍历缓冲区
        {
            // 判断是否为帧头
            if (static_cast<quint8>(buffer_.at(i)) == 0x55 && static_cast<quint8>(buffer_.at(i + 1)) == 0xAA)
            {
                magic = static_cast<int>(i);  // 找到帧头
                break;
            }
        }
        if (magic < 0)  // 找不到 55 AA。末尾单独的0x55可能是下一字节才到的帧头，其余丢掉
        {
            if (!buffer_.isEmpty() && static_cast<quint8>(buffer_.back()) == 0x55)
                buffer_ = buffer_.right(1);
            else
                buffer_.clear();
            break;
        }
        if (magic > 0)  // 找到帧头，移除帧头前面的数据
            buffer_.remove(0, magic);
        if (buffer_.size() < 6)  // 数据长度不够
            break;

        const quint8 version = static_cast<quint8>(buffer_.at(2));  // 版本
        const quint8 type = static_cast<quint8>(buffer_.at(3));  // 类型
        const int length = static_cast<quint8>(buffer_.at(4))  // 数据长度
            | (static_cast<int>(static_cast<quint8>(buffer_.at(5))) << 8);  // 数据长度
        if (version != kVersion || length > kMaxPayload)  // 版本或数据长度不正确
        {
            buffer_.remove(0, 1);  // 移除第一个字节
            continue;
        }

        const int total = 8 + length;  // 总长度
        if (buffer_.size() < total)
            break;  // 数据长度不够

        const QByteArray covered = buffer_.mid(2, 4 + length);  // 覆盖数据
        const quint16 actual = static_cast<quint8>(buffer_.at(6 + length))  // 实际 CRC16
            | (static_cast<quint16>(static_cast<quint8>(buffer_.at(7 + length))) << 8);  // 实际 CRC16
        if (crc16Modbus(covered) != actual)  // 实际 CRC16 不正确
        {
            buffer_.remove(0, 1);  // 移除第一个字节
            continue;
        }

        Frame frame;  // 帧数据
        frame.version = version;  // 版本
        frame.type = type;  // 类型
        frame.payload = buffer_.mid(6, length);  // 业务数据
        frame.raw = buffer_.left(total);  // 原始数据
        frames.append(frame);  // 添加到列表
        buffer_.remove(0, total);  // 移除总长度
    }
    return frames;  // 返回解析后的数据
}

/**
 * @brief 清空缓冲区
 */
void FrameParser::clear()
{
    buffer_.clear();
}

/**
 * @brief 获取缓冲区大小
 * @return 缓冲区大小
 */
int FrameParser::bufferedBytes() const
{
    return static_cast<int>(buffer_.size());
}
