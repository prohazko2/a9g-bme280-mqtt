#include "stdbool.h"
#include "stdint.h"
#include "stdio.h"
#include "string.h"

#include "api_os.h"
#include "api_sys.h"
#include "api_debug.h"
#include "api_event.h"
#include "api_mqtt.h"
#include "api_network.h"
#include "api_socket.h"
#include "api_info.h"
#include "demo_mqtt.h"

#include "api_hal_gpio.h"
#include "api_hal_pm.h"

#include "api_hal_i2c.h"

#include "time.h"

#include "cJSON.h"

#define PDP_CONTEXT_APN "internet"
#define PDP_CONTEXT_USERNAME ""
#define PDP_CONTEXT_PASSWD ""

#define MAIN_TASK_STACK_SIZE (2048 * 2)
#define MAIN_TASK_PRIORITY 0
#define MAIN_TASK_NAME "Main Test Task"

#define SECOND_TASK_STACK_SIZE (2048 * 2)
#define SECOND_TASK_PRIORITY 0
#define SECOND_TASK_NAME "MQTT Test Task"

char willMsg[50] = "GPRS 123456789012345 disconnected!";
uint8_t imei[16] = "";

static int32_t t_fine;

#define CLOCKS_PER_USEC (0.016384)

static HANDLE mainTaskHandle = NULL;
static HANDLE secondTaskHandle = NULL;

static HANDLE semMqttStart = NULL;
MQTT_Connect_Info_t ci;

// leds
static GPIO_LEVEL ledBlueLevel = GPIO_LEVEL_LOW;
GPIO_config_t gpioLedBlue = {
    .mode = GPIO_MODE_OUTPUT,
    .pin = GPIO_PIN27,
    .defaultLevel = GPIO_LEVEL_LOW};

static GPIO_LEVEL ledBlue2Level = GPIO_LEVEL_LOW;
GPIO_config_t gpioLedBlue2 = {
    .mode = GPIO_MODE_OUTPUT,
    .pin = GPIO_PIN28,
    .defaultLevel = GPIO_LEVEL_LOW};

GPIO_config_t gpioInput = {
    .mode = GPIO_MODE_INPUT,
    .pin = GPIO_PIN30,
    .defaultLevel = GPIO_LEVEL_HIGH,
    .intConfig.debounce = 0,
    .intConfig.type = GPIO_INT_TYPE_MAX,
    .intConfig.callback = NULL};

#define MAX_TIMINGS 85
#define MAX_COUNTER 5000

#define MAXdhtData 5 // to complete 40 = 5*8 Bits

enum dht11_status
{
    DHT11_CRC_ERROR = -2,
    DHT11_TIMEOUT_ERROR,
    DHT11_OK
};

typedef enum
{
    MQTT_EVENT_CONNECTED = 0,
    MQTT_EVENT_DISCONNECTED,
    MQTT_EVENT_INIT_REQ,
    MQTT_EVENT_READ_REQ,
    MQTT_EVENT_MAX
} MQTT_Event_ID_t;

typedef struct
{
    MQTT_Event_ID_t id;
    MQTT_Client_t *client;
} MQTT_Event_t;

typedef enum
{
    MQTT_STATUS_DISCONNECTED = 0,
    MQTT_STATUS_CONNECTED,
    MQTT_STATUS_MAX
} MQTT_Status_t;

MQTT_Status_t mqttStatus = MQTT_STATUS_DISCONNECTED;

bool AttachActivate()
{

    uint8_t status;
    bool ret = Network_GetAttachStatus(&status);
    if (!ret)
    {
        Trace(2, "get attach staus fail");
        return false;
    }
    Trace(2, "attach status:%d", status);
    if (!status)
    {
        ret = Network_StartAttach();
        if (!ret)
        {
            Trace(2, "network attach fail");
            return false;
        }
    }
    else
    {
        ret = Network_GetActiveStatus(&status);
        if (!ret)
        {
            Trace(2, "get activate staus fail");
            return false;
        }
        Trace(2, "activate status:%d", status);
        if (!status)
        {
            Network_PDP_Context_t context = {
                .apn = PDP_CONTEXT_APN,
                .userName = PDP_CONTEXT_USERNAME,
                .userPasswd = PDP_CONTEXT_PASSWD};
            Network_StartActive(context);
        }
    }
    return true;
}

