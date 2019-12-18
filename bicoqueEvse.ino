// screen
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// modbus
#include <ModbusMaster232.h>

// basic
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

#include <ESP8266httpUpdate.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <FS.h>


// Instantiate ModbusMaster object as slave ID 1
ModbusMaster232 node(1);
// ModBus is a complet new lib. add sofwareserial to use serial1 Corresponding to RX0(GPIO3) and TX0(GPIO1) in board

// firmware version
#define SOFT_VERSION "1.4.74"
#define SOFT_DATE "2019-09-03"
#define EVSE_VERSION 11

#define DEBUG 1

// address for EEPROM
#define ALREADYBOOT 98
#define ALREADYBOOT_SIZE 1

// Wifi AP info for configuration
const char* wifiApSsid = "bicoqueEVSE";
const char* wifiApPasswd = "randomPass";
String wifiSsid;
String wifiPasswd;


// Update info
#define BASE_URL "http://mangue.net/ota/esp/bicoqueEvse/"
#define UPDATE_URL "http://mangue.net/ota/esp/bicoqueEvse/update.php"

// Web server info
ESP8266WebServer server(80);


// EVSE info
char* evseStatusName[] = { "n/a", "Waiting Car", "Car Connected", "Charging", "Charging", "Error" } ;
String evseRegisters[25]; // indicate the number of registers
int registers[] = { 1000, 1001, 1002, 1003, 1004, 1005, 1006, 2000, 2001, 2002, 2003, 2004, 2005, 2006, 2007, 2008, 2009, 2010, 2011, 2012, 2013, 2014, 2015, 2016, 2017 };
int simpleEvseVersion ; // need to have it to check a begining and not start if it's not the same
int evseCurrentLimit  = 0;
int evsePowerOnLimit  = 0;
int evseHardwareLimit = 0;
int evseStatus        = 0;
int evseEnable        = 0;
int evseStatusCounter = 0;
int evseConnectionProblem = 0;
#define NORMAL_STATE 0
#define FORCE_CURRENT 1
#define EVSE_ACTIVE 0
#define EVSE_DISACTIVE 1


// config
int wifiEnable;
int evseAutoStart;
int alreadyBoot;
IPAddress ip;

// Button switch
int inPin = 12; // D6
int buttonCounter = 0;
int buttonPressed = 0; //When a button just pressed, do not continue to thik it's pressed. Need to release button to restart counter


// Module
int internalMode  = 0; // 0: normal - 1: Not define - 2: menu
int menuTimerIdle = 0;
int sleepModeTimer = 0;
int sleepMode = 0;

// Screen LCD
LiquidCrystal_I2C lcd(0x27, 20, 4); // set the LCD address to 0x27 for a 20 chars and 4 line display
// Using ports SDCLK / SDA / SCL / VCC +5V / GND

// Relay install on v3.3 / GND / D4
int relayOutput = D4; // set to 0 if disactive


// Json allocation
StaticJsonDocument<200> jsonBuffer;
String dataJsonConfig;
DynamicJsonDocument jsonConfig(200);
//JsonObject jsonConfigWifi = jsonConfig.createNestedObject("wifi");
//JsonObject jsonConfigEvse = jsonConfig.createNestedObject("evse");

String dataJsonConsumption;
DynamicJsonDocument jsonConsumption(200);
//JsonObject jsonConsumptionDays = jsonConsumption.createNestedObject("days");

/*

{
"location": {
"lat": 48.748010,
"lon": 2.293491
},
"feeds": [
{
"key": "a1",
"value": 42
},
{
"key": "a2",
"value": 43
}
]
}


// Allocate JsonDocument
const int capacity = JSON_ARRAY_SIZE(2) + 4 * JSON_OBJECT_SIZE(2);
StaticJsonDocument<capacity> doc;
// Add the "location" object
JsonObject location = doc.createNestedObject("location");
location["lat"] = 48.748010;
location["lon"] = 2.293491;
// Add the "feeds" array
JsonArray feeds = doc.createNestedArray("feeds");
JsonObject feed1 = feeds.createNestedObject();
feed1["key"] = "a1";
feed1["value"] = analogRead(A1);
JsonObject feed2 = feeds.createNestedObject();
feed2["key"] = "a2";
feed2["value"] = analogRead(A2);

*/







// screen default page
int page = 0;

// Menu setings
//char* menu1[] = { "SETINGS", "Amperage","AutoStart","Infos","Consommation","Wifi","Exit" } ;
char* menu1[] = { "SETINGS", "Amperage", "AutoStart", "Infos", "Wifi", "Reboot", "Exit" } ;
char* menu2[] = { "WIFI", "Activation", "Reset", "Back" } ;
//char* menu3[] = { "CONSOMMATION","Sur 24h","Sur 30 jours","Total","Back" };

char* enum101[] = { "AMPERAGE", "5A", "6A", "7A", "8A", "9A", "10A", "11A", "12A", "13A", "14A", "15A", "16A", "17A", "18A", "19A", "20A", "21A", "22A", "23A", "24A", "25A", "26A", "27A", "28A", "29A", "30A" };
char* enum201[] = { "AUTOSTART", "Active", "Desactive" };
char* enum301[] = { "WIFI ACTIVATION", "Active", "Desactive" };
char* enum401[] = { "WIFI RESET", "Oui", "Non" };

char* pages1001[] = { "INFOS", "page1", "page2" };

int menuStatus = 1;
char menuValue[30];
#define NUMITEMS(arg) ((unsigned int) (sizeof (arg) / sizeof (arg [0])))
#define MENU_DW 1
#define MENU_SELECT 2


// current consumptions
#define AC_POWER 230
int consumptions[31] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
int consumptionLastTime    = 0;
float consumptionCounter   = 0;
int consumptionActual      = 0;
int consumptionTotal       = 0;
float consumptionTotalTemp = 0;
int statsLastWrite         = 0;
int consumptionLastCharge  = 0;
int consumptionLastChargeRunning = 0;

// Time
long timeAtStarting = 0; // need to get from ntp server timestamp when ESP start


