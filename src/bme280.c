/*
 * Project: Remote Mail Notifier (and GPS Tracker)
 * Author: Zak Kemble, contact@zakkemble.net
 * Copyright: (C) 2020 by Zak Kemble
 * License: 
 * Web: https://blog.zakkemble.net/remote-mail-notifier-and-gps-tracker/
 */

#include "stdbool.h"
#include "stdint.h"
#include "stdio.h"
#include "string.h"

#include "time.h"

#include "api_os.h"
#include "api_debug.h"
#include "api_event.h"
#include "api_hal_i2c.h"

#include "demo_mqtt.h"

// Calibration registers are read everytime a sensor is read, maybe read calibration regs once and store in memory?

// TODO
// Sometimes the I2C bus glitches out and the SDA line is stuck low. I'm not sure if its the BME280 or the A9G. When this happens I2C read/writes will fail with error 2 (I2C_ERROR_RESOURCE_BUSY)
// Maybe I2C_Close() then I2C_Init() fixes it?
// It might be caused by GSM interference
// Maybe stronger pullup resistors will fix it, they're 10k at the mo


typedef uint32_t millis_t;

millis_t millis()
{
	return (millis_t)(clock() / CLOCKS_PER_MSEC);
}

#define PRINTD(fmt, ...)                                         \
	do                                                           \
	{                                                            \
		if (DEBUG)                                               \
			Trace(1, ":(%u)(DBG)" fmt, millis(), ##__VA_ARGS__); \
	} while (0)

static void i2cWrite(void *buff, uint8_t len)
{
	I2C_Error_t res = I2C_Transmit(I2C2, ADDR, buff, len, I2C_DEFAULT_TIME_OUT);
	if (res != I2C_ERROR_NONE)
	{
		PRINTD("I2C2 write err: %d", res);
	}
}

static void i2cRead(void *buff, uint8_t len)
{
	I2C_Error_t res = I2C_Receive(I2C2, ADDR, buff, len, I2C_DEFAULT_TIME_OUT);
	if (res != I2C_ERROR_NONE)
	{
		PRINTD("I2C2 read err: %d", res);
	}
}

uint8_t read8(uint8_t reg)
{
	i2cWrite(&reg, 1);
	i2cRead(&reg, 1);
	return reg;
}

uint16_t read16(uint8_t reg)
{
	uint8_t data[2];
	i2cWrite(&reg, 1);
	i2cRead(data, 2);
	return (data[0] << 8) | data[1];
}

uint16_t read16_LE(uint8_t reg)
{
	uint16_t temp = read16(reg);
	return (temp >> 8) | (temp << 8);
}

int16_t readS16_LE(uint8_t reg)
{
	return (int16_t)read16_LE(reg);
}

uint32_t read24(uint8_t reg)
{
	uint8_t data[3];
	i2cWrite(&reg, 1);
	i2cRead(data, 3);
	return (data[0] << 16) | (data[1] << 8) | data[2];
}

void bme280_init()
{
	uint8_t data[2];

	data[0] = REG_CTRL_HUM;
	data[1] = OVERSAMPLE_X16_HUMIDITY;
	i2cWrite(data, 2);

	data[0] = REG_CTRL;
	data[1] = OVERSAMPLE_X16_TEMPERATURE | OVERSAMPLE_X16_PRESSURE | MODE_NORMAL;
	i2cWrite(data, 2);
}

void bme280_startConvertion()
{
	uint8_t data[2];
	data[0] = REG_CTRL;
	data[1] = OVERSAMPLE_X16_TEMPERATURE | OVERSAMPLE_X16_PRESSURE | MODE_FORCE;
	i2cWrite(data, 2);
}

uint8_t bme280_status()
{
	return read8(REG_STATUS) & (0x08 | 0x01);
}

uint8_t bme280_chipid()
{
	return read8(BME280_REGISTER_CHIPID);
}

