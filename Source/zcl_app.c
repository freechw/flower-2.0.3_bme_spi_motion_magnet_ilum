
#include "AF.h"
#include "OSAL.h"
#include "OSAL_Clock.h"
#include "OSAL_PwrMgr.h"
#include "ZComDef.h"
#include "ZDApp.h"
#include "ZDNwkMgr.h"
#include "ZDObject.h"
#include "math.h"

#include "nwk_util.h"
#include "zcl.h"
#include "zcl_app.h"
#include "zcl_diagnostic.h"
#include "zcl_general.h"
#include "zcl_lighting.h"
#include "zcl_ms.h"

#include "bdb.h"
#include "bdb_interface.h"
#include "gp_interface.h"

#include "Debug.h"

#include "OnBoard.h"

/* HAL */
//#include "ds18b20.h"
#include "hal_adc.h"
#include "hal_drivers.h"
#include "hal_key.h"
#include "hal_led.h"

#include "bme280spi.h"
#include "battery.h"
#include "commissioning.h"
#include "factory_reset.h"
#include "utils.h"
#include "version.h"


/*********************************************************************
 * MACROS
 */
#define HAL_KEY_P0_EDGE_BITS HAL_KEY_BIT0

#define HAL_KEY_CODE_RELEASE_KEY HAL_KEY_CODE_NOKEY

// use led4 as output pin, osal will shitch it low when go to PM
/*
#define POWER_ON_SENSORS()                                                                                                                 \
    do {                                                                                                                                   \
        HAL_TURN_ON_LED4();                                                                                                                \
        st(T3CTL |= BV(4););                                                                                                               \
        IO_PUD_PORT(OCM_CLK_PORT, IO_PUP);                                                                                                 \
        IO_PUD_PORT(OCM_DATA_PORT, IO_PUP);                                                                                                \
        IO_PUD_PORT(DS18B20_PORT, IO_PUP);                                                                                                 \
    } while (0)
*/   
/*
#define POWER_ON_SENSORS()                                                                                                                 \
    do {                                                                                                                                   \
        HAL_TURN_ON_LED4();                                                                                                                \
    } while (0) 
*/  
/*
#define POWER_OFF_SENSORS()                                                                                                                \
    do {                                                                                                                                   \
        HAL_TURN_OFF_LED4();                                                                                                               \
        st(T3CTL &= ~BV(4); T3CTL |= BV(2););                                                                                              \
        IO_PUD_PORT(OCM_CLK_PORT, IO_PDN);                                                                                                 \
        IO_PUD_PORT(OCM_DATA_PORT, IO_PDN);                                                                                                \
        IO_PUD_PORT(DS18B20_PORT, IO_PDN);                                                                                                 \
    } while (0)
*/
/*
#define POWER_OFF_SENSORS()                                                                                                                \
    do {                                                                                                                                   \
        HAL_TURN_OFF_LED4();                                                                                                               \
    } while (0)
*/
/*********************************************************************
 * CONSTANTS
 */

/*********************************************************************
 * TYPEDEFS
 */

/*********************************************************************
 * GLOBAL VARIABLES
 */

extern bool requestNewTrustCenterLinkKey;
byte zclApp_TaskID;

/*********************************************************************
 * GLOBAL FUNCTIONS
 */

/*********************************************************************
 * LOCAL VARIABLES
 */

static uint8 currentSensorsReadingPhase = 0;

uint8 report = 0;
uint8 power = 0;
bool bmeDetect = 0;
bool LumDetect = 0;
uint8 contDetect = 0;

afAddrType_t inderect_DstAddr = {.addrMode = (afAddrMode_t)AddrNotPresent, .endPoint = 0, .addr.shortAddr = 0};

/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void zclApp_HandleKeys(byte shift, byte keys);
static void zclApp_Report(void);

static void zclApp_BasicResetCB(void);
static void zclApp_RestoreAttributesFromNV(void);
static void zclApp_SaveAttributesToNV(void);

static ZStatus_t zclApp_ReadWriteAuthCB(afAddrType_t *srcAddr, zclAttrRec_t *pAttr, uint8 oper);