static void NetworkEventDispatch(API_Event_t *pEvent)
{
    switch (pEvent->id)
    {
    case API_EVENT_ID_NETWORK_REGISTER_DENIED:
        Trace(2, "network register denied");
        break;

    case API_EVENT_ID_NETWORK_REGISTER_NO:
        Trace(2, "network register no");
        break;

    case API_EVENT_ID_NETWORK_REGISTERED_HOME:
    case API_EVENT_ID_NETWORK_REGISTERED_ROAMING:
        Trace(2, "network register success");
        AttachActivate();
        break;

    case API_EVENT_ID_NETWORK_DETACHED:
        Trace(2, "network detached");
        AttachActivate();
        break;
    case API_EVENT_ID_NETWORK_ATTACH_FAILED:
        Trace(2, "network attach failed");
        AttachActivate();
        break;

    case API_EVENT_ID_NETWORK_ATTACHED:
        Trace(2, "network attach success");
        AttachActivate();
        break;

    case API_EVENT_ID_NETWORK_DEACTIVED:
        Trace(2, "network deactived");
        AttachActivate();
        break;

    case API_EVENT_ID_NETWORK_ACTIVATE_FAILED:
        Trace(2, "network activate failed");
        AttachActivate();
        break;

    case API_EVENT_ID_NETWORK_ACTIVATED:
        Trace(2, "network activate success..");
        if (semMqttStart)
            OS_ReleaseSemaphore(semMqttStart);
        break;

    case API_EVENT_ID_SIGNAL_QUALITY:
        Trace(2, "CSQ:%d", pEvent->param1);
        break;

    default:
        break;
    }
}

static void EventDispatch(API_Event_t *pEvent)
{
    switch (pEvent->id)
    {
    case API_EVENT_ID_NO_SIMCARD:
        Trace(2, "!!NO SIM CARD%d!!!!", pEvent->param1);
        break;
    case API_EVENT_ID_SIMCARD_DROP:
        Trace(2, "!!SIM CARD%d DROP!!!!", pEvent->param1);
        break;
    case API_EVENT_ID_SYSTEM_READY:
        Trace(2, "system initialize complete");
        break;
    case API_EVENT_ID_NETWORK_REGISTER_DENIED:
    case API_EVENT_ID_NETWORK_REGISTER_NO:
    case API_EVENT_ID_NETWORK_REGISTERED_HOME:
    case API_EVENT_ID_NETWORK_REGISTERED_ROAMING:
    case API_EVENT_ID_NETWORK_DETACHED:
    case API_EVENT_ID_NETWORK_ATTACH_FAILED:
    case API_EVENT_ID_NETWORK_ATTACHED:
    case API_EVENT_ID_NETWORK_DEACTIVED:
    case API_EVENT_ID_NETWORK_ACTIVATE_FAILED:
    case API_EVENT_ID_NETWORK_ACTIVATED:
    case API_EVENT_ID_SIGNAL_QUALITY:
        NetworkEventDispatch(pEvent);
        break;

    default:
        break;
    }
}

