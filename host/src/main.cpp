#include "DeviceModel.h"
#include "MainWindow.h"
#include "TcpWorker.h"

#include <QApplication>
#include <QThread>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("rain"));  // 组织名称
    QCoreApplication::setApplicationName(QStringLiteral("WeatherClock"));  // 应用程序名称

    DeviceModel model;  // 数据源
    TcpWorker *worker = new TcpWorker; // TCP客户端
    QThread thread;
    worker->moveToThread(&thread);  // 将TCP客户端移出主线程
    QObject::connect(&thread, &QThread::started, worker, &TcpWorker::init);  // 线程启动后初始化TCP客户端
    QObject::connect(worker, &TcpWorker::frameReceived, &model, &DeviceModel::applyFrame, Qt::QueuedConnection);  // 接收数据帧
    QObject::connect(worker, &TcpWorker::connected, &model, &DeviceModel::onLinkUp, Qt::QueuedConnection);  // 连接成功
    QObject::connect(worker, &TcpWorker::disconnected, &model, &DeviceModel::onLinkLost, Qt::QueuedConnection);  // 连接断开
    thread.start();  // 启动TCP客户端线程

    MainWindow window(&model, worker);  // 主窗口
    window.show();  // 显示主窗口
    const int code = QApplication::exec();

    if (thread.isRunning())  // 线程运行中
    {
        QMetaObject::invokeMethod(worker, &TcpWorker::shutdown, Qt::BlockingQueuedConnection);  // 关闭TCP客户端
        thread.quit();  // 退出线程
        thread.wait(3000);  // 等待3秒
    }
    if (worker->thread() == QThread::currentThread())  // 当前线程是TCP客户端线程
        delete worker;  // 删除TCP客户端

    return code;  // 返回退出代码
}
