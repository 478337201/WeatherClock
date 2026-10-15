#include "weather.h"
#include <string.h>
#include <stdlib.h>

/**
 * @brief 定位 JSON 键对应的值。冒号两侧的空白都跳过
 * @param json JSON 文本
 * @param key 带引号的键名，例如 "\"text\""
 * @return 值的起始位置。找不到返回 NULL
 */
static const char *json_value(const char *json, const char *key)
{
	const char *p = strstr(json, key);

	if (p == NULL)
		return NULL;
	p += strlen(key);
	while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
		p++;
	if (*p != ':')
		return NULL;
	p++;
	while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
		p++;
	return p;
}

/**
 * @brief 拷贝 JSON 字符串。心知接口的值带引号，冒号后不一定有空格
 * @return 读到成对引号时返回 true
 */
static bool json_copy_string(const char *json, const char *key, char *dst, size_t size)
{
	const char *p;
	size_t n = 0;

	if (size == 0)
		return false;
	dst[0] = '\0';
	p = json_value(json, key);
	if (p == NULL || *p != '"')
		return false;
	p++;
	while (*p != '\0' && *p != '"' && n + 1 < size)
	{
		if (*p == '\\' && p[1] != '\0')
			p++;
		dst[n++] = *p++;
	}
	dst[n] = '\0';
	return *p == '"';
}

/**
 * @brief 解析天气响应
 * @param response 天气响应
 * @param info 天气信息
 * @return 是否解析成功
 */
bool parse_seniverse_response(const char *response, weather_info_t *info)
{
	const char *now;
	char code[8];
	char temperature[16];
	bool got_text;
	bool got_temperature;

	if (response == NULL || info == NULL)
		return false;

	memset(info, 0, sizeof(*info));
	info->weather_code = -1;

	response = strstr(response, "\"results\":");
	if (response == NULL)
		return false;
	if (strstr(response, "\"location\":") == NULL)
		return false;

	now = strstr(response, "\"now\":");
	if (now == NULL)
		return false;

	json_copy_string(response, "\"name\"", info->city, sizeof(info->city));
	got_text = json_copy_string(now, "\"text\"", info->weather, sizeof(info->weather));
	if (json_copy_string(now, "\"code\"", code, sizeof(code)))
		info->weather_code = atoi(code);
	got_temperature = json_copy_string(now, "\"temperature\"", temperature, sizeof(temperature));
	if (got_temperature)
		info->temperature = (float)atof(temperature);

	return got_text || got_temperature;
}
