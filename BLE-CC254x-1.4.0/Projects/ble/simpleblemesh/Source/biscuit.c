/*

Copyright (c) 2013 RedBearLab

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal 
in the Software without restriction, including without limitation the rights 
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/

/*********************************************************************
* INCLUDES
*/

#include "bcomdef.h"
#include "OSAL.h"
#include "OSAL_PwrMgr.h"

#include "OnBoard.h"
#include "hal_adc.h"
#include "hal_led.h"
#include "hal_key.h"
#include "hal_lcd.h"

#include "gatt.h"

#include "hci.h"

#include "gapgattserver.h"
#include "gattservapp.h"
#include "devinfoservice.h"

#include "peripheralObserverProfile.h"

#include "gapbondmgr.h"

#include "biscuit.h"
#include "txrxservice.h"
#include "npi.h"

#include "i2c.h"
#include "eeprom.h"
#include "string.h"

#include "print_uart.h"
/*********************************************************************
* MACROS
*/

/*********************************************************************
* CONSTANTS
*/

// How often to perform periodic event
#define SBP_PERIODIC_EVT_PERIOD                   11000

// What is the advertising interval when device is discoverable (units of 625us, 160=100ms)
#define DEFAULT_ADVERTISING_INTERVAL          160

// Limited discoverable mode advertises for 30.72s, and then stops
// General discoverable mode advertises indefinitely

#if defined ( CC2540_MINIDK )
#define DEFAULT_DISCOVERABLE_MODE             GAP_ADTYPE_FLAGS_LIMITED
#else
#define DEFAULT_DISCOVERABLE_MODE             GAP_ADTYPE_FLAGS_GENERAL
#endif  // defined ( CC2540_MINIDK )

// Minimum connection interval (units of 1.25ms, 80=100ms) if automatic parameter update request is enabled
#define DEFAULT_DESIRED_MIN_CONN_INTERVAL     80

// Maximum connection interval (units of 1.25ms, 800=1000ms) if automatic parameter update request is enabled
#define DEFAULT_DESIRED_MAX_CONN_INTERVAL     800

// Slave latency to use if automatic parameter update request is enabled
#define DEFAULT_DESIRED_SLAVE_LATENCY         0

// Supervision timeout value (units of 10ms, 1000=10s) if automatic parameter update request is enabled
#define DEFAULT_DESIRED_CONN_TIMEOUT          1000

// Whether to enable automatic parameter update request when a connection is formed
#define DEFAULT_ENABLE_UPDATE_REQUEST         FALSE;

// Connection Pause Peripheral time value (in seconds)
#define DEFAULT_CONN_PAUSE_PERIPHERAL         6

#define INVALID_CONNHANDLE                    0xFFFF

// Length of bd addr as a string
#define B_ADDR_STR_LEN                        15        

#define MAX_RX_LEN                            128
#define SBP_RX_TIME_OUT                       5

#define DEFAULT_SCAN_DURATION                 10000

// Discovey mode (limited, general, all)
#define DEFAULT_DISCOVERY_MODE                DEVDISC_MODE_ALL

// TRUE to use active scan
#define DEFAULT_DISCOVERY_ACTIVE_SCAN         FALSE

// TRUE to use white list during discovery
#define DEFAULT_DISCOVERY_WHITE_LIST          FALSE

// Maximum number of scan responses
#define DEFAULT_MAX_SCAN_RES                  8



/*********************************************************************
* TYPEDEFS
*/

/*********************************************************************
* GLOBAL VARIABLES
*/

/*********************************************************************
* EXTERNAL VARIABLES
*/

/*********************************************************************
* EXTERNAL FUNCTIONS
*/ 

/*********************************************************************
* LOCAL VARIABLES
*/
static uint8 biscuit_TaskID;   // Task ID for internal task/event processing

static gaprole_States_t gapProfileState = GAPROLE_INIT;

static uint8 RXBuf[MAX_RX_LEN];
static uint8 rxLen = 0;
static uint8 rxHead = 0, rxTail = 0;

// GAP - SCAN RSP data (max size = 31 bytes)
static uint8 scanRspData[] =
{
  // Tx power level
  //0x02,   // length of this data
  //GAP_ADTYPE_POWER_LEVEL,
  //0,       // 0dBm
  
  // service UUID, to notify central devices what services are included
  // in this peripheral
  17,   // length of this data
  GAP_ADTYPE_128BIT_COMPLETE,      // some of the UUID's, but not all
  TXRX_SERV_UUID,
  
};