// ********************************************
// All Menu Functions
// ********************************************
int menuGetFromAction(void)
{
  Serial.print("Debug menu: menuValue : "); Serial.println(menuValue);

  if (menuStatus >= 100 && menuStatus < 200) // AMPERAGE
  {
    menuStatus = 1; // menu to return to
    // remove the 'A' at the end of value ex: 5A -> 5
    menuValue[ strlen(menuValue) - 1 ] = '\0';
    String tempValue = menuValue;

    //-- write data
    evseUpdatePower(tempValue.toInt(), NORMAL_STATE);
  }
  else if (menuStatus >= 200 && menuStatus < 300) // AUTOSTART
  {
    menuStatus = 2;
    if ( strcmp(menuValue, enum201[1]) == 0)
    {
      evseAutoStart = 1;
    }
    else if (strcmp(menuValue, enum201[2]) == 0)
    {
      evseAutoStart = 0;
    }

    evseUpdatePower(evsePowerOnLimit, NORMAL_STATE);
    jsonConfig["evse"]["autostart"] = evseAutoStart;
    configSave();
    // eepromWrite(AUTOSTART, String(evseAutoStart) );
  }
  else if (menuStatus >= 300 && menuStatus < 400)
  {
    menuStatus = 11;
    if ( strcmp(menuValue, enum301[1]) == 0) {
      wifiEnable = 1;
    }
    else if ( strcmp(menuValue, enum301[2]) == 0)
    {
      wifiEnable = 0;
      WiFi.mode(WIFI_OFF);
      WiFi.disconnect();
    }

    jsonConfig["wifi"]["enable"] = wifiEnable;
    configSave();
  }
  else if (menuStatus >= 400 && menuStatus < 500)
  {
    menuStatus = 12;
    if ( strcmp(menuValue, enum401[1]) == 0)
    {
      // Reset Wifi info from eeprom
      wifiReset();
    }
  }
  else if (menuStatus >= 1000 && menuStatus < 2000)
  {
    menuStatus = 3;
  }
  else
  {
    // continue navigation into menu
    switch (menuStatus)
    {
      case 1: if (evsePowerOnLimit < 5) {
          evsePowerOnLimit = 5;
        }; menuStatus = 101 + evsePowerOnLimit - 5 ; break;
      case 2: if (evseAutoStart) {
          menuStatus = 201;
        } else {
          menuStatus = 202;
        }; break;
      case 3: menuStatus = 1001; evseAllInfo() ; break;
      case 4: menuStatus = 11; break;
      case 5: ESP.restart(); return 0;
      case 6: menuStatus = 1 ; internalMode = 3 ; return 0; // exit menu

      case 11:
        // get actual value and set the good menu
        if (wifiEnable) {
          menuStatus = 301;
        } else {
          menuStatus = 302;
        };
        break; // enum
      case 12: menuStatus = 401; break;
      case 13: menuStatus = 4; break;
    }
  }
  return 1;
}

void getMenu(int action)
{

  String menuToPrint[30] ;
  int itemsToPrint;
  int menuTemp;
  int valueSelecting = 0;

  // Need to check select action
  if (action == MENU_SELECT)
  {
    int fnret = menuGetFromAction();
    if (fnret == 0) {
      return;  // Exit wanted
    }

    if (DEBUG > 0)
    {
      printf("Value to store : %s\n", menuValue);
    }
  }

  // set array to generic one
  if (menuStatus < 10)
  {
    menuTemp = 0; itemsToPrint = NUMITEMS(menu1); for ( int i = 0 ; i < itemsToPrint ; i++) {
      menuToPrint[i] = menu1[i];
    }
  }
  else if (menuStatus >= 10 && menuStatus < 20)
  {
    menuTemp = 10; itemsToPrint = NUMITEMS(menu2); for ( int i = 0 ; i < itemsToPrint ; i++) {
      menuToPrint[i] = menu2[i];
    }
  }
  else if (menuStatus >= 100 && menuStatus < 200)
  {
    menuTemp = 100; itemsToPrint = NUMITEMS(enum101); for ( int i = 0 ; i < itemsToPrint ; i++) {
      menuToPrint[i] = enum101[i];
    }
    valueSelecting = 1;
  }
  else if (menuStatus >= 200 && menuStatus < 300)
  {
    menuTemp = 200; itemsToPrint = NUMITEMS(enum201); for ( int i = 0 ; i < itemsToPrint ; i++) {
      menuToPrint[i] = enum201[i];
    }
    valueSelecting = 1;
  }
  else if (menuStatus >= 300 && menuStatus < 400)
  {
    menuTemp = 300; itemsToPrint = NUMITEMS(enum301); for ( int i = 0 ; i < itemsToPrint ; i++) {
      menuToPrint[i] = enum301[i];
    }
    valueSelecting = 1;
  }
  else if (menuStatus >= 400 && menuStatus < 500)
  {
    menuTemp = 400; itemsToPrint = NUMITEMS(enum401); for ( int i = 0 ; i < itemsToPrint ; i++) {
      menuToPrint[i] = enum401[i];
    }
    valueSelecting = 1;
  }
  else if (menuStatus > 1000)
  {
    menuTemp = 1000;
    itemsToPrint = NUMITEMS(evseRegisters);
    for ( int i = 0 ; i < itemsToPrint ; i++)
    {
      menuToPrint[i] = evseRegisters[i];
      if (DEBUG > 0)
      {
        Serial.print("pointer address : "); Serial.println((long)&menuToPrint[i]);
        Serial.print(i); Serial.print(" ----> "); Serial.println(menuToPrint[i]);
      }
    }
    valueSelecting = 3;
  }

  if (action == MENU_DW) {
    menuStatus++;
  }
  if ( (menuStatus - menuTemp) >= itemsToPrint) {
    menuStatus = menuTemp + 1;
  }

  menuToPrint[ (menuStatus - menuTemp) ].toCharArray(menuValue, 30);

  // Printing on screen
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(menuToPrint[0]);


  if (valueSelecting == 0)
  {
    int screenCursor = 1;
    int myPosition = menuStatus - menuTemp;

    // we have only 3 lines to print.
    int startArray = 0;
    if ( myPosition < 4) {
      startArray = 1;
    }
    else if (myPosition >= 4) {
      startArray = myPosition - 2;
    }

    for (int i = startArray ; i < itemsToPrint; i++)
    {
      char temp2[18] = "                 ";
      int tempLength = menuToPrint[i].length();
      int lenghToAdd = 17 - tempLength; // align on right
      lenghToAdd = 1; // align on left
      for (int j = 0; j < tempLength ; j++) {
        temp2[(j + lenghToAdd)] = menuToPrint[i][j];
      }

      if (screenCursor < 4)
      {
        if ( i == myPosition )
        {
          lcd.setCursor( 1, screenCursor);
          lcd.write(0);
        }
        else
        {
          lcd.setCursor( 2, screenCursor);
        }
        lcd.print(temp2);
      }

      screenCursor++;
    }
  }
  else if (valueSelecting == 1)
  {
    char temp2[20] = "                   ";
    int tempLength = menuToPrint[(menuStatus - menuTemp)].length();
    int lenghToAdd = 19 - tempLength;
    for (int j = 0; j < tempLength ; j++) {
      temp2[(j + lenghToAdd)] = menuToPrint[(menuStatus - menuTemp)][j];
    }
    lcd.setCursor(0, 1);
    lcd.print(temp2);
  }
  else if (valueSelecting == 2)
  {
    for (int i = 1; i < itemsToPrint; i++)
    {
      char temp2[22] = "";
      int tempLength = menuToPrint[i].length(); //strlen(menuToPrint[i]);
      int lenghToAdd = 21 - tempLength;
      for (int j = 0; j < tempLength ; j++) {
        temp2[(j + lenghToAdd)] = menuToPrint[i][j];
      }


      if (i == (menuStatus - menuTemp))
      {
        lcd.setCursor(20 - tempLength , 3);
        lcd.print(temp2);
      }
    }
  }
  else if (valueSelecting == 3)
  {
    int beginArray = (menuStatus - menuTemp - 1);
    if (beginArray > itemsToPrint ) {
      beginArray = 0;
    }

    for ( int i = beginArray ; i < (beginArray + 4) ; i++)
    {
      lcd.setCursor(0, (i - beginArray));
      lcd.print(menuToPrint[i]);
      Serial.print("menu debug = "); Serial.print(i); Serial.print("  --  "); Serial.println(menuToPrint[i]);
    }

    menuStatus = menuTemp + beginArray + 4;
  }
}