void OnMqttReceived(void *arg, const char *topic, uint32_t payloadLen)
{
    Trace(1, "MQTT received publish data request, topic:%s, payload length:%d", topic, payloadLen);

    MQTT_Client_t *client = (MQTT_Client_t *)arg;


    if (strcmp(topic, "base/relay/led0") == 0)
    {
        GPIO_SetLevel(gpioLedBlue2, GPIO_LEVEL_LOW);
    }
    if (strcmp(topic, "base/relay/led1") == 0)
    {
        GPIO_SetLevel(gpioLedBlue2, GPIO_LEVEL_HIGH);
    }

    if (strcmp(topic, "dht/start") == 0)
    {

        MQTT_Event_t *event = (MQTT_Event_t *)OS_Malloc(sizeof(MQTT_Event_t));

        event->id = MQTT_EVENT_INIT_REQ;
        event->client = client;
        OS_SendEvent(secondTaskHandle, event, OS_TIME_OUT_WAIT_FOREVER, OS_EVENT_PRI_NORMAL);
    }

    if (strcmp(topic, "dht/read") == 0)
    {

        MQTT_Event_t *event = (MQTT_Event_t *)OS_Malloc(sizeof(MQTT_Event_t));

        event->id = MQTT_EVENT_READ_REQ;
        event->client = client;
        OS_SendEvent(secondTaskHandle, event, OS_TIME_OUT_WAIT_FOREVER, OS_EVENT_PRI_NORMAL);
    }
}

void OnMqttReceiedData(void *arg, const uint8_t *data, uint16_t len, MQTT_Flags_t flags)
{
    Trace(1, "MQTT recieved publish data,  length:%d,data:%s", len, data);
    if (flags == MQTT_FLAG_DATA_LAST)
        Trace(1, "MQTT data is last frame");
}

void OnMqttSubscribed(void *arg, MQTT_Error_t err)
{
    if (err != MQTT_ERROR_NONE)
        Trace(1, "MQTT subscribe fail,error code:%d", err);
    else
        Trace(1, "MQTT subscribe success,topic:%s", (const char *)arg);
}

void OnMqttConnection(MQTT_Client_t *client, void *arg, MQTT_Connection_Status_t status)
{

    Trace(1, "MQTT connection status:%d", status);
    MQTT_Event_t *event = (MQTT_Event_t *)OS_Malloc(sizeof(MQTT_Event_t));
    if (!event)
    {
        Trace(1, "MQTT no memory");
        return;
    }
    if (status == MQTT_CONNECTION_ACCEPTED)
    {
        Trace(1, "MQTT succeed connect to broker");
        //!!! DO NOT suscribe here(interrupt function), do MQTT suscribe in task, or it will not excute
        event->id = MQTT_EVENT_CONNECTED;
        event->client = client;
        OS_SendEvent(secondTaskHandle, event, OS_TIME_OUT_WAIT_FOREVER, OS_EVENT_PRI_NORMAL);
    }
    else
    {
        event->id = MQTT_EVENT_DISCONNECTED;
        event->client = client;
        OS_SendEvent(secondTaskHandle, event, OS_TIME_OUT_WAIT_FOREVER, OS_EVENT_PRI_NORMAL);
        Trace(1, "MQTT connect to broker fail,error code:%d", status);
    }
    Trace(1, "MQTT OnMqttConnection() end");
}

static uint32_t reconnectInterval = 3000;
void StartTimerPublish(uint32_t interval, MQTT_Client_t *client);
void StartTimerConnect(uint32_t interval, MQTT_Client_t *client);
void OnPublish(void *arg, MQTT_Error_t err)
{
    if (err == MQTT_ERROR_NONE)
        Trace(1, "MQTT publish success");
    else
        Trace(1, "MQTT publish error, error code:%d", err);
}

void OnTimerPublish(void *param)
{
    MQTT_Error_t err;
    MQTT_Client_t *client = (MQTT_Client_t *)param;
    uint8_t status = MQTT_IsConnected(client);
    Trace(1, "mqtt status:%d", status);
    if (mqttStatus != MQTT_STATUS_CONNECTED)
    {
        Trace(1, "MQTT not connected to broker! can not publish");
        return;
    }

    SendTemp(client);

    StartTimerPublish(PUBLISH_INTERVAL, client);
}