// GAP - Advertisement data (max size = 31 bytes, though this is
// best kept short to conserve power while advertisting)
static uint8 advertData[31] =
{
  // Flags; this sets the device to use limited discoverable
  // mode (advertises for 30 seconds at a time) instead of general
  // discoverable mode (advertises indefinitely)
  0x02,   // length of this data
  GAP_ADTYPE_FLAGS,
  DEFAULT_DISCOVERABLE_MODE | GAP_ADTYPE_FLAGS_BREDR_NOT_SUPPORTED,
  
  // complete name 
  9,   // length of this data
  GAP_ADTYPE_LOCAL_NAME_COMPLETE,
  'B','L','E',' ','M','i','n','i',
  
};              

// GAP GATT Attributes
static uint8 attDeviceName[GAP_DEVICE_NAME_LEN] = "BLE Mini";


// Number of scan results and scan result index
static uint8 simpleBLEScanRes;
static uint8 simpleBLEScanIdx;

// Scan result list
static gapDevRec_t simpleBLEDevList[DEFAULT_MAX_SCAN_RES];

// Scanning state
static uint8 simpleBLEScanning = FALSE;

/*********************************************************************
* LOCAL FUNCTIONS
*/
static void biscuit_ProcessOSALMsg( osal_event_hdr_t *pMsg );
static void peripheralStateNotificationCB( gaprole_States_t newState );
static void performPeriodicTask( void );
static void txrxServiceChangeCB( uint8 paramID );
static void simpleBLEObserverEventCB( observerRoleEvent_t *pEvent );


#if defined( CC2540_MINIDK )
static void biscuit_HandleKeys( uint8 shift, uint8 keys );
#endif

static void dataHandler( uint8 port, uint8 events );

/*********************************************************************
* PROFILE CALLBACKS
*/

// GAP Role Callbacks
static gapRolesCBs_t biscuit_PeripheralCBs =
{
  peripheralStateNotificationCB,  // Profile State Change Callbacks
  NULL,                            // When a valid RSSI is read from controller (not used by application)
  simpleBLEObserverEventCB
};
/*
static gapObserverRoleCB_t observerCB =
{
NULL,                     // RSSI callback
simpleBLEObserverEventCB  // Event callback
};*/

// GAP Bond Manager Callbacks
static gapBondCBs_t biscuit_BondMgrCBs =
{
  NULL,                     // Passcode callback (not used by application)
  NULL                      // Pairing / Bonding state Callback (not used by application)
};

// Simple GATT Profile Callbacks
static txrxServiceCBs_t biscuit_TXRXServiceCBs =
{
  txrxServiceChangeCB    // Charactersitic value change callback
};
/*********************************************************************
* PUBLIC FUNCTIONS
*/