// ********************************************
// EVSE Functions
// ********************************************
void evseReload()
{
  node.readHoldingRegisters(1000, 1);
  evseCurrentLimit = node.getResponseBuffer(0);
  node.clearResponseBuffer();
  node.readHoldingRegisters(1002, 1);
  evseStatus = node.getResponseBuffer(0);
  node.clearResponseBuffer();
  node.readHoldingRegisters(1003, 1);
  evseHardwareLimit = node.getResponseBuffer(0);
  node.clearResponseBuffer();
  node.readHoldingRegisters(2000, 1);
  evsePowerOnLimit = node.getResponseBuffer(0);
  node.clearResponseBuffer();

  evseEnableCheck();

}
void evseAllInfo()
{
  for (int i = 0; i < NUMITEMS(registers); i++)
  {
    node.readHoldingRegisters( registers[i], 1);
    int registerResult = node.getResponseBuffer(0);
    String tempValue = "";
    tempValue += registers[i] ;
    tempValue += " : ";
    tempValue += registerResult ;
    evseRegisters[i] = tempValue;

    if (i == 2)
    {
      String messageToLog = "Read register : "; messageToLog += registers[i]; messageToLog += " - value : "; messageToLog += registerResult ;
      logger(messageToLog);
    }
    if (DEBUG)
    {
      Serial.print("Debug : "); Serial.println(evseRegisters[i]);
      delay(100);
    }
  }
  if (DEBUG)
  {
    for (int i = 0; i < 20; i++) {
      Serial.print(i);
      Serial.print(" - ");
      Serial.println(evseRegisters[i]);
    }
  }
}
void evseStatusCheck()
{
  node.readHoldingRegisters(1002, 1);
  evseStatus = node.getResponseBuffer(0);
  node.clearResponseBuffer();
}
void evseEnableCheck()
{
  node.readHoldingRegisters(2005, 1);
  int registerValue = node.getResponseBuffer(0);
  node.clearResponseBuffer();

  if (registerValue == 0)
  {
    // enable
    evseEnable = 1;
  }
  else if (registerValue == 16384)
  {
    // disable
    evseEnable = 0;
  }
  else if (registerValue == 8192)
  {
    // disable after charge
    evseEnable = 1;
  }
  else
  {
    evseEnable = 255;
  }
}
void evseUpdatePower(int power, int currentOnly)
{

  evseWrite("currentLimit", power);
  evseCurrentLimit = power;
  
  if (currentOnly == 0)
  {
    evseWrite("powerOn", power);
    evsePowerOnLimit = power;
  }
}
void evseWrite(String evseRegister, int value)
{
   int RegisterToWriteIn;
   if (evseRegister == "currentLimit")
   {
        Serial.print("Current limit : "); Serial.println(value);
        RegisterToWriteIn = 1000;
   }
   else if (evseRegister == "powerOn")
   {
        Serial.print("Power On : "); Serial.println(value);
        RegisterToWriteIn = 2000;
   }
   else if (evseRegister == "modbus")
   {
        Serial.print("Set Modbus communication : "); Serial.println(value);
        RegisterToWriteIn = 2001;
   }
   else if (evseRegister == "minAmpsValue")
   {
        // value can be 0-1-2-3-4-5
        // default 5. Need to set 0 for set currentLimit to 0A
        Serial.print("authorize min Amp : "); Serial.println(value);
        RegisterToWriteIn = 2002; 
   }
   else if (evseRegister == "evseStatus")
   {
	int valueToWrite;
	if (value == EVSE_ACTIVE)
	{
		valueToWrite = 0;
                if (relayOutput)
                {
                   // There is a bug in simpleEVSE. If the EVSE is disable and the car
                   // is plug for more than 15 mins, when we active the EVSE, nothing append.
                   // So we will cut the link between EVSE and CAR for 2 sec for init.
                   digitalWrite(relayOutput, HIGH);
                   delay(2000);
                   digitalWrite(relayOutput, LOW);
                }
	}
	else if (value == EVSE_DISACTIVE)
        {
                // valueToWrite = 8192; //-- disactive EVSE after charge
                valueToWrite = 16384; //-- disactive EVSE
        }
	else
	{
		Serial.println("evseStatus not correct");
		return;
	}

	value = valueToWrite;
	Serial.print("evseStatus, witre value into 2005 register : "); Serial.println(value);
	RegisterToWriteIn = 2005;
   }
   else
   {

    Serial.println("No register given... exit");
    return;
  }

  String messageToLog = "Write register : "; messageToLog += RegisterToWriteIn; messageToLog += " - value : "; messageToLog += value ;
  logger(messageToLog);

  node.setTransmitBuffer(0, value);
  node.writeMultipleRegisters(RegisterToWriteIn, 1);

  evseReload();
}




// ********************************************
// Hardware Button
// ********************************************
int checkButton()
{
  int buttonPress = 0;
  int pressType   = 0;
  buttonPress = digitalRead(inPin);

  if (buttonPress == LOW)
  {
    // button is press

    // button need to be released to count again
    if (buttonPressed == 0)
    {
      Serial.println("Button press");
      buttonCounter++;
      Serial.print("Count : "); Serial.println(buttonCounter);
      delay(100);
      if (buttonCounter > 3)
      {
        pressType = 2; // long press > 3sec
        buttonCounter = 0;
        buttonPressed = 1;
      }
      else
      {
        pressType = 3; // pressing
      }
    }
  }
  else
  {
    if (buttonCounter > 0)
    {
      // Short press
      Serial.println("Reset counter");
      buttonCounter = 0;
      pressType = 1;
    }
    buttonPressed = 0;
  }

  return pressType;
}


// ********************************************
// EEPROM Functions
// ********************************************
String eepromRead(int startAddress, int bytes)
{
  String stringRead;
  for (int i = 0; i < bytes; ++i)
  {
    stringRead += char(EEPROM.read(startAddress + i));
  }

  if (DEBUG)
  {
    Serial.print("Debug eeprom: read address : "); Serial.print(startAddress); Serial.print(" - string : "); Serial.println(stringRead);
  }
  return stringRead;
}

int eepromWrite(int startAddress, String stringToWrite)
{
  for (int i = 0; i < stringToWrite.length(); ++i)
  {
    EEPROM.write(startAddress + i, stringToWrite[i]);
  }
  EEPROM.commit();

  if (DEBUG)
  {
    Serial.print("Debug eeprom: write address : "); Serial.print(startAddress); Serial.print(" - string : "); Serial.println(stringToWrite);
  }
  return 1;
}
int eepromClear(int startAddress, int clearSize)
{
  for (int i = 0; i < clearSize; ++i)
  {
    EEPROM.write(startAddress + i, 0);
  }
  EEPROM.commit();

  return 1;
}
int eepromReadInt(int address) {
  int value = 0x0000;
  value = value | (EEPROM.read(address) << 8);
  value = value | EEPROM.read(address + 1);
  return value;
}

void eepromWriteInt(int address, int value) {
  EEPROM.write(address, (value >> 8) & 0xFF );
  EEPROM.write(address + 1, value & 0xFF);
  EEPROM.commit();
}

float eepromReadFloat(int address) {
  union u_tag {
    byte b[4];
    float fval;
  } u;
  u.b[0] = EEPROM.read(address);
  u.b[1] = EEPROM.read(address + 1);
  u.b[2] = EEPROM.read(address + 2);
  u.b[3] = EEPROM.read(address + 3);
  return u.fval;
}

