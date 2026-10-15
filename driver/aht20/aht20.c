#include "FreeRTOS.h"
#include "task.h"
#include "stm32f4xx.h"
#include "aht20.h"

/* do-while(0)：宏整体当作一条语句，调用处可以安全地加分号。
 * 续行符必须是该行最后一个字符，不能写在 // 注释后面。 */
#define I2C_CHECK_EVENT(EVENT, TIMEOUT) \
    do \
    { \
        uint32_t timeout = (TIMEOUT); /* 最大等待时间，单位 us */ \
        while (!I2C_CheckEvent(I2C2, EVENT)) /* 事件未到则继续等 */ \
        { \
            if (timeout == 0) /* 等到 0 仍未发生，按超时失败返回 */ \
                return false; \
            vTaskDelay(pdMS_TO_TICKS(1)); \
            timeout = (timeout > 1000) ? (timeout - 1000) : 0; \
        } \
    } while (0)

/**
 * @brief 写入I2C数据
 * @param data 写入的数据
 * @param length 写入数据的长度
 * @return 写入成功返回true，否则返回false
 */
static bool aht20_write(uint8_t data[], uint32_t length)
{
    I2C_AcknowledgeConfig(I2C2, ENABLE);  // 使能ACK
    I2C_GenerateSTART(I2C2, ENABLE);  // 生成起始信号
    I2C_CHECK_EVENT(I2C_EVENT_MASTER_MODE_SELECT, 1000);  // 等待事件发生
    I2C_Send7bitAddress(I2C2, 0x70, I2C_Direction_Transmitter);  // 发送设备地址
    I2C_CHECK_EVENT(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED, 1000);  // 等待事件发生
    for (uint32_t i = 0; i < length; i++)  // 发送数据
    {
        I2C_SendData(I2C2, data[i]);
        I2C_CHECK_EVENT(I2C_EVENT_MASTER_BYTE_TRANSMITTING, 1000);  // 等待事件发生
    }
    I2C_GenerateSTOP(I2C2, ENABLE);  // 生成停止信号
    return true;
}

/**
 * @brief 读取I2C数据
 * @param data 保存读取数据的指针
 * @param length 读取数据的长度
 * @return 读取成功返回true，否则返回false
 */
static bool aht20_read(uint8_t data[], uint32_t length)
{
    I2C_AcknowledgeConfig(I2C2, ENABLE);  // 使能ACK
    I2C_GenerateSTART(I2C2, ENABLE);  // 生成起始信号
    I2C_CHECK_EVENT(I2C_EVENT_MASTER_MODE_SELECT, 1000);  // 等待事件发生
    I2C_Send7bitAddress(I2C2, 0x70, I2C_Direction_Receiver);  // 发送设备地址
    I2C_CHECK_EVENT(I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED, 1000);  // 等待事件发生
    for (uint32_t i = 0; i < length; i++)  // 读取数据
    {
        if (i == length - 1)  // 最后一个字节
            I2C_AcknowledgeConfig(I2C2, DISABLE);  // 禁止发送ACK
        I2C_CHECK_EVENT(I2C_EVENT_MASTER_BYTE_RECEIVED, 1000);
        data[i] = I2C_ReceiveData(I2C2);
    }
    I2C_GenerateSTOP(I2C2, ENABLE);  // 生成停止信号
    return true;
}

/**
 * @brief 读取AHT20的校准状态
 * @param status 保存校准状态的指针
 * @return 读取成功返回true，否则返回false
 */
static bool aht20_read_status(uint8_t *status)
{
    uint8_t cmd = 0x71;  // AHT20的读状态命令
    if (!aht20_write(&cmd, 1))  // 发送读状态命令
        return false;
    if (!aht20_read(status, 1))  // 读取校准状态
        return false;
    return true;
}

/**
 * @brief 判断AHT20是否已经校准完成，可以正常测量
 * @return 校准完成返回true，否则返回false
 */
static bool aht20_is_ready(void)
{
    uint8_t status;
    if (!aht20_read_status(&status))  // 读取校准状态
        return false;
    return (status & 0x08) != 0;  // 判断校准状态是否为完成
}