/*********************************************************************
* @fn      SimpleBLEPeripheral_Init
*
* @brief   Initialization function for the Simple BLE Peripheral App Task.
*          This is called during initialization and should contain
*          any application specific initialization (ie. hardware
*          initialization/setup, table initialization, power up
*          notificaiton ... ).         
*
* @param   task_id - the ID assigned by OSAL.  This ID should be
*                    used to send messages and set timers.
*
* @return  none
*/
void Biscuit_Init( uint8 task_id )
{
  biscuit_TaskID = task_id;
  
  // Setup the GAP
  VOID GAP_SetParamValue( TGAP_CONN_PAUSE_PERIPHERAL, DEFAULT_CONN_PAUSE_PERIPHERAL );
  
  // Setup the GAP Peripheral Role Profile
  {
    // Device starts advertising upon initialization
    uint8 initial_advertising_enable = TRUE;
    
    // By setting this to zero, the device will go into the waiting state after
    // being discoverable for 30.72 second, and will not being advertising again
    // until the enabler is set back to TRUE
    uint16 gapRole_AdvertOffTime = 0;
    
    uint8 enable_update_request = DEFAULT_ENABLE_UPDATE_REQUEST;
    uint16 desired_min_interval = DEFAULT_DESIRED_MIN_CONN_INTERVAL;
    uint16 desired_max_interval = DEFAULT_DESIRED_MAX_CONN_INTERVAL;
    uint16 desired_slave_latency = DEFAULT_DESIRED_SLAVE_LATENCY;
    uint16 desired_conn_timeout = DEFAULT_DESIRED_CONN_TIMEOUT;
    
    // Set the GAP Role Parametersuint8 initial_advertising_enable = TRUE;
    GAPRole_SetParameter( GAPROLE_ADVERT_ENABLED, sizeof( uint8 ), &initial_advertising_enable );
    GAPRole_SetParameter( GAPROLE_ADVERT_OFF_TIME, sizeof( uint16 ), &gapRole_AdvertOffTime );
    
    GAPRole_SetParameter( GAPROLE_SCAN_RSP_DATA, sizeof ( scanRspData ), scanRspData );
    
    GAPRole_SetParameter( GAPROLE_PARAM_UPDATE_ENABLE, sizeof( uint8 ), &enable_update_request );
    GAPRole_SetParameter( GAPROLE_MIN_CONN_INTERVAL, sizeof( uint16 ), &desired_min_interval );
    GAPRole_SetParameter( GAPROLE_MAX_CONN_INTERVAL, sizeof( uint16 ), &desired_max_interval );
    GAPRole_SetParameter( GAPROLE_SLAVE_LATENCY, sizeof( uint16 ), &desired_slave_latency );
    GAPRole_SetParameter( GAPROLE_TIMEOUT_MULTIPLIER, sizeof( uint16 ), &desired_conn_timeout );
  }
  
  // Setup observer related GAP profile properties
  {
    uint8 scanRes = DEFAULT_MAX_SCAN_RES;
    GAPRole_SetParameter(GAPOBSERVERROLE_MAX_SCAN_RES, sizeof( uint8 ), &scanRes);
  }
  
  i2c_init();
  
  // Set the GAP Characteristics
  uint8 nameFlag = eeprom_read(4);
  uint8 nameLen = eeprom_read(5);
  if( (nameFlag!=1) || (nameLen>20) )        // First time power up after burning firmware  
  {
    GGS_SetParameter( GGS_DEVICE_NAME_ATT, GAP_DEVICE_NAME_LEN, attDeviceName );
    uint8 len = strlen( (char const *)attDeviceName );
    TXRX_SetParameter( DEV_NAME_CHAR, len, attDeviceName );
    
    eeprom_write(4, 1);
    eeprom_write(5, len);
    for(uint8 i=0; i<len; i++)
    {
      eeprom_write(i+8, attDeviceName[i]);
    }
  }
  else
  {    
    uint8 devName[GAP_DEVICE_NAME_LEN];
    for(uint8 i=0; i<nameLen; i++)
    {
      devName[i] = eeprom_read(i+8);
    }
    devName[nameLen] = '\0';
    GGS_SetParameter( GGS_DEVICE_NAME_ATT, GAP_DEVICE_NAME_LEN, devName );
    TXRX_SetParameter( DEV_NAME_CHAR, nameLen, devName );
  } 
  
  uint8 LocalName[GAP_DEVICE_NAME_LEN];
  nameLen = eeprom_read(5);
  for(uint8 i=0; i<nameLen; i++)
  {
    LocalName[i] = eeprom_read(i+8);
  }
  advertData[3] = nameLen + 1;  
  //osal_memcpy(&advertData[5], LocalName, nameLen);  
  //osal_memset(&advertData[nameLen+5], 0, 31-5-nameLen);
  GAPRole_SetParameter( GAPROLE_ADVERT_DATA, sizeof( advertData ), advertData );
  
  // Set advertising interval
  {
    uint16 advInt = DEFAULT_ADVERTISING_INTERVAL;
    
    GAP_SetParamValue( TGAP_LIM_DISC_ADV_INT_MIN, advInt );
    GAP_SetParamValue( TGAP_LIM_DISC_ADV_INT_MAX, advInt );
    GAP_SetParamValue( TGAP_GEN_DISC_ADV_INT_MIN, advInt );
    GAP_SetParamValue( TGAP_GEN_DISC_ADV_INT_MAX, advInt );
  }
  
  {
    GAP_SetParamValue( TGAP_GEN_DISC_SCAN, DEFAULT_SCAN_DURATION );
    GAP_SetParamValue( TGAP_LIM_DISC_SCAN, DEFAULT_SCAN_DURATION );
  }
  
  // Setup the GAP Bond Manager
  {
    uint32 passkey = 0; // passkey "000000"
    uint8 pairMode = GAPBOND_PAIRING_MODE_WAIT_FOR_REQ;
    uint8 mitm = TRUE;
    uint8 ioCap = GAPBOND_IO_CAP_DISPLAY_ONLY;
    uint8 bonding = TRUE;
    GAPBondMgr_SetParameter( GAPBOND_DEFAULT_PASSCODE, sizeof ( uint32 ), &passkey );
    GAPBondMgr_SetParameter( GAPBOND_PAIRING_MODE, sizeof ( uint8 ), &pairMode );
    GAPBondMgr_SetParameter( GAPBOND_MITM_PROTECTION, sizeof ( uint8 ), &mitm );
    GAPBondMgr_SetParameter( GAPBOND_IO_CAPABILITIES, sizeof ( uint8 ), &ioCap );
    GAPBondMgr_SetParameter( GAPBOND_BONDING_ENABLED, sizeof ( uint8 ), &bonding );
  }
  
  // Initialize GATT attributes
  GGS_AddService( GATT_ALL_SERVICES );            // GAP
  GATTServApp_AddService( GATT_ALL_SERVICES );    // GATT attributes
  //DevInfo_AddService();                           // Device Information Service
  TXRX_AddService( GATT_ALL_SERVICES );  // Simple GATT Profile
#if defined FEATURE_OAD
  VOID OADTarget_AddService();                    // OAD Profile
#endif
  
#if defined( CC2540_MINIDK )
  
  // Register for all key events - This app will handle all key events
  RegisterForKeys( biscuit_TaskID );
  
  // makes sure LEDs are off
  HalLedSet( (HAL_LED_1 | HAL_LED_2), HAL_LED_MODE_OFF );
  
  // For keyfob board set GPIO pins into a power-optimized state
  // Note that there is still some leakage current from the buzzer,
  // accelerometer, LEDs, and buttons on the PCB.
  
  P0SEL = 0; // Configure Port 0 as GPIO
  P1SEL = 0; // Configure Port 1 as GPIO
  P2SEL = 0; // Configure Port 2 as GPIO
  
  P0DIR = 0xFC; // Port 0 pins P0.0 and P0.1 as input (buttons),
  // all others (P0.2-P0.7) as output
  P1DIR = 0xFF; // All port 1 pins (P1.0-P1.7) as output
  P2DIR = 0x1F; // All port 1 pins (P2.0-P2.4) as output
  
  P0 = 0x03; // All pins on port 0 to low except for P0.0 and P0.1 (buttons)
  P1 = 0;   // All pins on port 1 to low
  P2 = 0;   // All pins on port 2 to low
  
#endif // #if defined( CC2540_MINIDK )
  
  // Register callback with TXRXService
  VOID TXRX_RegisterAppCBs( &biscuit_TXRXServiceCBs );
  
  // Enable clock divide on halt
  // This reduces active current while radio is active and CC254x MCU
  // is halted
  //  HCI_EXT_ClkDivOnHaltCmd( HCI_EXT_ENABLE_CLK_DIVIDE_ON_HALT );
  
  // Initialize serial interface
  P1SEL = 0x30;
  P1DIR |= 0x02;
  P1_1 = 1;
  PERCFG |= 1;
  NPI_InitTransport(dataHandler);
  
  uint8 flag, baud;
  uint8 value;
  flag = eeprom_read(0);
  baud = eeprom_read(1);
  if( flag!=1 || baud>4 )       // First time power up after burning firmware
  {
    U0GCR &= 0xE0;      // Default baudrate 57600
    U0GCR |= 0x0A;
    U0BAUD = 216;
    value = 3;
    
    eeprom_write(0, 1);
    eeprom_write(1, 3);
  }
  else
  {
    switch(baud)
    {
    case 0:   //9600
      {
        U0GCR &= 0xE0;
        U0GCR |= 0x08;
        U0BAUD = 59;
        value = 0;
        break;
      }
      
    case 1:   //19200
      {
        U0GCR &= 0xE0;
        U0GCR |= 0x09;
        U0BAUD = 59;
        value = 1;
        break;
      }
      
    case 2:   //38400
      {
        U0GCR &= 0xE0;
        U0GCR |= 0x0A;
        U0BAUD = 59;
        value = 2;
        break;
      }
      
    case 3:   //57600
      {
        U0GCR &= 0xE0;
        U0GCR |= 0x0A;
        U0BAUD = 216;
        value = 3;
        break;
      }
      
    case 4:   //115200
      {
        U0GCR &= 0xE0;
        U0GCR |= 0x0B;
        U0BAUD = 216;
        value = 4;
        break;
      }
      
    default:
      break;
    }
  }
  TXRX_SetParameter( BAUDRATE_CHAR, 1, &value );
  
  uint8 flag2, txpwr;
  flag2 = eeprom_read(2);
  txpwr = eeprom_read(3);
  if( flag2!=1 || txpwr>3 )       // First time power up after burning firmware
  {
    HCI_EXT_SetTxPowerCmd( HCI_EXT_TX_POWER_0_DBM );
    txpwr = HCI_EXT_TX_POWER_0_DBM;
    
    eeprom_write(2, 1);
    eeprom_write(3, HCI_EXT_TX_POWER_0_DBM);
  }
  else
  {
    HCI_EXT_SetTxPowerCmd( txpwr );
  }
  TXRX_SetParameter( TX_POWER_CHAR, 1, &txpwr );
  
  // Setup a delayed profile startup
  osal_set_event( biscuit_TaskID, SBP_START_DEVICE_EVT );
}