void eepromWriteFloat(int address, float value) {
  union u_tag {
    byte b[4];
    float fval;
  } u;
  u.fval = value;

  EEPROM.write(address  , u.b[0]);
  EEPROM.write(address + 1, u.b[1]);
  EEPROM.write(address + 2, u.b[2]);
  EEPROM.write(address + 3, u.b[3]);

  EEPROM.commit();
}

void eepromReadString(int offset, int bytes, char *buf) {
  char c = 0;
  for (int i = offset; i < (offset + bytes); i++) {
    c = EEPROM.read(i);
    buf[i - offset] = c;
    if (c == 0) break;
  }
}

void eepromWriteString(int offset, int bytes, char *buf) {
  char c = 0;
  for (int i = 0; i < bytes; i++) {
    c = buf[i];
    EEPROM.write(offset + i, c);
  }
  EEPROM.commit();
}

// ********************************************
// SPIFFFS storage Functions
// ********************************************
String storageRead(char *fileName)
{
  String dataText;

  File file = SPIFFS.open(fileName, "r");
  if (!file)
  {
    logger("FS: opening file error");
    //-- debug
  }
  else
  {
    size_t sizeFile = file.size();
    if (sizeFile > 200)
    {
      Serial.println("Size of file is too clarge");
    }
    else
    {
      dataText = file.readString();
      file.close();
    }
  }

  return dataText;
}

bool storageWrite(char *fileName, String dataText)
{
  File file = SPIFFS.open(fileName, "w");
  file.println(dataText);

  file.close();

  return true;
}


void consumptionSave()
{
  dataJsonConsumption = "";
  serializeJson(jsonConsumption, dataJsonConsumption);

  if (DEBUG) {
    Serial.print("write consumption : ");
    Serial.println(dataJsonConsumption);
  }

  storageWrite("/consumption.json", dataJsonConsumption);
}

void configSave()
{
  dataJsonConfig = "";
  serializeJson(jsonConfig, dataJsonConfig);

  if (DEBUG) 
  {
    String testLau = jsonConfig["wifi"]["password"];
    Serial.print("jsonConfigWifiPassword : ");Serial.println(testLau);
    Serial.print("write config : ");
    Serial.println(dataJsonConfig);
  }

  storageWrite("/config.json", dataJsonConfig);
}




// ********************************************
// Time and Stats Functions
// ********************************************
long getTime()
{
  // get mili from begining
  long timeInSec = timeAtStarting + millis() / 1000; // work in sec instead of milisec

  return timeInSec;
}

long getTimeOnStartup()
{
  // send NTP request. or get a webpage with a timestamp

  HTTPClient httpClient;
  httpClient.begin("http://mangue.net/ota/esp/bicoqueEvse/timestamp.php");
  int httpAnswerCode = httpClient.GET();
  String timestamp;
  if (httpAnswerCode > 0)
  {
    timestamp = httpClient.getString();
  }
  httpClient.end();

  // global variable
  timeAtStarting = timestamp.toInt() - (millis() / 1000);

}




// ********************************************
// SCREEN Functions
// ********************************************

void screenDefault(int page)
{
  Serial.println("enter screen default");

  lcd.clear();
  lcd.home();

  String statusLine3;
  String statusLine1;
  String statusLine2;

  Serial.print("evseStatus : "); Serial.println(evseStatus);

  switch (evseStatus)
  {
    case 0:
      statusLine1 = "                   "; statusLine2 = " EVSE not connected"; statusLine3 = "";
      break;
    case 1:
      statusLine1 = " ---- bicoque ---- "; statusLine2 = "Station de recharge "; statusLine3 = "  Attente vehicule";
      break;
    case 2:
      statusLine1 = "  Vehicule present";
      if (evseAutoStart == 1)
      {
        statusLine2 = "                   "; statusLine3 = "attente voiture";
      }
      else
      {
        statusLine2 = " Lancer la charge ?"; statusLine3 = "          Long press";
      }
      break;
    case 3:
      statusLine1 = "   Chargement..."; statusLine2 = "Arreter la charge ?"; statusLine3 = "          Long press";
      break;
    case 4:
      statusLine1 = "   Chargement..."; statusLine2 = "Arreter la charge ?"; statusLine3 = "          Long press";
      break;
  }


  switch (page)
  {
    case 0:
      lcd.setCursor(0, 0);
      lcd.print(statusLine1);
      lcd.setCursor(0, 2);
      lcd.print(statusLine2);
      lcd.setCursor(0, 3);
      lcd.print(statusLine3);
      break;
    case 1:
      lcd.setCursor(0, 0); lcd.print("Wifi Power : "); lcd.print(WiFi.RSSI());
      lcd.setCursor(0, 1); lcd.print("IP: "); lcd.print(ip);
      lcd.setCursor(0, 2); lcd.print("Amps : "); lcd.print(evsePowerOnLimit); lcd.print("A / "); lcd.print(evseCurrentLimit); lcd.print("A");
      lcd.setCursor(0, 3); lcd.print("Conso: "); lcd.print(int(consumptionTotal / 1000)); lcd.print("kWh");
      break;
    case 2:
      lcd.setCursor(0, 2); lcd.print("   SETTINGS");
      break;
  }
}