static void zclApp_ReadSensors(void);
static void zclApp_ReadBME280(void);
//static void zclApp_ReadDS18B20(void);
static void zclApp_ReadLumosity(void);
//static void zclApp_ReadSoilHumidity(void);
//static void zclApp_InitPWM(void);

/*********************************************************************
 * ZCL General Profile Callback table
 */
static zclGeneral_AppCallbacks_t zclApp_CmdCallbacks = {
    zclApp_BasicResetCB, // Basic Cluster Reset command
    NULL, // Identify Trigger Effect command
    NULL, // On/Off cluster commands
    NULL, // On/Off cluster enhanced command Off with Effect
    NULL, // On/Off cluster enhanced command On with Recall Global Scene
    NULL, // On/Off cluster enhanced command On with Timed Off
    NULL, // RSSI Location command
    NULL  // RSSI Location Response command
};

void zclApp_Init(byte task_id) {
      zclApp_RestoreAttributesFromNV();
      
//    IO_IMODE_PORT_PIN(SOIL_MOISTURE_PORT, SOIL_MOISTURE_PIN, IO_TRI); // tri state p0.4 (soil humidity pin)
    IO_IMODE_PORT_PIN(LUMOISITY_PORT, LUMOISITY_PIN, IO_TRI); // tri state p0.7 (lumosity pin)
    HAL_TURN_ON_LED4(); // p1.1 ON
    zclApp_ReadLumosity();
    HAL_TURN_OFF_LED4(); // p1.1 OFF
    if (zclApp_IlluminanceSensor_MeasuredValue > 1000){
      LumDetect = 1;
    } else {
      IO_IMODE_PORT_PIN(LUMOISITY_PORT, LUMOISITY_PIN, IO_PUD); // Pullup/pulldn input p0.7 (lumosity pin)
      LumDetect = 0;      
    }

    LREP("P0_4 %d\r\n", P0_4);
    contDetect = P0_4;
    zclApp_Magnet_OnOff = contDetect;
    if (contDetect == 1){
      P2INP &= ~HAL_KEY_BIT5; // pull up
      MicroWait(50);
      PICTL |= HAL_KEY_P0_EDGE_BITS; // set falling edge on port
    } else {
      P2INP |= HAL_KEY_BIT5; // pull down
      MicroWait(50);
      PICTL &= ~(HAL_KEY_P0_EDGE_BITS);     
    }
    
//    IO_PUD_PORT(OCM_CLK_PORT, IO_PUP);
//    IO_PUD_PORT(OCM_DATA_PORT, IO_PUP);
//    IO_PUD_PORT(DS18B20_PORT, IO_PUP);
//    POWER_OFF_SENSORS();

    P1SEL &= ~BV(0); // Set P1_0 to GPIO
    P1DIR |= BV(0); // P1_0 output
    P1 |=  BV(0);   // power on DD
    
    
    bmeDetect = BME280Init();
//    zclApp_InitPWM();
    // this is important to allow connects throught routers
    // to make this work, coordinator should be compiled with this flag #define TP2_LEGACY_ZC
    requestNewTrustCenterLinkKey = FALSE;

    zclApp_TaskID = task_id;

    zclGeneral_RegisterCmdCallbacks(1, &zclApp_CmdCallbacks);
    zcl_registerAttrList(zclApp_FirstEP.EndPoint, zclApp_AttrsFirstEPCount, zclApp_AttrsFirstEP);
    bdb_RegisterSimpleDescriptor(&zclApp_FirstEP);

    zcl_registerAttrList(zclApp_SecondEP.EndPoint, zclApp_AttrsSecondEPCount, zclApp_AttrsSecondEP);
    bdb_RegisterSimpleDescriptor(&zclApp_SecondEP);
    
    zcl_registerAttrList(zclApp_ThirdEP.EndPoint, zclApp_AttrsThirdEPCount, zclApp_AttrsThirdEP);
    bdb_RegisterSimpleDescriptor(&zclApp_ThirdEP);
    zcl_registerReadWriteCB(zclApp_ThirdEP.EndPoint, NULL, zclApp_ReadWriteAuthCB);

    zcl_registerForMsg(zclApp_TaskID);

    // Register for all key events - This app will handle all key events
    RegisterForKeys(zclApp_TaskID);
    LREP("Started build %s \r\n", zclApp_DateCodeNT);

    osal_start_reload_timer(zclApp_TaskID, APP_REPORT_EVT, APP_REPORT_DELAY);
    osal_start_reload_timer(zclApp_TaskID, APP_REPORT_MEASURE_EVT, 10000);
}