/*********************************************************************
* @fn      Biscuit_ProcessEvent
*
* @brief   Simple BLE Peripheral Application Task event processor.  This function
*          is called to process all events for the task.  Events
*          include timers, messages and any other user defined events.
*
* @param   task_id  - The OSAL assigned task ID.
* @param   events - events to process.  This is a bit map and can
*                   contain more than one event.
*
* @return  events not processed
*/
uint16 Biscuit_ProcessEvent( uint8 task_id, uint16 events )
{
  
  VOID task_id; // OSAL required parameter that isn't used in this function
  
  if ( events & SYS_EVENT_MSG )
  {
    uint8 *pMsg;
    
    if ( (pMsg = osal_msg_receive( biscuit_TaskID )) != NULL )
    {
      biscuit_ProcessOSALMsg( (osal_event_hdr_t *)pMsg );
      
      // Release the OSAL message
      VOID osal_msg_deallocate( pMsg );
    }
    
    // return unprocessed events
    return (events ^ SYS_EVENT_MSG);
  }
  
  if ( events & SBP_ADV_IN_CONNECTION_EVT )
  {
    debugPrintLine("hejsan123");
    uint8 turnOnAdv = TRUE;
    // Turn on advertising while in a connection
    GAPRole_SetParameter( GAPROLE_ADVERT_ENABLED, sizeof( uint8 ), &turnOnAdv );
    
    return (events ^ SBP_ADV_IN_CONNECTION_EVT);
  }
  
  if ( events & SBP_START_DEVICE_EVT )
  {
    // Start the Device
    VOID GAPRole_StartDevice( &biscuit_PeripheralCBs);
    
    // Start Bond Manager
    VOID GAPBondMgr_Register( &biscuit_BondMgrCBs );
    
    // Set timer for first periodic event
    osal_start_timerEx( biscuit_TaskID, SBP_PERIODIC_EVT, SBP_PERIODIC_EVT_PERIOD );
    
    
    
    return ( events ^ SBP_START_DEVICE_EVT );
  }
  
  if ( events & SBP_RX_TIME_OUT_EVT )
  {
    uint8 data[20];
    uint8 send;
    while(rxLen != 0)
    {
      if(rxLen <= 20)
      {
        send = rxLen;
        rxLen = 0;
      }
      else
      { 
        send = 20;      
        rxLen -= 20;
      }
      for(uint8 i=0; i<send; i++)
      {
        data[i] = RXBuf[rxTail];
        rxTail++;
        if(rxTail == MAX_RX_LEN)
        {
          rxTail = 0;
        }
      }
      TXRX_SetParameter(TX_DATA_CHAR, send, data);
    }
    
    return (events ^ SBP_RX_TIME_OUT_EVT);
  }
  
  if ( events & SBP_PERIODIC_EVT )
  {
    // Restart timer
    if ( SBP_PERIODIC_EVT_PERIOD )
    {
      osal_start_timerEx( biscuit_TaskID, SBP_PERIODIC_EVT, SBP_PERIODIC_EVT_PERIOD );
    }
    
    // Perform periodic application task
    performPeriodicTask();
    
    return (events ^ SBP_PERIODIC_EVT);
  }
  
  // Discard unknown events
  return 0;
}