void StartTimerPublish(uint32_t interval, MQTT_Client_t *client)
{
    OS_StartCallbackTimer(mainTaskHandle, interval, OnTimerPublish, (void *)client);
}


void InitTemp(MQTT_Client_t *client)
{
    cJSON *root = NULL;

    root = cJSON_CreateObject();

    bme280_init();

    cJSON_AddNumberToObject(root, "status", bme280_status());

    char *json = cJSON_Print(root);

    MQTT_Publish(client, "dht11/init", json, strlen(json), 1, 2, 0, OnPublish, NULL);

    free(json);
    cJSON_Delete(root);

}

void SendTemp(MQTT_Client_t *client)
{
    cJSON *root = NULL;

    root = cJSON_CreateObject();

    cJSON_AddNumberToObject(root, "sensor", bme280_chipid());
    cJSON_AddNumberToObject(root, "status", bme280_status());

    int32_t adc_T = read24(REG_TEMPERATURE);

    cJSON_AddNumberToObject(root, "adc_T_0", adc_T);

    uint16_t dig_T1 = read16_LE(REG_CALI_DIG_T1);
    int16_t dig_T2 = readS16_LE(REG_CALI_DIG_T2);
    int16_t dig_T3 = readS16_LE(REG_CALI_DIG_T3);

    adc_T >>= 4;

    int32_t var1 = ((((adc_T >> 3) - ((int32_t)dig_T1 << 1))) * ((int32_t)dig_T2)) >> 11;
    int32_t var2 = (((((adc_T >> 4) - ((int32_t)dig_T1)) * ((adc_T >> 4) - ((int32_t)dig_T1))) >> 12) * ((int32_t)dig_T3)) >> 14;

    t_fine = var1 + var2;

    int32_t T = (t_fine * 5 + 128) >> 8;

    cJSON_AddNumberToObject(root, "c", T / 100.0);

    char *json = cJSON_Print(root);

    MQTT_Publish(client, "dht11/raw", json, strlen(json), 1, 2, 0, OnPublish, NULL);

    free(json);
    cJSON_Delete(root);
}

void OnTimerStartConnect(void *param)
{
    MQTT_Error_t err;
    MQTT_Client_t *client = (MQTT_Client_t *)param;
    uint8_t status = MQTT_IsConnected(client);
    Trace(1, "mqtt status:%d", status);
    if (mqttStatus == MQTT_STATUS_CONNECTED)
    {
        Trace(1, "already connected!");
        return;
    }
    err = MQTT_Connect(client, BROKER_IP, BROKER_PORT, OnMqttConnection, NULL, &ci);
    if (err != MQTT_ERROR_NONE)
    {
        Trace(1, "MQTT connect fail,error code:%d", err);
        reconnectInterval += 1000;
        if (reconnectInterval >= 60000)
            reconnectInterval = 60000;
            
        StartTimerConnect(reconnectInterval, client);
    }
}

void StartTimerConnect(uint32_t interval, MQTT_Client_t *client)
{
    OS_StartCallbackTimer(mainTaskHandle, interval, OnTimerStartConnect, (void *)client);
}