uint16 zclApp_event_loop(uint8 task_id, uint16 events) {
    afIncomingMSGPacket_t *MSGpkt;

    (void)task_id; // Intentionally unreferenced parameter
    if (events & SYS_EVENT_MSG) {
        while ((MSGpkt = (afIncomingMSGPacket_t *)osal_msg_receive(zclApp_TaskID))) {
            switch (MSGpkt->hdr.event) {
            case KEY_CHANGE:
                zclApp_HandleKeys(((keyChange_t *)MSGpkt)->state, ((keyChange_t *)MSGpkt)->keys);
                
                break;
            case ZCL_INCOMING_MSG:
                if (((zclIncomingMsg_t *)MSGpkt)->attrCmd) {
                    osal_mem_free(((zclIncomingMsg_t *)MSGpkt)->attrCmd);
                }
                break;

            default:
                break;
            }
            // Release the memory
            osal_msg_deallocate((uint8 *)MSGpkt);
        }
        // return unprocessed events
        return (events ^ SYS_EVENT_MSG);
    }
    
    if (events & APP_REPORT_MEASURE_EVT) {
        LREPMaster("APP_REPORT_MEASURE_EVT\r\n");
        report = 0;
        zclApp_Report();   
        return (events ^ APP_REPORT_MEASURE_EVT);
    }
    
    if (events & APP_REPORT_EVT) {
        LREPMaster("APP_REPORT_EVT\r\n");
        report = 1;
        zclApp_Report();
        return (events ^ APP_REPORT_EVT);
    }

    if (events & APP_READ_SENSORS_EVT) {
        LREPMaster("APP_READ_SENSORS_EVT\r\n");
        zclApp_ReadSensors();
        return (events ^ APP_READ_SENSORS_EVT);
    }
        
    if (events & APP_MOTION_ON_EVT) {
        LREPMaster("APP_MOTION_ON_EVT\r\n");
        P1 &= ~BV(0);   // power off motion
        osal_start_timerEx(zclApp_TaskID, APP_MOTION_DELAY_EVT, zclApp_Config.PirOccupiedToUnoccupiedDelay * 1000);
        LREPMaster("START_DELAY\r\n");
        //report
        zclApp_Occupied = 1;
        bdb_RepChangedAttrValue(zclApp_ThirdEP.EndPoint, OCCUPANCY, ATTRID_MS_OCCUPANCY_SENSING_CONFIG_OCCUPANCY);
        
        return (events ^ APP_MOTION_ON_EVT);
    }
    
    if (events & APP_MOTION_OFF_EVT) {
        LREPMaster("APP_MOTION_OFF_EVT\r\n");
        //report    
        zclApp_Occupied = 0;
        bdb_RepChangedAttrValue(zclApp_ThirdEP.EndPoint, OCCUPANCY, ATTRID_MS_OCCUPANCY_SENSING_CONFIG_OCCUPANCY);

        return (events ^ APP_MOTION_OFF_EVT);
    }
    
    if (events & APP_MOTION_DELAY_EVT) {
        LREPMaster("APP_MOTION_DELAY_EVT\r\n");
        power = 2;
        P1 |=  BV(0);   // power on motion
        
        return (events ^ APP_MOTION_DELAY_EVT);
    }
    
    if (events & APP_CONTACT_DELAY_EVT) {
        LREPMaster("APP_CONTACT_DELAY_EVT\r\n");
        bdb_RepChangedAttrValue(zclApp_SecondEP.EndPoint, ONOFF, ATTRID_ON_OFF);

        return (events ^ APP_CONTACT_DELAY_EVT);
    }
    
    if (events & APP_SAVE_ATTRS_EVT) {
        LREPMaster("APP_SAVE_ATTRS_EVT\r\n");
        zclApp_SaveAttributesToNV();
        return (events ^ APP_SAVE_ATTRS_EVT);
    }

    // Discard unknown events
    return 0;
}