/*********************************************************************
* @fn      biscuit_ProcessOSALMsg
*
* @brief   Process an incoming task message.
*
* @param   pMsg - message to process
*
* @return  none
*/
static void biscuit_ProcessOSALMsg( osal_event_hdr_t *pMsg )
{
  switch ( pMsg->event )
  {
#if defined( CC2540_MINIDK )
  case KEY_CHANGE:
    biscuit_HandleKeys( ((keyChange_t *)pMsg)->state, ((keyChange_t *)pMsg)->keys );
    break;
#endif // #if defined( CC2540_MINIDK )
    
  default:
    // do nothing
    break;
  }
}

#if defined( CC2540_MINIDK )
/*********************************************************************
* @fn      biscuit_HandleKeys
*
* @brief   Handles all key events for this device.
*
* @param   shift - true if in shift/alt.
* @param   keys - bit field for key events. Valid entries:
*                 HAL_KEY_SW_2
*                 HAL_KEY_SW_1
*
* @return  none
*/
static void biscuit_HandleKeys( uint8 shift, uint8 keys )
{
  // do nothing
}
#endif // #if defined( CC2540_MINIDK )

/*********************************************************************
* @fn      peripheralStateNotificationCB
*
* @brief   Notification from the profile of a state change.
*
* @param   newState - new state
*
* @return  none
*/
static void peripheralStateNotificationCB( gaprole_States_t newState )
{  
  static uint8 first_conn_flag = 0;
  switch ( newState )
  {
  case GAPROLE_STARTED:
    {
      uint8 ownAddress[B_ADDR_LEN];
      uint8 systemId[DEVINFO_SYSTEM_ID_LEN];
      
      GAPRole_GetParameter(GAPROLE_BD_ADDR, ownAddress);
      
      // use 6 bytes of device address for 8 bytes of system ID value
      systemId[0] = ownAddress[0];
      systemId[1] = ownAddress[1];
      systemId[2] = ownAddress[2];
      
      // set middle bytes to zero
      systemId[4] = 0x00;
      systemId[3] = 0x00;
      
      // shift three bytes up
      systemId[7] = ownAddress[5];
      systemId[6] = ownAddress[4];
      systemId[5] = ownAddress[3];
      
      DevInfo_SetParameter(DEVINFO_SYSTEM_ID, DEVINFO_SYSTEM_ID_LEN, systemId);
    }
    break;
    
  case GAPROLE_ADVERTISING:
    {
      debugPrintLine("Started advertising");
      uint8 stat = getStatus_();
      debugPrintRaw(&stat);
      
      
      
    }
    break;
    
  case GAPROLE_CONNECTED:
    { 
      debugPrintLine("GAPROLE_CONNECTED");
             
      
      // Only turn advertising on for this state when we first connect
      // otherwise, when we go from connected_advertising back to this state
      // we will be turning advertising back on.

      uint8 turnOnAdv = TRUE;
      // Turn on advertising while in a connection
      GAPRole_SetParameter( GAPROLE_ADVERT_ENABLED, sizeof( uint8 ), &turnOnAdv );      
    }
    break;
    
    case GAPROLE_CONNECTED_ADV:
    {
      debugPrintLine("GAPROLE_CONNECTED_ADV");
      uint8 stat = getStatus_();
      debugPrintRaw(&stat);
    }
    break;      
  case GAPROLE_WAITING:
    {
      debugPrintLine("GAPROLE_WAITING");
      uint8 turnOnAdv = TRUE;
      // Turn on advertising while in a connection
      GAPRole_SetParameter( GAPROLE_ADVERT_ENABLED, sizeof( uint8 ), &turnOnAdv ); 
    }
    break;
    
  case GAPROLE_WAITING_AFTER_TIMEOUT:
    {
      debugPrintLine("GAPROLE_WAITING_AFTER_TIMEOUT");
      // Reset flag for next connection.
      first_conn_flag = 0;
    }
    break;
    
  case GAPROLE_ERROR:
    {
      debugPrintLine("GAPROLE ERROR");
    }
    break;
    
  default:
    {
    }
    break;
    
  }
  
  gapProfileState = newState;
}