/**
 * @brief 判断AHT20是否正在测量
 * @return 正在测量返回true，否则返回false
 */
static bool aht20_is_busy(void)
{
    uint8_t status;
    if (!aht20_read_status(&status))  // 读取校准状态
        return false;
    return (status & 0x80) != 0;  // 判断校准状态是否为正在测量
}

/**
 * @brief 初始化AHT20
 * @return 初始化成功返回true，否则返回false
 */
bool aht20_init(void)
{
    // 初始化I2C
    I2C_InitTypeDef I2C_InitStruct;
    I2C_StructInit(&I2C_InitStruct);
    I2C_InitStruct.I2C_Ack = I2C_Ack_Enable;
    I2C_InitStruct.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    I2C_InitStruct.I2C_ClockSpeed = 100ul * 1000ul;
    I2C_InitStruct.I2C_DutyCycle = I2C_DutyCycle_2;
    I2C_InitStruct.I2C_Mode = I2C_Mode_I2C;
    I2C_InitStruct.I2C_OwnAddress1 = 0x00;
    I2C_Init(I2C2, &I2C_InitStruct);

    // 初始化IIC GPIO
    GPIO_InitTypeDef GPIO_InitStruct;
    GPIO_StructInit(&GPIO_InitStruct);
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AF;
    GPIO_InitStruct.GPIO_OType = GPIO_OType_OD;
    GPIO_InitStruct.GPIO_PuPd = GPIO_PuPd_NOPULL;
    GPIO_InitStruct.GPIO_Speed = GPIO_High_Speed;
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_10 | GPIO_Pin_11;
    GPIO_Init(GPIOB, &GPIO_InitStruct);
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource10, GPIO_AF_I2C2);
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource11, GPIO_AF_I2C2);
    
    vTaskDelay(pdMS_TO_TICKS(40));  // AHT20规定的上电等待时间
    if (aht20_is_ready())  // 检查AHT20是否校准成功
        return true;
    
    // 校准失败
    if (!aht20_write((uint8_t[]){0xBE, 0x08, 0x00}, 3))  // 重新加载校准参数
        return false;
    
    for (uint32_t t = 0; t < 20; t ++)  // 等待校准完成，最多等待200ms
    {
        vTaskDelay(pdMS_TO_TICKS(5));
        if (aht20_is_ready())  // 检查校准状态是否为完成
            return true;
    }
    return false;  // 校准失败
}

/**
 * @brief 开始AHT20的测量
 * @return 开始测量成功返回true，否则返回false
 */
 bool aht20_start_measurement(void)
 {
     return aht20_write((uint8_t[]){0xAC, 0x33, 0x00}, 3);  // 发送开始测量命令
 }

/**
 * @brief 等待AHT20的测量完成
 * @return 测量完成返回true，否则返回false
 */
 bool aht20_wait_for_measurement(void)
 {
     for (uint32_t t = 0; t < 20; t++)  // 等待测量完成，最多等待200ms
     {
         vTaskDelay(pdMS_TO_TICKS(10));
         if (!aht20_is_busy())  // 检查测量是否完成
             return true;
     }
     return false;  // 测量超时
 }

 /**
 * @brief 读取AHT20的测量结果
 * @param temperature 保存温度的指针
 * @param humidity 保存湿度的指针
 * @return 读取成功返回true，否则返回false
 */
bool aht20_read_measurement(float *temperature, float *humidity)
{
    uint8_t data[6];
    if (!aht20_read(data, 6))  // 读取测量结果
        return false;
    
    uint32_t raw_humidity = ((uint32_t)data[1] << 12) | ((uint32_t)data[2] << 4) | 
        ((uint32_t)(data[3] &0xF0) >> 4);  // 计算湿度
    uint32_t raw_temperature = ((uint32_t)(data[3] & 0x0F) << 16) | ((uint32_t)data[4] << 8) | 
        ((uint32_t)data[5]);  // 计算温度
    
    *humidity = (float)raw_humidity * 100.0f / (float)0x100000;  // 转换为湿度
    *temperature = (float)raw_temperature * 200.0f / (float)0x100000 - 50.0f;  // 转换为温度
    
    return true;  // 读取成功
}