// ********************************************
// WebServer
// ********************************************
void webRoot() {
  String message = "<!DOCTYPE HTML>";
  message += "<html>";

  message += "Bicoque EVSE v";
  message += SOFT_VERSION;
  message += " <a href='/reload'>reload</a> ";
  message += "<br><br>";

  message += "Hardware Limit : "; message += evseHardwareLimit; message += "A<br>";
  message += "On power on Limit : "; message += evsePowerOnLimit; message += "A<br>";
  message += "Actual Limit : "; message += evseCurrentLimit; message += "A<br>";
  message += "EVSE status : ";
  message += evseStatusName[ evseStatus ];
  message += "<br><br>";

  message += "Wifi power : "; message += WiFi.RSSI(); message += "<br>";
  message += "Wifi power : "; message += wifiPower(); message += "<br>";
  message += "<br>";

  message += "</html>";
  server.send(200, "text/html", message);
}
void webDebug()
{
  // get all infos from registers
  evseAllInfo();

  String message = "<!DOCTYPE HTML>";
  message += "<html>";

  message += "Bicoque EVSE - debug -<a href='/reload'>reload</a> ";
  message += "<br><br>\n";

  int itemsToPrint = NUMITEMS(evseRegisters);
  for ( int i = 0 ; i < itemsToPrint ; i++)
  {
    message += evseRegisters[i]; message += "<br>\n";
  }
  message += "<br>\n";

  message += "<form action='/write' method='GET'>Amperage (0-80): <input type=text name=amperage><input type=submit></form><br>\n";
  message += "<form action='/write' method='GET'>Modbus (0-1) : <input type=text name=modbus><input type=submit></form><br>\n";
  message += "<form action='/write' method='GET'>Raw write : Register : <input type=text name=register> / Value : <input type=text name=value><input type=submit></form><br>\n";
  message += "<form action='/write' method='GET'>Start Charging: <input type=hidden name=chargeon value=yes><input type=submit></form><br>\n";
  message += "<form action='/write' method='GET'>Stop Charging: <input type=hidden name=chargeoff value=yes><input type=submit></form><br>\n";
  message += "<br><br>\n";
  message += "<a href='/reboot'>Rebbot device</a><br>\n";

  message += "</html>\n";
  server.send(200, "text/html", message);
}
void webJsonInfo()
{

  long timestamp = int(millis() / 1000);

  String message = "{\n  \"version\": \""; message += SOFT_VERSION; message += "\",\n";
  //message += "  \"\": \""; message += ; message += "\",\n";

  message += "  \"powerOn\": \""; message += evsePowerOnLimit; message += "\",\n";
  message += "  \"currentLimit\": \""; message += evseCurrentLimit; message += "\",\n";
  message += "  \"consumptionLastCharge\": \""; message += consumptionLastCharge ; message += "\",\n";
  message += "  \"consumptionLastTime\": \""; message += consumptionLastTime ; message += "\",\n";
  message += "  \"consumptionCounter\": \""; message += consumptionCounter ; message += "\",\n";
  message += "  \"consumptions\": \""; message += consumptionTotal ; message += "\",\n";
  message += "  \"consumptionActual\": \""; message += consumptionActual ; message += "\",\n";
  message += "  \"wifiSignal\": \""; message += WiFi.RSSI(); message += "\",\n";
  message += "  \"wifiPower\": \""; message += wifiPower() ; message += "\",\n";
  message += "  \"uptime\": \""; message += timestamp ; message += "\",\n";
  message += "  \"time\": \""; message += timestamp + timeAtStarting ; message += "\",\n";
  message += "  \"statusName\": \""; message += evseStatusName[ evseStatus ] ; message += "\",\n";
  message += "  \"enable\": \""; message += evseEnable ; message += "\",\n";


  message += "  \"status\": \""; message += evseStatus; message += "\"\n";
  message += "}";

  server.send(200, "application/json", message);
}
void webReboot()
{
  String message = "<!DOCTYPE HTML>";
  message += "<html>";
  message += "Rebbot in progress...<b>";
  message += "</html>\n";
  server.send(200, "text/html", message);

  ESP.restart();
}

void webApiStatus()
{
  String message = "{\n";

  if ( server.method() == HTTP_GET )
  {
    message += "  \"status\": \""; message += evseEnable ; message += "\",\n";
    message += "  \"statusCar\": \""; message += evseStatusName[ evseStatus ] ; message += "\"\n}\n";
  }
  else
  {
    // For POST
    DeserializationError error = deserializeJson(jsonBuffer, server.arg(0));
    if (error)
    {
      message += "\"message\": \"JSON parsing failed!\"\n}\n";
    }
    else
    {
      String actionValue = jsonBuffer["action"];
      if (actionValue == "on")
      {
        evseWrite("evseStatus", EVSE_ACTIVE);
        message += "  \"msg\": \"Write on done : "; message += actionValue ; message += "\",\n";
      }
      if (actionValue == "off")
      {
        evseWrite("evseStatus", EVSE_DISACTIVE);
        message += "  \"msg\": \"Write off done : "; message += actionValue ; message += "\",\n";
      }
      message += "  \"action\": \""; message += actionValue ; message += "\"\n}\n";
    }
  }

  server.send(200, "application/json", message);
}

void webApiPower()
{
  String message = "{\n";

  if ( server.method() == HTTP_GET )
  {
    message += "  \"status\": \""; message += evseCurrentLimit ; message += "\"\n}\n";
  }
  else
  {
    // For POST
    DeserializationError error = deserializeJson(jsonBuffer, server.arg(0));
    if (error)
    {
      message += "\"message\": \"JSON parsing failed!\"\n}\n";
    }
    else
    {
      int actionValue = jsonBuffer["action"];
      evseUpdatePower(actionValue, NORMAL_STATE);
      message += "  \"action\": \""; message += actionValue ; message += "\"\n}\n";
    }
  }

  server.send(200, "application/json", message);
}


void webApiConfig()
{

  server.send(200, "application/json", dataJsonConfig);

}





void webNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " Name: " + server.argName(i) + " - Value: " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}
void webReload()
{
  evseReload();
  webRoot();

}
void webWrite()
{
  String message;

  String chargeOn = server.arg("chargeon");
  String chargeOff = server.arg("chargeoff");
  String amp = server.arg("amperage");
  String modbus = server.arg("modbus");
  String qstatus = server.arg("status");
  String registerNumber = server.arg("register");
  String value = server.arg("value");
  String clearAll = server.arg("clearall");
  String eepromWrite = server.arg("eepromwrite");
  String eepromRead = server.arg("eepromread");
  String autoStart = server.arg("autostart");
  String wifiEnable = server.arg("wifienable");
  String alreadyBoot = server.arg("alreadyboot");


  if (autoStart != "")
  {
    jsonConfig["evse"]["autostart"] = autoStart;
    configSave();
  }

  if ( wifiEnable != "")
  {
    jsonConfig["wifi"]["enable"] = wifiEnable;
    configSave();
  }

  if ( alreadyBoot != "")
  {
    jsonConfig["alreadyBoot"] = alreadyBoot;
    configSave();
  }


  if (amp != "")
  {
    message += "amp found\n";
    int tempAmp = amp.toInt();
    message += "amp = ";
    message += tempAmp;
    message += "\n";
    message += "Update Power in normal state\n";
    evseUpdatePower(tempAmp, NORMAL_STATE);
    //evseWrite("powerOn", amp.toInt());
  }

  if (modbus != "")
  {
    message += "modbus found : -"; message += modbus; message += "-\n";
    evseWrite("modbus", modbus.toInt());
  }
  if (qstatus != "")
  {
    message += "qstatus found : -"; message += qstatus; message += "-\n";
    evseStatus = qstatus.toInt();
    Serial.print("Set evseStatus to : "); Serial.println(evseStatus);
  }
  if (chargeOn != "")
  {
    message += "chargeOn found : -"; message += chargeOn; message += "-\n";
    //evseUpdatePower(evsePowerOnLimit, FORCE_CURRENT);
    evseWrite("evseStatus", EVSE_ACTIVE);
  }
  if (chargeOff != "")
  {
    message += "chargeOff found : -"; message += chargeOff; message += "-\n";
    //evseUpdatePower(0, FORCE_CURRENT);
    evseWrite("evseStatus", EVSE_DISACTIVE);
  }
  if (registerNumber != "" && value != "")
  {
    message += "registerNumber found : -"; message += registerNumber ; message += "-\n";
    node.setTransmitBuffer(0, value.toInt());
    node.writeMultipleRegisters(registerNumber.toInt(), 1);
  }

  if (clearAll == "yes")
  {
    message += "clear all stats. removed";
  }

  if (eepromWrite != "")
  {
    eepromWriteInt(eepromWrite.toInt(), 12345);
    message += "write : 12345 to eepaom "; message += eepromWrite ; message += "\n";
  }
  if (eepromRead != "")
  {
    int result = eepromReadInt(eepromRead.toInt());
    message += "read memory : "; message += eepromRead ; message += " - result : "; message += result ; message += "\n";
  }

  Serial.println("Write done");
  message += "Write done...\n";
  server.send(200, "text/plain", message);
}
void webInitRoot()
{
  String wifiList = wifiScan();
  String message = "<!DOCTYPE HTML>";
  message += "<html>";

  message += "Bicoque EVSE";
  message += "<br><br>\n";

  message += "Please configure your Wifi : <br>\n";
  message += "<p>\n";
  message += wifiList;
  message += "</p>\n<form method='get' action='setting'><label>SSID: </label><input name='ssid' length=32><input name='pass' length=64><input type='submit'></form>\n";
  message += "</html>\n";
  server.send(200, "text/html", message);
}
void webInitSetting()
{
  String content;
  int statusCode;
  String qsid = server.arg("ssid");
  String qpass = server.arg("pass");
  if (qsid.length() > 0 && qpass.length() > 0)
  {
    Serial.print("Debug : qsid : ");Serial.println(qsid);
    Serial.print("Debug : qpass : ");Serial.println(qpass);
    jsonConfig["wifi"]["ssid"] = qsid;
    configSave();
    jsonConfig["wifi"]["password"] = qpass;
    configSave();
    jsonConfig["wifi"]["enable"] = 1;
    configSave();

    content = "{\"Success\":\"saved to eeprom... reset to boot into new wifi\"}";
    statusCode = 200;
  }
  else
  {
    content = "{\"Error\":\"404 not found\"}";
    statusCode = 404;
    Serial.println("Sending 404");
  }
  server.send(statusCode, "application/json", content);
}