/*********************************************************************
* @fn      simpleBLEObserverEventCB
*
* @brief   Observer event callback function.
*
* @param   pEvent - pointer to event structure
*
* @return  none
*/
static void simpleBLEObserverEventCB( observerRoleEvent_t *pEvent )
{
  
  debugPrintRaw(&pEvent->gap.opcode);
  switch ( pEvent->gap.opcode )
  {
  case GAP_DEVICE_INIT_DONE_EVENT:  
    {
      //LCD_WRITE_STRING( "BLE Observer", HAL_LCD_LINE_1 );
      //LCD_WRITE_STRING( bdAddr2Str( pEvent->initDone.devAddr ),  HAL_LCD_LINE_2 );
    }
    break;
    
  case GAP_DEVICE_INFO_EVENT:
    {
      //simpleBLEAddDeviceInfo( pEvent->deviceInfo.addr, pEvent->deviceInfo.addrType );
      debugPrintLine("ehmarine");
      uint8* data = pEvent->deviceInfo.pEvtData;
      uint8  dataLen = pEvent->deviceInfo.dataLen;
      data[0] = 3;
      data[1] = 1;
      
      
      
      
      if(data[3] == 0x1A){
        osal_memcpy(&advertData[5], data, dataLen-5);
        GAPRole_SetParameter( GAPROLE_ADVERT_DATA, sizeof( advertData ), advertData );
        //uint8 initial_advertising_enable = TRUE;
        //GAPRole_SetParameter( GAPROLE_ADVERT_ENABLED, sizeof( uint8 ), &initial_advertising_enable );
        
      }
      
      
      
      
    }
    break;
    
  case GAP_DEVICE_DISCOVERY_EVENT:
    {
      // discovery complete
      simpleBLEScanning = FALSE;
      
      // Copy results
      simpleBLEScanRes = pEvent->discCmpl.numDevs;
      osal_memcpy( simpleBLEDevList, pEvent->discCmpl.pDevList,
                  (sizeof( gapDevRec_t ) * pEvent->discCmpl.numDevs) );
      
      //LCD_WRITE_STRING_VALUE( "Devices Found", simpleBLEScanRes,
      //                        10, HAL_LCD_LINE_1 );
      /*if ( simpleBLEScanRes > 0 )
      {
      LCD_WRITE_STRING( "<- To Select", HAL_LCD_LINE_2 );
    }*/
      
      // initialize scan index to last device
      simpleBLEScanIdx = simpleBLEScanRes;
    }
    break;
    
  default:
    break;
  }
}



