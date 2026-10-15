#include "TcpWorker.h"

#include <QCoreApplication>
#include <QNetworkProxy>
#include <QTcpSocket>

TcpWorker::TcpWorker(QObject *parent) : QObject(parent)
{
}

/**
 * @brief 初始化TCP客户端
 */
void TcpWorker::init()
{
    if (socket_ != nullptr)  // 已经初始化
        return;

    socket_ = new QTcpSocket(this);  // 创建TCP套接字
    socket_->setProxy(QNetworkProxy::NoProxy);  // 直连TCP不走代理
    connect(socket_, &QTcpSocket::readyRead, this, &TcpWorker::onReadyRead);  // 有数据可读
    connect(socket_, &QTcpSocket::connected, this, &TcpWorker::onConnected);  // 连接成功
    connect(socket_, &QTcpSocket::disconnected, this, &TcpWorker::onDisconnected);  // 连接断开
    connect(socket_, &QTcpSocket::errorOccurred, this, &TcpWorker::onError);  // 出现错误
}

/**
 * @brief 连接到主机
 */
void TcpWorker::connectToHost(const QString &host, quint16 port)
{
    if (socket_ == nullptr)  // 没有初始化
        init();  // 初始化TCP客户端

    closing_ = false;  // 设置连接未断开
    notifiedClose_ = false;  // 设置未通知过连接断开
    parser_.clear();  // 清空解析器缓冲区
    socket_->connectToHost(host, port);  // 连接到主机
}

/**
 * @brief 断开连接
 */
void TcpWorker::disconnectFromHost()
{
    if (socket_ == nullptr)  // 没有初始化
        return;

    closing_ = true;  // 设置连接已断开
    parser_.clear();
    notifiedClose_ = false;  // 设置未通知过连接断开
    if (socket_->state() != QAbstractSocket::UnconnectedState)  // 连接状态不是未连接
        socket_->abort();  // 断开连接
    if (!notifiedClose_) {  // 未通知过连接断开
        notifiedClose_ = true;  // 设置已通知过连接断开
        emit disconnected();  // 发送连接断开信号到DeviceModel
    }
}

/**
 * @brief 关闭TCP客户端
 */
void TcpWorker::shutdown()
{
    closing_ = true;  // 设置连接已断开
    if (socket_ != nullptr)  // 有TCP套接字
    {
        socket_->disconnect(this);  // 断开连接
        socket_->abort();  // 断开连接
        delete socket_;  // 删除TCP套接字
        socket_ = nullptr;  // 设置为空
    }
    parser_.clear();  // 清空解析器缓冲区

    QThread *const mainThread = QCoreApplication::instance() != nullptr
        ? QCoreApplication::instance()->thread() : nullptr;  // 获取主线程
    if (mainThread != nullptr && thread() != mainThread)  // 主线程不是当前线程
        moveToThread(mainThread);  // 移动到主线程
}

/**
 * @brief 有数据可读
 */
void TcpWorker::onReadyRead()
{
    if (socket_ == nullptr)  // 没有初始化
        return;

    const QList<Frame> frames = parser_.feed(socket_->readAll());  // 解析数据
    for (const Frame &frame : frames)  // 发送数据帧到DeviceModel
        emit frameReceived(frame);
}

/**
 * @brief 连接成功
 */
void TcpWorker::onConnected()
{
    closing_ = false;  
    notifiedClose_ = false;
    emit connected();  // 发送连接成功信号到DeviceModel
}

/**
 * @brief 连接断开
 */
void TcpWorker::onDisconnected()
{
    parser_.clear();  // 清空解析器缓冲区
    if (notifiedClose_)  // 已经通知过连接断开
        return;
    notifiedClose_ = true;  // 设置已经通知过连接断开
    emit disconnected();  // 发送连接断开信号到DeviceModel
}

/**
 * @brief 出现错误
 */
void TcpWorker::onError()
{
    if (closing_ || socket_ == nullptr)  // 连接已断开或没有初始化
        return;

    QString message;  // 错误信息
    switch (socket_->error())  // 错误类型
    {
        case QAbstractSocket::ConnectionRefusedError:
            message = QString::fromUtf8("连接被拒绝。请核对屏幕上的 IP，设备应正在监听该端口");
            break;
        case QAbstractSocket::HostNotFoundError:
            message = QString::fromUtf8("找不到这台主机");
            break;
        case QAbstractSocket::SocketTimeoutError:
            message = QString::fromUtf8("连接超时");
            break;
        case QAbstractSocket::NetworkError:
            message = QString::fromUtf8("网络不可达。电脑需要和设备在同一个局域网");
            break;
        case QAbstractSocket::RemoteHostClosedError:
            message = QString::fromUtf8("设备关闭了连接");
            break;
        default:
            message = socket_->errorString();
            break;
    }
    emit errorOccurred(message);  // 发送错误信号到MainWindow
}
