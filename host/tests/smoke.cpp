#include "DeviceModel.h"
#include "MainWindow.h"
#include "TcpWorker.h"

#include <QApplication>
#include <QTimer>

// 构造主窗口后立刻退出，确认控件能创建。不连接设备。
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("WeatherClock"));
    QCoreApplication::setApplicationName(QStringLiteral("Host"));

    DeviceModel model;
    TcpWorker worker;
    MainWindow window(&model, &worker);
    window.show();
    QTimer::singleShot(0, &app, &QApplication::quit);
    return QApplication::exec();
}