/*********************************************************************
* @fn      performPeriodicTask
*
* @brief   Perform a periodic application task. This function gets
*          called every five seconds as a result of the SBP_PERIODIC_EVT
*          OSAL event. In this example, the value of the third
*          characteristic in the SimpleGATTProfile service is retrieved
*          from the profile, and then copied into the value of the
*          the fourth characteristic.
*
* @param   none
*
* @return  none
*/
static void performPeriodicTask( void )
{
  /*debugPrintLine("Starting search...");
  uint8 stat = getStatus_();
  debugPrintRaw(&stat);
  */
  bStatus_t ret = GAPObserverRole_StartDiscovery( DEFAULT_DISCOVERY_MODE,
                                                 DEFAULT_DISCOVERY_ACTIVE_SCAN,
                                                 DEFAULT_DISCOVERY_WHITE_LIST );
  
  
  // do nothing
}

/*********************************************************************
* @fn      txrxServiceChangeCB
*
* @brief   Callback from SimpleBLEProfile indicating a value change
*
* @param   paramID - parameter ID of the value that was changed.
*
* @return  none
*/
static void txrxServiceChangeCB( uint8 paramID )
{
  uint8 data[20];
  uint8 len;
  
  if (paramID == TXRX_RX_DATA_READY)
  {
    debugPrintLine("Sending UART data");
    TXRX_GetParameter(RX_DATA_CHAR, &len, data);
    HalUARTWrite(NPI_UART_PORT, (uint8*)data, len);
  }
  else if (paramID == TXRX_RX_NOTI_ENABLED)
  {
    GAPRole_SendUpdateParam( DEFAULT_DESIRED_MAX_CONN_INTERVAL, DEFAULT_DESIRED_MIN_CONN_INTERVAL,
                            DEFAULT_DESIRED_SLAVE_LATENCY, DEFAULT_DESIRED_CONN_TIMEOUT, GAPROLE_RESEND_PARAM_UPDATE );
  }
  else if (paramID == BAUDRATE_SET)
  {
    uint8 newValue;
    TXRX_GetParameter(BAUDRATE_CHAR, &len, &newValue);
    switch(newValue)
    {
    case 0:   //9600
      {
        U0GCR &= 0xE0;
        U0GCR |= 0x08;
        U0BAUD = 59;
        break;
      }
      
    case 1:   //19200
      {
        U0GCR &= 0xE0;
        U0GCR |= 0x09;
        U0BAUD = 59;
        break;
      }
      
    case 2:   //38400
      {
        U0GCR &= 0xE0;
        U0GCR |= 0x0A;
        U0BAUD = 59;
        break;
      }
      
    case 3:   //57600
      {
        U0GCR &= 0xE0;
        U0GCR |= 0x0A;
        U0BAUD = 216;
        break;
      }
      
    case 4:   //115200
      {
        U0GCR &= 0xE0;
        U0GCR |= 0x0B;
        U0BAUD = 216;
        break;
      }
      
    default:
      break;
    }
    eeprom_write(1, newValue);
  }
  else if (paramID == DEV_NAME_CHANGED)
  {
    uint8 newDevName[GAP_DEVICE_NAME_LEN];
    TXRX_GetParameter(DEV_NAME_CHAR, &len, newDevName);
    
    uint8 devNamePermission = GATT_PERMIT_READ|GATT_PERMIT_WRITE; 
    GGS_SetParameter( GGS_W_PERMIT_DEVICE_NAME_ATT, sizeof ( uint8 ), &devNamePermission );
    newDevName[ len ] = '\0';
    GGS_SetParameter( GGS_DEVICE_NAME_ATT, GAP_DEVICE_NAME_LEN, newDevName );
    
    advertData[3] = len + 1;
    osal_memcpy(&advertData[5], newDevName, len);
    osal_memset(&advertData[len+5], 0, 31-5-len);
    GAPRole_SetParameter( GAPROLE_ADVERT_DATA, sizeof( advertData ), advertData );
    
    eeprom_write(5, len);
    for(uint8 i=0; i<len; i++)
    {
      eeprom_write(i+8, newDevName[i]);
    }
  }
  else if (paramID == TX_POWER_CHANGED)
  {
    uint8 newValue;
    TXRX_GetParameter(TX_POWER_CHAR, &len, &newValue);
    
    if(newValue < 4 && newValue >= 0)
    {
      HCI_EXT_SetTxPowerCmd( newValue );
    }
    eeprom_write(3, newValue);
  }
}

