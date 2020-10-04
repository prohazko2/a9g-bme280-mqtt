#ifndef __DEMO_MQTT_H_
#define __DEMO_MQTT_H_


#define BROKER_IP  "sandbox.rightech.io"
#define BROKER_PORT 1883
//#define CLIENT_ID "mqtt-olegprohazko-MLYTYM2E2fNa"
//#define CLIENT_USER "test"
//#define CLIENT_PASS "test"
#define SUBSCRIBE_TOPIC "proha/#"

#define PUBLISH_INTERVAL 5 * 60000 //5min
//#define PUBLISH_INTERVAL 10000 //10s


#define ADDR 0x76

#define DEBUG 1

#define REG_CALI_DIG_T1 0x88
#define REG_CALI_DIG_T2 0x8A
#define REG_CALI_DIG_T3 0x8C

#define REG_CALI_DIG_P1 0x8E
#define REG_CALI_DIG_P2 0x90
#define REG_CALI_DIG_P3 0x92
#define REG_CALI_DIG_P4 0x94
#define REG_CALI_DIG_P5 0x96
#define REG_CALI_DIG_P6 0x98
#define REG_CALI_DIG_P7 0x9A
#define REG_CALI_DIG_P8 0x9C
#define REG_CALI_DIG_P9 0x9E

#define REG_CALI_DIG_H1 0xA1
#define REG_CALI_DIG_H2 0xE1
#define REG_CALI_DIG_H3 0xE3
#define REG_CALI_DIG_H4 0xE4
#define REG_CALI_DIG_H5 0xE5
#define REG_CALI_DIG_H6 0xE7

#define REG_STATUS 0xF3
#define REG_CTRL 0xF4
#define REG_CTRL_HUM 0xF2
#define REG_TEMPERATURE 0xFA
#define REG_PRESSURE 0xF7
#define REG_HUMIDITY 0xFD

#define OVERSAMPLE_X16_TEMPERATURE 0b10100000
#define OVERSAMPLE_X16_PRESSURE 0b00010100
#define OVERSAMPLE_X16_HUMIDITY 0b00000101
#define MODE_SLEEP 0x00
#define MODE_FORCE 0x01
#define MODE_NORMAL 0x03

#define BME280_REGISTER_CHIPID 0xD0



void bme280_init(void);
void bme280_startConvertion(void);

int32_t bme280_readTemperature(void);

uint8_t bme280_status(void);
uint8_t bme280_chipid(void);

uint16_t read16_LE(uint8_t reg);
int16_t readS16_LE(uint8_t reg);

uint32_t read24(uint8_t reg);



#endif

