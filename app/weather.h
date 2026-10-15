#ifndef __WEAHTER_H__
#define __WEAHTER_H__

#include <stdbool.h>

typedef struct
{
	char city[32]; // 城市名称
	char weather[32]; // 天气现象，来自 "text"
	int weather_code; // 天气代码。缺省为-1
	float temperature; // 温度
} weather_info_t;  // 天气信息结构体

bool parse_seniverse_response(const char *response, weather_info_t *info); // 解析天气响应

#endif // __WEAHTER_H__