static void zclApp_HandleKeys(byte portAndAction, byte keyCode) {
    LREP("zclApp_HandleKeys portAndAction=0x%X keyCode=0x%X\r\n", portAndAction, keyCode);
    zclFactoryResetter_HandleKeys(portAndAction, keyCode);
    zclCommissioning_HandleKeys(portAndAction, keyCode);
    if (portAndAction & HAL_KEY_PRESS) {
        LREPMaster("Key press\r\n");
    }

    bool contact = portAndAction & HAL_KEY_PRESS ? TRUE : FALSE;
    uint8 endPoint = 0;
    if (portAndAction & HAL_KEY_PORT0) {
        LREPMaster("Key press PORT0\r\n");
//        P2INP ^= HAL_KEY_BIT5; // flip pull up/down
        osal_start_timerEx(zclApp_TaskID, APP_CONTACT_DELAY_EVT, 500); //adaptive contact
        HalLedSet(HAL_LED_1, HAL_LED_MODE_BLINK);
        if (contact){
          P2INP &= ~HAL_KEY_BIT5; // pull up
          zclApp_Magnet_OnOff = contact;
        } else {
          P2INP |= HAL_KEY_BIT5; // pull down
          zclApp_Magnet_OnOff = contact;
        }
        
    } else if (portAndAction & HAL_KEY_PORT1) {     
        LREPMaster("Key press PORT1\r\n");
//          P2INP ^= HAL_KEY_BIT6; // flip down/up
        if (!contact) {
          P2INP |= HAL_KEY_BIT6;  // pull down
        } else {
          P2INP &= ~HAL_KEY_BIT6; // pull up
        }
        HalLedSet(HAL_LED_1, HAL_LED_MODE_BLINK);
        if (power == 0){
          if (contact) {
            osal_start_timerEx(zclApp_TaskID, APP_MOTION_ON_EVT, 100);
            osal_start_timerEx(zclApp_TaskID, APP_REPORT_MEASURE_EVT, 100);
          }
        } else {
          if (power == 1){
            osal_start_timerEx(zclApp_TaskID, APP_MOTION_OFF_EVT, 100); //adaptive motion
          }
          power = power - 1;
        }
        LREP("power=%d\r\n", power);
     } else if (portAndAction & HAL_KEY_PORT2) {
          osal_start_timerEx(zclApp_TaskID, APP_REPORT_EVT, 200);
          LREPMaster("Key press PORT2\r\n");
     }
     LREP("contact=%d endpoint=%d\r\n", contact, endPoint);
     uint16 alarmStatus = 0;
     if (!contact) {
        alarmStatus |= BV(0);
     } 
}
/*
static void zclApp_InitPWM(void) {
    PERCFG &= ~(0x20); // Select Timer 3 Alternative 1 location
    P2SEL |= 0x20;
    P2DIR |= 0xC0;  // Give priority to Timer 1 channel2-3
    P1SEL |= BV(4); // Set P1_4 to peripheral, Timer 1,channel 2
    P1DIR |= BV(4);

    T3CTL &= ~BV(4); // Stop timer 3 (if it was running)
    T3CTL |= BV(2);  // Clear timer 3
    T3CTL &= ~0x08;  // Disable Timer 3 overflow interrupts
    T3CTL |= 0x03;   // Timer 3 mode = 3 - Up/Down

    T3CCTL1 &= ~0x40; // Disable channel 0 interrupts
    T3CCTL1 |= BV(2); // Ch0 mode = compare
    T3CCTL1 |= BV(4); // Ch0 output compare mode = toggle on compare

    T3CTL &= ~(BV(7) | BV(6) | BV(5)); // Clear Prescaler divider value
    T3CC0 = 4;                         // Set ticks
}
*/
static void zclApp_ReadSensors(void) {
    LREP("currentSensorsReadingPhase %d\r\n", currentSensorsReadingPhase);
    /**
     * FYI: split reading sensors into phases, so single call wouldn't block processor
     * for extensive ammount of time
     * */
    switch (currentSensorsReadingPhase++) {
    case 0:
      if (report == 1){
        HalLedSet(HAL_LED_1, HAL_LED_MODE_BLINK);
      }
        HAL_TURN_ON_LED4(); // p1.1 ON
        if (LumDetect == 1){
          zclApp_ReadLumosity();
        }
        break;
    case 1:
      if (report == 1){
        zclBattery_Report();
      } 
        break;
    case 2:
        if (bmeDetect == 1){
          zclApp_ReadBME280();
        }
        break;
    default:
        HAL_TURN_OFF_LED4(); // p1.1 OFF
        osal_stop_timerEx(zclApp_TaskID, APP_READ_SENSORS_EVT);
        osal_clear_event(zclApp_TaskID, APP_READ_SENSORS_EVT);
        currentSensorsReadingPhase = 0;
        break;
    }
}

