#include "WeatherText.h"

QString weatherTextFromCode(int code)
{
    if (code == 0 || code == 2 || code == 38)
        return QString::fromUtf8("晴");
    if (code == 1 || code == 3)
        return QString::fromUtf8("夜间");
    if (code == 4 || code == 9)
        return QString::fromUtf8("阴");
    if (code == 5 || code == 6 || code == 7 || code == 8)
        return QString::fromUtf8("多云");
    if (code == 10 || code == 13 || code == 14 || code == 15
        || code == 16 || code == 17 || code == 18 || code == 19)
        return QString::fromUtf8("雨");
    if (code == 11 || code == 12)
        return QString::fromUtf8("雷阵雨");
    if (code == 20 || code == 21 || code == 22 || code == 23 || code == 24 || code == 25)
        return QString::fromUtf8("雪");
    return QString::fromUtf8("其他");
}