// Wifi
bool wifiConnect(const char* ssid, const char* password)
{
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  int c = 0;
  Serial.println("Waiting for Wifi to connect");
  while ( c < 20 ) {
    if (WiFi.status() == WL_CONNECTED) {
      return true;
    }
    delay(500);
    Serial.print(WiFi.status());
    c++;
  }
  Serial.println("");
  Serial.println("Connect timed out, opening AP");
  return false;
}
String wifiScan(void)
{
  String st;
  int n = WiFi.scanNetworks();
  Serial.println("scan done");
  if (n == 0)
    Serial.println("no networks found");
  else
  {
    Serial.print(n);
    Serial.println(" networks found");
    st = "<ol>";
    for (int i = 0; i < n; ++i)
    {
      // Print SSID and RSSI for each network found
      st += "<li>";
      st += WiFi.SSID(i);
      st += " (";
      st += WiFi.RSSI(i);
      st += ")";
      st += (WiFi.encryptionType(i) == ENC_TYPE_NONE) ? " " : "*";
      st += "</li>";
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.print(WiFi.SSID(i));
      Serial.print(" (");
      Serial.print(WiFi.RSSI(i));
      Serial.print(")");
      Serial.println((WiFi.encryptionType(i) == ENC_TYPE_NONE) ? " " : "*");
      delay(10);
    }
    st += "</ol>";
  }
  Serial.println("");
  delay(100);

  return st;
}
void wifiReset()
{

  jsonConfig["wifi"]["ssid"]     = "";
  jsonConfig["wifi"]["password"] = "";
  configSave();

  WiFi.mode(WIFI_OFF);
  WiFi.disconnect();
}
int wifiPower()
{
  int dBm = WiFi.RSSI();
  int quality;
  // dBm to Quality:
  if (dBm <= -100)
  {
    quality = 0;
  }
  else if (dBm >= -50)
  {
    quality = 100;
  }
  else
  {
    quality = 2 * (dBm + 100);
  }

  return quality;
}



String urlencode(String str)
{
  String encodedString = "";
  char c;
  char code0;
  char code1;
  for (int i = 0; i < str.length(); i++)
  {
    c = str.charAt(i);
    if (c == ' ')
    {
      encodedString += '+';
    }
    else if (isalnum(c))
    {
      encodedString += c;
    }
    else
    {
      code1 = (c & 0xf) + '0';
      if ((c & 0xf) > 9)
      {
        code1 = (c & 0xf) - 10 + 'A';
      }
      c = (c >> 4) & 0xf;
      code0 = c + '0';
      if (c > 9)
      {
        code0 = c - 10 + 'A';
      }
      encodedString += '%';
      encodedString += code0;
      encodedString += code1;
    }
  }

  return encodedString;
}


void logger(String message)
{
  if (wifiEnable)
  {
    HTTPClient httpClient;
    String urlTemp = BASE_URL;
    urlTemp += "log.php?message=";
    urlTemp += urlencode(message);

    httpClient.begin(urlTemp);
    httpClient.GET();
    httpClient.end();
  }
}


