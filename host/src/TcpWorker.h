#pragma once

#include "FrameParser.h"

#include <QObject>
#include <QString>

class QTcpSocket;

class TcpWorker : public QObject
{
    Q_OBJECT
public:
    explicit TcpWorker(QObject *parent = nullptr);

public slots:
    void init();  // 初始化TCP客户端
    void connectToHost(const QString &host, quint16 port);  // 连接到主机
    void disconnectFromHost();  // 断开连接
    void shutdown();  // 关闭TCP客户端

signals:
    void frameReceived(const Frame &frame);  // 接收数据帧信号
    void connected();  // 连接成功信号
    void disconnected();  // 连接断开信号
    void errorOccurred(const QString &message);  // 出现错误信号

private slots:
    void onReadyRead();  // 有数据可读
    void onConnected();  // 连接成功
    void onDisconnected();  // 连接断开
    void onError();  // 出现错误

private:
    QTcpSocket *socket_ = nullptr;  // TCP套接字
    FrameParser parser_;  // 数据解析器
    bool closing_ = false;  // 连接是否断开
    bool notifiedClose_ = false;  // 是否已经通知过连接断开
};