void SecondTaskEventDispatch(MQTT_Event_t *pEvent)
{
    switch (pEvent->id)
    {
    case MQTT_EVENT_CONNECTED:
        reconnectInterval = 3000;
        mqttStatus = MQTT_STATUS_CONNECTED;

        GPIO_SetLevel(gpioLedBlue, GPIO_LEVEL_HIGH);

        Trace(1, "MQTT connected, now subscribe topic:%s", SUBSCRIBE_TOPIC);

        MQTT_Error_t err;
        MQTT_SetInPubCallback(pEvent->client, OnMqttReceived, OnMqttReceiedData, (void *)pEvent->client);
        err = MQTT_Subscribe(pEvent->client, SUBSCRIBE_TOPIC, 2, OnMqttSubscribed, (void *)SUBSCRIBE_TOPIC);
        if (err != MQTT_ERROR_NONE)
            Trace(1, "MQTT subscribe error, error code:%d", err);

        StartTimerPublish(PUBLISH_INTERVAL, pEvent->client);
        SendTemp(pEvent->client);

        break;
    case MQTT_EVENT_DISCONNECTED:
        mqttStatus = MQTT_STATUS_DISCONNECTED;
        GPIO_SetLevel(gpioLedBlue, GPIO_LEVEL_LOW);

        StartTimerConnect(reconnectInterval, pEvent->client);
        break;
    case MQTT_EVENT_INIT_REQ:
        InitTemp(pEvent->client);
        break;
    case MQTT_EVENT_READ_REQ:
        SendTemp(pEvent->client);
        break;
    default:
        break;
    }
}

void SecondTask(void *pData)
{
    MQTT_Event_t *event = NULL;

    semMqttStart = OS_CreateSemaphore(0);
    OS_WaitForSemaphore(semMqttStart, OS_WAIT_FOREVER);
    OS_DeleteSemaphore(semMqttStart);
    semMqttStart = NULL;

    Trace(1, "start mqtt test");

    INFO_GetIMEI(imei);
    Trace(1, "IMEI:%s", imei);

    MQTT_Client_t *client = MQTT_ClientNew();

    MQTT_Error_t err;
    memset(&ci, 0, sizeof(MQTT_Connect_Info_t));
    ci.client_id = CLIENT_ID;
    ci.client_user = CLIENT_USER;
    ci.client_pass = CLIENT_PASS;
    ci.keep_alive = 6 * 60;
    ci.clean_session = 1;
    ci.use_ssl = false;
    ci.will_qos = 2;
    ci.will_topic = "will";
    ci.will_retain = 1;
    memcpy(strstr(willMsg, "GPRS") + 5, imei, 15);
    ci.will_msg = willMsg;

    err = MQTT_Connect(client, BROKER_IP, BROKER_PORT, OnMqttConnection, NULL, &ci);
    if (err != MQTT_ERROR_NONE)
        Trace(1, "MQTT connect fail,error code:%d", err);

    for (uint8_t i = 0; i < POWER_TYPE_MAX; ++i)
        PM_PowerEnable(i, true);

    GPIO_Init(gpioLedBlue);
    GPIO_Init(gpioLedBlue2);

    //GPIO_Init(gpioInput);
    //bme280_init();

    PM_PowerEnable(POWER_TYPE_CAM, true);
    I2C_Config_t i2cConfig;
    i2cConfig.freq = I2C_FREQ_100K;
    I2C_Init(I2C2, i2cConfig);

    bme280_init();

    while (1)
    {
        if (OS_WaitEvent(secondTaskHandle, (void **)&event, OS_TIME_OUT_WAIT_FOREVER))
        {
            SecondTaskEventDispatch(event);
            OS_Free(event);
        }
    }
}

void MainTask(void *pData)
{
    API_Event_t *event = NULL;

    secondTaskHandle = OS_CreateTask(SecondTask,
                                     NULL, NULL, SECOND_TASK_STACK_SIZE, SECOND_TASK_PRIORITY, 0, 0, SECOND_TASK_NAME);

    while (1)
    {
        if (OS_WaitEvent(mainTaskHandle, (void **)&event, OS_TIME_OUT_WAIT_FOREVER))
        {
            EventDispatch(event);
            OS_Free(event->pParam1);
            OS_Free(event->pParam2);
            OS_Free(event);
        }
    }
}

void mqtt_Main(void)
{
    mainTaskHandle = OS_CreateTask(MainTask,
                                   NULL, NULL, MAIN_TASK_STACK_SIZE, MAIN_TASK_PRIORITY, 0, 0, MAIN_TASK_NAME);
    OS_SetUserMainHandle(&mainTaskHandle);
}