void setup()
{

  Serial.begin(115200);
  delay(100);
  node.begin(9600);  // Modbus RTU
  delay(100);

  Serial.println("");
  Serial.print("Welcome to bicoqueEVSE - "); Serial.println(SOFT_VERSION);

  // relay Init
  if (relayOutput)
  {
    pinMode(relayOutput, OUTPUT);
    digitalWrite(relayOutput, LOW);
  }


  // Screen Init
  lcd.init();
  lcd.backlight();
  uint8_t bell[8]  = {0x4, 0xe, 0xe, 0xe, 0x1f, 0x0, 0x4};
  uint8_t note[8]  = {0x2, 0x3, 0x2, 0xe, 0x1e, 0xc, 0x0};
  uint8_t clock[8] = {0x0, 0xe, 0x15, 0x17, 0x11, 0xe, 0x0};
  uint8_t heart[8] = {0x0, 0xa, 0x1f, 0x1f, 0xe, 0x4, 0x0};
  uint8_t duck[8]  = {0x0, 0xc, 0x1d, 0xf, 0xf, 0x6, 0x0};
  uint8_t check[8] = {0x0, 0x1 , 0x3, 0x16, 0x1c, 0x8, 0x0};
  uint8_t cross[8] = {0x0, 0x1b, 0xe, 0x4, 0xe, 0x1b, 0x0};
  uint8_t retarrow[8] = {  0x1, 0x1, 0x5, 0x9, 0x1f, 0x8, 0x4};
  uint8_t arrow[8] = { 0x0, 0x4, 0x2, 0x1f, 0x2, 0x4 };
  lcd.createChar(0, arrow );
  //lcd.createChar(0, bell);
  lcd.createChar(1, note);
  lcd.createChar(2, clock);
  lcd.createChar(3, heart);
  lcd.createChar(4, duck);
  lcd.createChar(5, check);
  lcd.createChar(6, cross);
  lcd.createChar(7, retarrow);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Booting...");
  lcd.setCursor(11, 3);
  lcd.print("v"); lcd.print(SOFT_VERSION);

  // FileSystem
  if (SPIFFS.begin())
  {
    // check if we have et config file
    if (SPIFFS.exists("/config.json"))
    {
      Serial.println("Config.json found. read data");
      dataJsonConfig = storageRead("/config.json");
      Serial.println(dataJsonConfig);
      DeserializationError jsonError = deserializeJson(jsonConfig, dataJsonConfig);
      if (jsonError)
      {
        // Getting error when deserialise... I don know what to do here...
      }

      wifiEnable            = jsonConfig["wifi"]["enable"];
      String wifiSsidTemp   = jsonConfig["wifi"]["ssid"];
      String wifiPasswdTemp = jsonConfig["wifi"]["password"];
      wifiSsid              = wifiSsidTemp;
      wifiPasswd            = wifiPasswdTemp;
      evseAutoStart         = jsonConfig["evse"]["autostart"];
      alreadyBoot           = jsonConfig["alreadyBoot"];
      wifiEnable=1; //to remove // lau debug
    }
    else
    {
      // No config found.
      // Start in AP mode to configure
      // debug create object here
      Serial.println("Config.json not found. Create one");
      wifiEnable    = 0;
      alreadyBoot   = 0;
      wifiSsid      = "";
      wifiPasswd    = "";
      evseAutoStart = 0;

      jsonConfig["wifi"]["enable"]    = wifiEnable;
      jsonConfig["wifi"]["ssid"]      = wifiSsid;
      jsonConfig["wifi"]["password"]  = wifiPasswd;
      jsonConfig["evse"]["autostart"] = evseAutoStart;
      jsonConfig["alreadyBoot"]       = alreadyBoot;

      configSave();
    }


    if (SPIFFS.exists("/consumption.json"))
    {
      dataJsonConsumption = storageRead("/consumption.json");
      DeserializationError jsonError = deserializeJson(jsonConsumption, dataJsonConsumption);
      if (jsonError)
      {
        // Getting error when deserialise... I don know what to do here...
      }

      consumptionLastCharge = jsonConsumption["lastCharge"];
      consumptionTotal      = jsonConsumption["total"];
    }
    else
    {
      consumptionLastCharge  = 0;
      consumptionTotal       = 0;

      jsonConsumption["lastCharge"] = consumptionLastCharge;
      jsonConsumption["total"]      = consumptionTotal;

      consumptionSave();
    }
  }

  if (DEBUG)
  {
    Serial.println("Config info :");
    Serial.print("wifiEnable : "); Serial.println(wifiEnable);
    Serial.print("wifiSsid : '"); Serial.print(wifiSsid); Serial.println("'");
 //   Serial.print("  sizeof : "); Serial.println(wifiSsid.length);
    Serial.print("alreadyBoot : "); Serial.println(alreadyBoot);
    Serial.print("autoStart : "); Serial.println(evseAutoStart);
  }

  if (alreadyBoot == 0 && wifiEnable == 0)
  {
    wifiEnable = 1;
  }


  // Define Pins for button
  pinMode(inPin, INPUT_PULLUP);

  int wifiMode = 0;
  String wifiList;
  if (wifiEnable)
  {
    Serial.println("Enter wifi config");
    lcd.setCursor(1, 1);
    lcd.print("wifi settings");

    if (wifiSsid.length() > 0 and 1 == 0)
    {
      lcd.setCursor(1, 2);
      lcd.print("   connecting...");
      Serial.print("Wifi: Connecting to -");
      Serial.print(wifiSsid); Serial.println("-");

      // Connecting to wifi
      WiFi.mode(WIFI_AP);
      WiFi.disconnect();
      WiFi.mode(WIFI_STA);
      WiFi.hostname(wifiApSsid);

      const char * login = wifiSsid.c_str();
      const char * pass  = wifiPasswd.c_str();
      bool wifiConnected = wifiConnect(login, pass);

      if (wifiConnected)
      {
        Serial.println("WiFi connected");
        wifiMode = 1;

        lcd.print(" .. ok");
      }
      else
      {
        Serial.println("Can't connect to Wifi. disactive webserver");
        wifiEnable = 0;
      }
    }
    else
    {
      lcd.setCursor(1, 2);
      lcd.print("   standalone mode");
      WiFi.mode(WIFI_STA);
      WiFi.disconnect();
      WiFi.mode(WIFI_AP);
      WiFi.hostname(wifiApSsid);
      Serial.println("Wifi config not present. set AP mode");
      WiFi.softAP(wifiApSsid, wifiApPasswd, 6);
      Serial.println("softap");
      wifiMode = 2;

      lcd.print("  ko");
    }
  }
  else
  {
    Serial.println("Wifi desactivated");
    WiFi.mode(WIFI_OFF);

    lcd.print(" ..off");
    delay(2000);
  }

  Serial.println("End of wifi config");

  if (wifiEnable)
  {
    // Start the server
    lcd.setCursor(1, 1);
    lcd.print("load webserver");
    lcd.setCursor(1, 2);
    lcd.print("                 ");

    if (wifiMode == 1) // normal mode
    {
      server.on("/", webRoot);
      server.on("/reload", webReload);
      server.on("/write", webWrite);
      server.on("/debug", webDebug);
      server.on("/reboot", webReboot);
      server.on("/jsonInfo", webJsonInfo);
      server.on("/api/status", webApiStatus);
      server.on("/api/power", webApiPower);
      server.on("/api/config", webApiConfig);

      server.on("/setting", webInitSetting);
      ip = WiFi.localIP();
    }
    else if (wifiMode == 2) // AP mode for config
    {
      server.on("/", webInitRoot);
      server.on("/setting", webInitSetting);
      ip = WiFi.softAPIP();
    }
    server.onNotFound(webNotFound);
    server.begin();

    Serial.print("Http: Server started at http://");
    Serial.print(ip);
    Serial.println("/");
    Serial.print("Status : ");
    Serial.println(WiFi.RSSI());

    lcd.setCursor(1, 1);
    lcd.print("check update       ");
    delay(1000);
    lcd.noBacklight();

    String updateUrl = UPDATE_URL;
    Serial.println("Check for new update at : "); Serial.println(updateUrl);
    ESPhttpUpdate.rebootOnUpdate(1);
    t_httpUpdate_return ret = ESPhttpUpdate.update(updateUrl, SOFT_VERSION);
    //t_httpUpdate_return ret = ESPhttpUpdate.update(updateUrl, ESP.getSketchMD5() );

    Serial.print("return : "); Serial.println(ret);
    switch (ret) {
      case HTTP_UPDATE_FAILED:
        Serial.printf("HTTP_UPDATE_FAILED Error (%d): %s\n", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
        break;

      case HTTP_UPDATE_NO_UPDATES:
        Serial.println("HTTP_UPDATE_NO_UPDATES");
        break;

      case HTTP_UPDATE_OK:
        Serial.println("HTTP_UPDATE_OK");
        break;

      default:
        Serial.print("Undefined HTTP_UPDATE Code: "); Serial.println(ret);
    }
    lcd.backlight();
    lcd.setCursor(1, 1);
    lcd.print("update done  ");

    // get time();
    getTimeOnStartup();
    logger("Starting bicoqueEvse");
    String messageToLog = "ConsoTotalg : "; messageToLog += consumptionTotal; messageToLog += " - "; messageToLog += SOFT_VERSION ; messageToLog += " "; messageToLog += SOFT_DATE;
    logger(messageToLog);
  }

  lcd.setCursor(1, 1);
  lcd.print("init EVSE     ");
  // Read EVSE info
  evseWrite("minAmpsValue", 0);
  evseWrite("modbus", 1);
  evseReload();


  // Screen
  screenDefault(page);

  if (alreadyBoot == 0)
  {
    jsonConfig["alreadyBoot"] = 1;
    configSave();
    // eepromWrite(ALREADYBOOT, "1");
  }


  if (DEBUG)
  {
    Dir dir = SPIFFS.openDir("/");
    while (dir.next())
    {
      logger(dir.fileName());
      File f = dir.openFile("r");
      String messageToLog = "size:"; messageToLog += f.size();
      logger(messageToLog);
    }

    logger("Config : ");
    logger(dataJsonConfig);
  }

}


void loop()
{
  // put your main code here, to run repeatedly:


  // If we are in menu
  if (internalMode == 1)
  {
    // Menu
    Serial.println("Enter Menu");
    getMenu(0);

    buttonCounter = 0;
    internalMode = 2;
    delay(1000);
    return;
  }
  else if (internalMode == 2)
  {
    int pressButton = 0;
    pressButton = checkButton();
    if (pressButton == 1 || pressButton == 2)
    {
      Serial.print("Chck button : ");
      Serial.println(pressButton);
      menuTimerIdle = 0;
      getMenu(pressButton);
      if ( pressButton == 2 )
      {
        delay(300);
      }
    }
    else if (pressButton == 0)
    {
      menuTimerIdle++;
      if (menuTimerIdle > 600) // 60 sec
      {
        Serial.println("Exit menu from idle");
        internalMode = 3;
      }
      delay(100);
    }

    return;
  }
  else if (internalMode == 3)
  {
    Serial.println("Clear screen");
    internalMode = 0;
    menuTimerIdle = 0;
    sleepModeTimer = 0;

    screenDefault(page);

    return;
  }


  // if we are not in menu

  // Check Button
  int pressButton;
  pressButton = checkButton();
  if (pressButton > 0)
  {
    if (sleepMode == 1)
    {
      Serial.println("Set backlight to on");
      lcd.backlight();
      sleepMode = 0;
      return;
    }

    if (pressButton == 2)
    {
      if (page == 0)
      {
        if (evseStatus <= 1)
        {
          if (evseAutoStart == 1) // We are into autostart mode
          {
            // Go to the menu
            internalMode = 1;
          }
          else // In manuel mode
          {
            // so in long pressed button, need to launch the start
            evseWrite("evseStatus", EVSE_ACTIVE);
          }
        }
        else if (evseStatus == 2) // EV present
        {
          // never be in this case with the firmware we have. but if long press, set current to launch charging
          evseWrite("evseStatus", EVSE_ACTIVE);
        }
        else if (evseStatus >= 3) // Charging
        {
          // Need to stop charging.
          evseWrite("evseStatus", EVSE_DISACTIVE);
        }
      }
      else if (page == 2)
      {
        internalMode = 1;
      }

      return;
    }
    else if (pressButton == 1)
    {
      page++;
      if (page >= 3) {
        page = 0;
      }
      screenDefault(page);
    }
  }



  int evseStatusBackup = evseStatus;
  // Check EVSE
  if ( evseStatusCounter < 1 )
  {
    evseStatusCheck();
    evseStatusCounter = 20;

    // maybe a connection error
    if (evseStatus == 0)
    {
      //logger("EvseStatus%20is%20set%20to%200");
      // If this the first time we have a problem give 1 chance for next time
      // Maybe we can upgrade to more than 1 try
      if (evseStatusBackup != 0 and evseConnectionProblem < 5)
      {
        evseStatus = evseStatusBackup;
        evseConnectionProblem++;
      }
      else
      {
        evseStatusCounter = 200;
      }
    }
    else
    {
      evseConnectionProblem = 0;
    }
  }
  else
  {
    evseStatusCounter--;
  }

  // check web client connections
  if (wifiEnable)
  {
    server.handleClient();
  }


  // get stats for power consumption
  if (evseStatus >= 3)
  {

    if (consumptionLastTime == 0)
    {
      // need to decale all int
      consumptionLastTime = getTime();
      statsLastWrite      = consumptionLastTime;
    }
    else
    {
      consumptionActual   = evseCurrentLimit * AC_POWER ;

      long timestamp = getTime(); // We work in seconds
      float comsumptionInterval = (consumptionActual * ( timestamp - consumptionLastTime ) / 3600.00);

      // To always display the last charge consumption
      if (consumptionLastChargeRunning == 0)
      {
        consumptionLastChargeRunning  = 1;
        consumptionLastCharge         = 0;
      }
      consumptionCounter           += comsumptionInterval;
      consumptionTotal             += int(comsumptionInterval + consumptionTotalTemp);
      consumptionTotalTemp          = comsumptionInterval + consumptionTotalTemp - int(comsumptionInterval + consumptionTotalTemp);
      consumptionLastCharge        += int(comsumptionInterval + consumptionTotalTemp);

      // do not write to often. Life of EEPROM is 100K commit.
      // Write every 10mins
      if ( (timestamp - statsLastWrite) > 600 )
      {
        // long consumptionToWrite      = int(consumptionTotal/1000);
        // long consumptionToWriteCents = consumptionTotal - (consumptionToWrite * 1000);

        jsonConsumption["lastCharge"] = consumptionLastCharge;
        jsonConsumption["total"]      = consumptionTotal;
        consumptionSave();

        statsLastWrite = timestamp;

        if (DEBUG)
        {
          String messageToLog = "Write.In.Memory.periodic : "; messageToLog += consumptionTotal; messageToLog += "Wh";
          logger(messageToLog);
        }
      }

      consumptionLastTime = timestamp;
    }
  }
  else
  {
    // EVSE Stop charging
    if (evseStatusBackup >= 3)
    {
      consumptionActual   = evseCurrentLimit * AC_POWER ;

      long timestamp = getTime(); // We work in seconds
      float comsumptionInterval = (consumptionActual * ( timestamp - consumptionLastTime ) / 3600.00);

      //consumptions[0] += int(consumptionCounter);
      consumptionTotal    += int(comsumptionInterval + consumptionTotalTemp);
      consumptionCounter   = 0;
      consumptionActual    = 0;
      consumptionLastTime  = 0;
      statsLastWrite       = 0;
      consumptionTotalTemp = 0;

      jsonConsumption["LastCharge"] = consumptionLastCharge;
      jsonConsumption["total"]      = consumptionTotal;
      consumptionSave();

      // long consumptionToWrite      = int(consumptionTotal/1000);
      // long consumptionToWriteCents = consumptionTotal - (consumptionToWrite * 1000);

      String messageToLog = "Write.In.Memory.stop.charging : "; messageToLog += consumptionTotal; messageToLog += "Wh ";
      logger(messageToLog);
    }
  }




  if (evseStatusBackup != evseStatus)
  {

    // if connection with evse was down
    if (evseStatusBackup == 0)
    {
      evseConnectionProblem = 0;
    }

    String messageToLog = "Changing state from ";
    messageToLog += evseStatusBackup;
    messageToLog += " to ";
    messageToLog += evseStatus;
    logger(messageToLog);

    // only remove the power from cable if we unplug the cable.
    // We need it if the car got a programming function to heater or cold the car.
    if (evseStatus == 1)
    {
      consumptionLastChargeRunning = 0;
      evseUpdatePower(evsePowerOnLimit, NORMAL_STATE); // If we force stoping or starting charging EV and autostart is on or off. this things permit to reset value to the normal
      //evseWrite("evseStatus", "disactive");
    }

    screenDefault(0);

    // awake from sleep mode
    if (sleepMode == 1)
    {
      sleepMode = 0;
      lcd.backlight();
    }
  }




  if (sleepMode == 0)
  {
    sleepModeTimer++;
    if (sleepModeTimer > 1200) // 120 sec
    {
      Serial.println("shutdown light on screen");
      lcd.noBacklight();
      sleepModeTimer = 0;
      sleepMode = 1;

    }
  }

  delay(100); // Wait 1 sec if we are not in sleepMode

}