uint16 temp_SoilHumiditySensor_MeasuredValue;
/*
static void zclApp_ReadSoilHumidity(void) {
    zclApp_SoilHumiditySensor_MeasuredValueRawAdc = adcReadSampled(SOIL_MOISTURE_PIN, HAL_ADC_RESOLUTION_14, HAL_ADC_REF_AVDD, 5);
    // FYI: https://docs.google.com/spreadsheets/d/1qrFdMTo0ZrqtlGUoafeB3hplhU3GzDnVWuUK4M9OgNo/edit?usp=sharing
    uint16 soilHumidityMinRangeAir = (uint16)AIR_COMPENSATION_FORMULA(zclBattery_RawAdc);
    uint16 soilHumidityMaxRangeWater = (uint16)WATER_COMPENSATION_FORMULA(zclBattery_RawAdc);
    LREP("soilHumidityMinRangeAir=%d soilHumidityMaxRangeWater=%d\r\n", soilHumidityMinRangeAir, soilHumidityMaxRangeWater);
    zclApp_SoilHumiditySensor_MeasuredValue = 
        (uint16)mapRange(soilHumidityMinRangeAir, soilHumidityMaxRangeWater, 0.0, 10000.0, zclApp_SoilHumiditySensor_MeasuredValueRawAdc);
    LREP("ReadSoilHumidity raw=%d mapped=%d\r\n", zclApp_SoilHumiditySensor_MeasuredValueRawAdc, zclApp_SoilHumiditySensor_MeasuredValue);
    uint8 procent = 0;
    if (temp_SoilHumiditySensor_MeasuredValue > zclApp_SoilHumiditySensor_MeasuredValue){
      procent = (temp_SoilHumiditySensor_MeasuredValue - zclApp_SoilHumiditySensor_MeasuredValue)/100;
    } else {
      procent = (zclApp_SoilHumiditySensor_MeasuredValue - temp_SoilHumiditySensor_MeasuredValue)/100;
    }
    if (procent > 5 || report == 1){
      temp_SoilHumiditySensor_MeasuredValue = zclApp_SoilHumiditySensor_MeasuredValue;
      bdb_RepChangedAttrValue(zclApp_FirstEP.EndPoint, SOIL_HUMIDITY, ATTRID_MS_RELATIVE_HUMIDITY_MEASURED_VALUE);
    }
}
*/
uint16 temp_DS18B20_MeasuredValue;
/*
static void zclApp_ReadDS18B20(void) {
//    int16 temp = readTemperature();
  int16 temp = 0;
  if (temp != 1) {
        zclApp_DS18B20_MeasuredValue = temp;
        LREP("ReadDS18B20 t=%d\r\n", zclApp_DS18B20_MeasuredValue);
        uint8 grad = 0;
        if (temp_DS18B20_MeasuredValue > zclApp_DS18B20_MeasuredValue){
          grad = (temp_DS18B20_MeasuredValue - zclApp_DS18B20_MeasuredValue)/100;
        } else {
          grad = (zclApp_DS18B20_MeasuredValue - temp_DS18B20_MeasuredValue)/100;
        }
      if (grad > 1 || report == 1){
        temp_DS18B20_MeasuredValue = zclApp_DS18B20_MeasuredValue;
        bdb_RepChangedAttrValue(zclApp_SecondEP.EndPoint, TEMP, ATTRID_MS_TEMPERATURE_MEASURED_VALUE);
      }
    } else {
        LREPMaster("ReadDS18B20 error\r\n");
    }
}
*/
uint16 temp_IlluminanceSensor_MeasuredValue;