/*********************************************************************
* @fn      dataHandler
*
* @brief   Callback from UART indicating a data coming
*
* @param   port - data port.
*
* @param   events - type of data.
*
* @return  none
*/
static void dataHandler( uint8 port, uint8 events )
{  
  if((events & HAL_UART_RX_TIMEOUT) == HAL_UART_RX_TIMEOUT)
  {
    osal_stop_timerEx( biscuit_TaskID, SBP_RX_TIME_OUT_EVT);
    
    uint8 len = NPI_RxBufLen();
    uint8 buf[128];
    NPI_ReadTransport( buf, len );
    
    uint8 copy;   
    if(len > (MAX_RX_LEN-rxLen))
    {    
      copy = MAX_RX_LEN - rxLen;
      rxLen = MAX_RX_LEN;
    }
    else
    {
      rxLen += len;
      copy = len;
    }
    for(uint8 i=0; i<copy; i++)
    {
      RXBuf[rxHead] = buf[i];
      rxHead++;
      if(rxHead == MAX_RX_LEN)
      {
        rxHead = 0;
      }
    }
    
    osal_start_timerEx( biscuit_TaskID, SBP_RX_TIME_OUT_EVT, SBP_RX_TIME_OUT);
  }
  return;
}

/*********************************************************************
*********************************************************************/