static void zclApp_ReadLumosity(void) {
    zclApp_IlluminanceSensor_MeasuredValueRawAdc = adcReadSampled(LUMOISITY_PIN, HAL_ADC_RESOLUTION_14, HAL_ADC_REF_AVDD, 5);
    zclApp_IlluminanceSensor_MeasuredValue = zclApp_IlluminanceSensor_MeasuredValueRawAdc;
    uint16 illum = 0;
    if (temp_IlluminanceSensor_MeasuredValue > zclApp_IlluminanceSensor_MeasuredValue){
      illum = (temp_IlluminanceSensor_MeasuredValue - zclApp_IlluminanceSensor_MeasuredValue);
    } else {
      illum = (zclApp_IlluminanceSensor_MeasuredValue - temp_IlluminanceSensor_MeasuredValue);
    }
    if (illum > 1000 || report == 1){
      temp_IlluminanceSensor_MeasuredValue = zclApp_IlluminanceSensor_MeasuredValue;
      bdb_RepChangedAttrValue(zclApp_FirstEP.EndPoint, ILLUMINANCE, ATTRID_MS_ILLUMINANCE_MEASURED_VALUE);
    }
    LREP("IlluminanceSensor_MeasuredValue value=%d\r\n", zclApp_IlluminanceSensor_MeasuredValue);
}

void user_delay_ms(uint32_t period) { MicroWait(period * 1000); }

uint16 temp_Temperature_Sensor_MeasuredValue;
uint16 temp_PressureSensor_MeasuredValue;
uint16 temp_HumiditySensor_MeasuredValue;

static void zclApp_ReadBME280(void) {
  
    bme280_takeForcedMeasurement();
    uint8 chip = bme280_read8(BME280_REGISTER_CHIPID);
    LREP("BME280_REGISTER_CHIPID=%d\r\n", chip);;
    if (chip == 0x60) {
//        zclApp_Temperature_Sensor_MeasuredValue = (int16)bme_results.temperature;
        zclApp_Temperature_Sensor_MeasuredValue = (int16)(bme280_readTemperature() *100);
        LREP("Temperature=%d\r\n", zclApp_Temperature_Sensor_MeasuredValue);
        
//        zclApp_PressureSensor_ScaledValue = (int16) (pow(10.0, (double) zclApp_PressureSensor_Scale) * (double) bme_results.pressure);
        zclApp_PressureSensor_ScaledValue = (int16) (pow(10.0, (double) zclApp_PressureSensor_Scale) * (double) bme280_readPressure()* 100);
//        zclApp_PressureSensor_MeasuredValue = bme_results.pressure / 100;
        zclApp_PressureSensor_MeasuredValue = (uint16)bme280_readPressure();
        LREP("Pressure=%d\r\n", zclApp_PressureSensor_MeasuredValue);
        
//        LREP("ReadBME280 t=%ld, p=%ld h=%ld\r\n", bme_results.temperature, bme_results.pressure, bme_results.humidity);
//        zclApp_HumiditySensor_MeasuredValue = (uint16)(bme_results.humidity * 100 / 1024);
        zclApp_HumiditySensor_MeasuredValue = (uint16)(bme280_readHumidity() * 100);
        LREP("Humidity=%d\r\n", zclApp_HumiditySensor_MeasuredValue);
        
        uint16 temp = 0;
        if (temp_Temperature_Sensor_MeasuredValue > zclApp_Temperature_Sensor_MeasuredValue){
          temp = (temp_Temperature_Sensor_MeasuredValue - zclApp_Temperature_Sensor_MeasuredValue);
        } else {
          temp = (zclApp_Temperature_Sensor_MeasuredValue - temp_Temperature_Sensor_MeasuredValue);
        }
        if (temp > 50 || report == 1){ //50 - 0.5 
          temp_Temperature_Sensor_MeasuredValue = zclApp_Temperature_Sensor_MeasuredValue;
          bdb_RepChangedAttrValue(zclApp_FirstEP.EndPoint, TEMP, ATTRID_MS_TEMPERATURE_MEASURED_VALUE);
        }
        
        uint16 press = 0;
        if (temp_PressureSensor_MeasuredValue > zclApp_PressureSensor_MeasuredValue){
          press = (temp_PressureSensor_MeasuredValue - zclApp_PressureSensor_MeasuredValue);
        } else {
          press = (zclApp_PressureSensor_MeasuredValue - temp_PressureSensor_MeasuredValue);
        }
        if (press > 1 || report == 1){ //1gPa
          temp_PressureSensor_MeasuredValue = zclApp_PressureSensor_MeasuredValue; 
          bdb_RepChangedAttrValue(zclApp_FirstEP.EndPoint, PRESSURE, ATTRID_MS_PRESSURE_MEASUREMENT_MEASURED_VALUE);
        }
        
        uint16 humid = 0;
        if (temp_HumiditySensor_MeasuredValue > zclApp_HumiditySensor_MeasuredValue){
          humid = (temp_HumiditySensor_MeasuredValue - zclApp_HumiditySensor_MeasuredValue);
        } else {
          humid = (zclApp_HumiditySensor_MeasuredValue - temp_HumiditySensor_MeasuredValue);
        }
        if (humid > 1000 || report == 1){ //10%
          temp_HumiditySensor_MeasuredValue = zclApp_HumiditySensor_MeasuredValue;
          bdb_RepChangedAttrValue(zclApp_FirstEP.EndPoint, HUMIDITY, ATTRID_MS_RELATIVE_HUMIDITY_MEASURED_VALUE);
        }
    } else {
        LREPMaster("NOT BME280\r\n");
    }
}
static void zclApp_Report(void) { osal_start_reload_timer(zclApp_TaskID, APP_READ_SENSORS_EVT, 100); }

static void zclApp_BasicResetCB(void) {
    LREPMaster("BasicResetCB\r\n");
    zclApp_ResetAttributesToDefaultValues();
    zclApp_SaveAttributesToNV();
}

static ZStatus_t zclApp_ReadWriteAuthCB(afAddrType_t *srcAddr, zclAttrRec_t *pAttr, uint8 oper) {
    LREPMaster("AUTH CB called\r\n");

    osal_start_timerEx(zclApp_TaskID, APP_SAVE_ATTRS_EVT, 2000);
    return ZSuccess;
}

static void zclApp_SaveAttributesToNV(void) {
    uint8 writeStatus = osal_nv_write(NW_APP_CONFIG, 0, sizeof(application_config_t), &zclApp_Config);
    LREP("Saving attributes to NV write=%d\r\n", writeStatus);
}

static void zclApp_RestoreAttributesFromNV(void) {
    uint8 status = osal_nv_item_init(NW_APP_CONFIG, sizeof(application_config_t), NULL);
    LREP("Restoring attributes from NV  status=%d \r\n", status);
    if (status == NV_ITEM_UNINIT) {
        uint8 writeStatus = osal_nv_write(NW_APP_CONFIG, 0, sizeof(application_config_t), &zclApp_Config);
        LREP("NV was empty, writing %d\r\n", writeStatus);
    }
    if (status == ZSUCCESS) {
        LREPMaster("Reading from NV\r\n");
        osal_nv_read(NW_APP_CONFIG, 0, sizeof(application_config_t), &zclApp_Config);
    }
}

/****************************************************************************
****************************************************************************/
