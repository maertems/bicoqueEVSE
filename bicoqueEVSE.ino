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

// NTP
#include <NTPClient.h>
#include <WiFiUdp.h>



// Instantiate ModbusMaster object as slave ID 1
ModbusMaster232 node(1);
// ModBus is a complet new lib. add sofwareserial to use serial1 Corresponding to RX0(GPIO3) and TX0(GPIO1) in board

// firmware version
#define SOFT_NAME "bicoqueEVSE"
#define SOFT_VERSION "1.5.10"
#define SOFT_DATE "2025-03-30"
#define EVSE_VERSION 10

#define DEBUG 1

// Wifi AP info for configuration
const char* wifiApSsid = "bicoqueEVSE";
bool networkEnable      = 1;
bool internetConnection = 0;
int wifiActivationTempo = 1;

// Update info
#define BASE_URL "http://esp.bicoque.com/" SOFT_NAME "/"
#define UPDATE_URL BASE_URL "update.php"

// Web server info
ESP8266WebServer server(80);
IPAddress ip;


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
#define EVSE_ACTIVE 0
#define EVSE_DISACTIVE 1

// Json allocation
//-- For all http API paramerters
StaticJsonDocument<200> jsonBuffer;

String dataJsonConsumption;
DynamicJsonDocument jsonConsumption(200);

// For timers
int timerPerHourLast     = 0;


// NTP Constant
#define NTP_SERVER "ntp.ovh.net"
#define NTP_TIME_ZONE 1         // GMT +1:00

// params WifiUDP object / ntp server / timeZone in sec / request ntp every xx milisec
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, NTP_SERVER , (NTP_TIME_ZONE * 3600) , 86400000);



// config default
typedef struct configWifiList
{
  String ssid;
  String password;
};
typedef struct configWifi 
{
  String ssid;
  String password;
  boolean enable;
  int prefered;
  int nextRecord;
  configWifiList list[10];
};
typedef struct configEvse 
{
  boolean autoStart;
};
typedef struct config
{
  configWifi wifi;
  configEvse evse;
  boolean alreadyStart;
  String softName;
  String softVersion;
};
config softConfig;


// Button switch
int inPin = 12; // D6
int buttonCounter = 0;
int buttonPressed = 0; //When a button just pressed, do not continue to thik it's pressed. Need to release button to restart counter


// Module
int internalMode   = 0; // 0: normal - 1: Not define - 2: menu
int menuTimerIdle  = 0;
int sleepModeTimer = 0;
int sleepMode      = 0;

// Screen LCD
// Using ports SDCLK / SDA / SCL / VCC +5V / GND
LiquidCrystal_I2C lcd(0x27, 20, 4); // set the LCD address to 0x27 for a 20 chars and 4 line display

// Relay install on v3.3 / GND / D5
int relayOutput = D5; // set to 0 if disactive





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




void wifiReset()
{
  //softConfig.wifi.ssid     = "";
  //softConfig.wifi.password = "";
  //configSave();

  WiFi.mode(WIFI_OFF);
  WiFi.disconnect();
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

int wifiPower()
{
  int dBm = WiFi.RSSI();
  int quality;
  // dBm to Quality:
  if (dBm <= -100)     { quality = 0; }
  else if (dBm >= -50) { quality = 100; }
  else                 { quality = 2 * (dBm + 100); }

  return quality;
}



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
    evseUpdatePower(tempValue.toInt());
  }
  else if (menuStatus >= 200 && menuStatus < 300) // AUTOSTART
  {
    menuStatus = 2;
    if ( strcmp(menuValue, enum201[1]) == 0)
    {
      softConfig.evse.autoStart = 1;
    }
    else if (strcmp(menuValue, enum201[2]) == 0)
    {
      softConfig.evse.autoStart = 0;
    }

    evseUpdatePower(evsePowerOnLimit);
    configSave();
  }
  else if (menuStatus >= 300 && menuStatus < 400)
  {
    menuStatus = 11;
    if ( strcmp(menuValue, enum301[1]) == 0) {
      softConfig.wifi.enable = 1;
    }
    else if ( strcmp(menuValue, enum301[2]) == 0)
    {
      softConfig.wifi.enable = 0;
      WiFi.mode(WIFI_OFF);
      WiFi.disconnect();
    }

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
      case 2: if (softConfig.evse.autoStart) {
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
        if (softConfig.wifi.enable) {
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
void evseUpdatePower(int power)
{

  evseWrite("currentLimit", power);
  evseCurrentLimit = power;
  
  evseWrite("powerOn", power);
  evsePowerOnLimit = power;
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

		   evseStatusCheck();
                   if (evseStatus < 3) // check that car is not charging
                   {
                     digitalWrite(relayOutput, HIGH);
                     delay(1000);
                     digitalWrite(relayOutput, LOW);
                     delay(1000);
                   }
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
    if (sizeFile > 1000)
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

String configSerialize()
{
  String dataJsonConfig;
  DynamicJsonDocument jsonConfig(800);
  JsonObject jsonConfigWifi     = jsonConfig.createNestedObject("wifi");
  JsonArray jsonConfigWifiList  = jsonConfigWifi.createNestedArray("list");;
  JsonObject jsonConfigEvse     = jsonConfig.createNestedObject("evse");
  

  jsonConfig["alreadyStart"]   = softConfig.alreadyStart;
  jsonConfig["softName"]       = softConfig.softName;
  jsonConfig["softVersion"]    = softConfig.softVersion;
  jsonConfigWifi["enable"]     = softConfig.wifi.enable;
  jsonConfigWifi["prefered"]   = softConfig.wifi.prefered;
  jsonConfigWifi["nextRecord"] = softConfig.wifi.nextRecord;
  jsonConfigEvse["autoStart"]  = softConfig.evse.autoStart;

  for (int i=0; i <10; i++)
  {
	JsonObject wifiInfo = jsonConfigWifiList.createNestedObject();
	wifiInfo["ssid"]     = softConfig.wifi.list[i].ssid; 
	wifiInfo["password"] = softConfig.wifi.list[i].password; 
  }

  serializeJson(jsonConfig, dataJsonConfig);

  return dataJsonConfig;
}


bool configSave()
{
  String dataJsonConfig = configSerialize();
  bool fnret = storageWrite("/config.json", dataJsonConfig);
  
  if (DEBUG) 
  {
    Serial.print("write config : ");
    Serial.println(dataJsonConfig);
    Serial.print("Return of write : "); Serial.println(fnret);
  }

  return fnret;
}

bool configRead(config &ConfigTemp, char *fileName )
{
  String dataJsonConfig;
  DynamicJsonDocument jsonConfig(800);
  
  dataJsonConfig = storageRead("/config.json");
  
  DeserializationError jsonError = deserializeJson(jsonConfig, dataJsonConfig);
  if (jsonError)
  {
    Serial.println("Got Error when deserialization : "); //Serial.println(jsonError);
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(jsonError.c_str());
    return false;
    // Getting error when deserialise... I don know what to do here...
  }

  ConfigTemp.alreadyStart    = jsonConfig["alreadyStart"];
  ConfigTemp.softName        = jsonConfig["softName"].as<String>();
  ConfigTemp.softVersion     = jsonConfig["softVersion"].as<String>();
  ConfigTemp.wifi.enable     = jsonConfig["wifi"]["enable"];
  ConfigTemp.wifi.prefered   = jsonConfig["wifi"]["prefered"];
  ConfigTemp.wifi.nextRecord = jsonConfig["wifi"]["nextRecord"];
  ConfigTemp.evse.autoStart  = jsonConfig["evse"]["autoStart"];

  for (int i=0; i <10; i++)
  {
        softConfig.wifi.list[i].ssid     = jsonConfig["wifi"]["list"][i]["ssid"].as<String>(); 
        softConfig.wifi.list[i].password = jsonConfig["wifi"]["list"][i]["password"].as<String>(); 
  }

  return true;
  
}

void configDump(config ConfigTemp)
{
  Serial.println("wifi data :");
  Serial.print("  - enable : "); Serial.println(ConfigTemp.wifi.enable);
  Serial.print("  - prefered : "); Serial.println(ConfigTemp.wifi.prefered);
  Serial.print("  - nextRecord : "); Serial.println(ConfigTemp.wifi.nextRecord);
  Serial.println("   - list:");
  for (int i=0; i <10; i++)
  {
    Serial.print("    - "); Serial.print(softConfig.wifi.list[i].ssid); Serial.print(" / "); Serial.println(softConfig.wifi.list[i].password);
  }
  Serial.println("evse data :");
  Serial.print("  - autostart : "); Serial.println(ConfigTemp.evse.autoStart);
  Serial.println("General data :");
  Serial.print("  - alreadyStart : "); Serial.println(ConfigTemp.alreadyStart);
  Serial.print("  - softName : "); Serial.println(ConfigTemp.softName);
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
      if (softConfig.evse.autoStart == 1)
      {
        statusLine2 = "                   "; statusLine3 = "     attente prog EV";
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
      lcd.setCursor(0, 2); lcd.print("Amps : "); lcd.print(evseCurrentLimit); lcd.print("A");
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

  message += "  \"powerHardwareLimit\": \""; message += evseHardwareLimit; message += "\",\n";
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
  message += "  \"time\": \""; message += timeClient.getEpochTime() ; message += "\",\n";
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
  String message = "{";

  String statusToSend = "on";
  if (evseEnable == 0)
  {
    statusToSend = "off";
  }


  if ( server.method() == HTTP_GET )
  {
    message += "\"value\":\""; message += statusToSend ; message += "\",";
    message += "\"statusCar\":\""; message += evseStatusName[ evseStatus ] ; message += "\"}";
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
      evseUpdatePower(actionValue);
      message += "  \"action\": \""; message += actionValue ; message += "\"\n}\n";
    }
  }

  server.send(200, "application/json", message);
}


void webApiConfig()
{
  String dataJsonConfig = configSerialize();
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

  String chargeOn       = server.arg("chargeon");
  String chargeOff      = server.arg("chargeoff");
  String amp            = server.arg("amperage");
  String modbus         = server.arg("modbus");
  String qstatus        = server.arg("status");
  String registerNumber = server.arg("register");
  String value          = server.arg("value");
  String autoStart      = server.arg("autostart");
  String wifiEnable     = server.arg("wifienable");
  String alreadyBoot    = server.arg("alreadyboot");
  String relay          = server.arg("relay");

  String clearAll       = server.arg("clearall");
  String setConsumption = server.arg("setConsumption");


  if (autoStart != "")
  {
    softConfig.evse.autoStart = false;
    if (autoStart == "1") { softConfig.evse.autoStart = true; }
    configSave();
  }

  if ( wifiEnable != "")
  {
    softConfig.wifi.enable = false;
    if ( wifiEnable == "1") { softConfig.wifi.enable = true; }
    configSave();
  }

  if ( alreadyBoot != "")
  {
    softConfig.alreadyStart = false;
    if ( alreadyBoot == "1") { softConfig.alreadyStart = true; }
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
    evseUpdatePower(tempAmp);
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
    evseWrite("evseStatus", EVSE_ACTIVE);
  }
  if (chargeOff != "")
  {
    message += "chargeOff found : -"; message += chargeOff; message += "-\n";
    evseWrite("evseStatus", EVSE_DISACTIVE);
  }
  if (registerNumber != "" && value != "")
  {
    message += "registerNumber found : -"; message += registerNumber ; message += "-\n";
    node.setTransmitBuffer(0, value.toInt());
    node.writeMultipleRegisters(registerNumber.toInt(), 1);
  }

  if (relay != "")
  {
    message += "relay action found : -"; message += relay ; message += "-\n";
    if (relayOutput)
    {
      if (relay == "off")
      {
      	digitalWrite(relayOutput, HIGH);
      }
      else if (relay == "on")
      {
      	digitalWrite(relayOutput, LOW);
      }
      else
      {
      	digitalWrite(relayOutput, HIGH);
        delay(500);
      	digitalWrite(relayOutput, HIGH);
        delay(500);
      	digitalWrite(relayOutput, HIGH);
        delay(500);
      	digitalWrite(relayOutput, HIGH);
        delay(500);

        digitalWrite(relayOutput, LOW);
        delay(500);
        digitalWrite(relayOutput, LOW);
        delay(500);
        digitalWrite(relayOutput, LOW);
        delay(500);
        digitalWrite(relayOutput, LOW);
        delay(500);
      }
    }
  }


  if (clearAll == "yes")
  {
    message += "clear all stats. removed";
  }

  if (setConsumption != "")
  {
    message += "Consumption set to "; message += setConsumption ; message += "-\n";
    jsonConsumption["lastCharge"] = consumptionLastCharge;
    jsonConsumption["total"]      = setConsumption;
    consumptionSave(); 
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
    softConfig.wifi.list[softConfig.wifi.nextRecord ].ssid     = qsid;
    softConfig.wifi.list[softConfig.wifi.nextRecord ].password = qpass;
    softConfig.wifi.prefered                                   = softConfig.wifi.nextRecord;
    softConfig.wifi.nextRecord ++;
    if (softConfig.wifi.nextRecord >= 10) { softConfig.wifi.nextRecord = 0; }
    softConfig.wifi.enable   = 1;
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


void web_template()
{

  String index_html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link rel="stylesheet" href="https://maxcdn.bootstrapcdn.com/bootstrap/3.4.1/css/bootstrap.min.css">
  <script src="https://ajax.googleapis.com/ajax/libs/jquery/3.5.1/jquery.min.js"></script>
  <script src="https://maxcdn.bootstrapcdn.com/bootstrap/3.4.1/js/bootstrap.min.js"></script>

  <link rel="stylesheet" href="https://use.fontawesome.com/releases/v5.7.2/css/all.css" integrity="sha384-fnmOCqbTlWIlj8LyTjo7mOUStjsKC4pOpQbqyi7RrhN7udi9RwhKkMHpvLbHG9Sr" crossorigin="anonymous">

</head>
<body>

<div class="container" style="width: 80%; margin: auto;max-width: 564px">
  <h2>bicoqueEVSE - configuration</h2>

</div>
</body>

<script>
function getData()
{
  var xhttp = new XMLHttpRequest();

  xhttp.onreadystatechange = function()
  {
    if (this.readyState == 4 && this.status == 200)
    {
      const obj = JSON.parse(this.responseText);
      // console.log(obj.version);
      document.getElementById("hw_current").innerHTML = obj.powerHardwareLimit;

    }
  };
  xhttp.open("GET", "/jsonInfo", true);
  xhttp.send();
}

getData();
setInterval(getData, 10000);

</script>
</html>
)rawliteral";

  server.send(200, "text/html", index_html);
}




void web_config()
{

  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link rel="stylesheet" href="https://maxcdn.bootstrapcdn.com/bootstrap/3.4.1/css/bootstrap.min.css">
  <script src="https://ajax.googleapis.com/ajax/libs/jquery/3.5.1/jquery.min.js"></script>
  <script src="https://maxcdn.bootstrapcdn.com/bootstrap/3.4.1/js/bootstrap.min.js"></script>

  <link rel="stylesheet" href="https://use.fontawesome.com/releases/v5.7.2/css/all.css" integrity="sha384-fnmOCqbTlWIlj8LyTjo7mOUStjsKC4pOpQbqyi7RrhN7udi9RwhKkMHpvLbHG9Sr" crossorigin="anonymous">

</head>
<body>

<div class="container" style="width: 80%; margin: auto;max-width: 564px">
  <h2>bicoqueEVSE - configuration</h2>

    <p>
    <ul class="list-group">
      <li class="list-group-item">
        <p>Wifi</p>
        <p>
          <ul class="list-group">
          <li class="list-group-item">Enable<span class="pull-right"><input type="checkbox" data-toggle="toggle" id=wifi_enable onChange="wifiEnableUpdate()"></span></li>
          <li class="list-group-item">Ssid<span class="pull-right"><input type="text" value=ssid id=wifi_ssid></span></li>
          <li class="list-group-item">Password<span class="pull-right"><input type="text" value=password id=wifi_password></span></li>
          </ul>
        </p>
        <p align=right>
          <input type="button" value="update" onclick="wifiUpdate()">
        </p>
      </li>
      <li class="list-group-item">
        <p>EVSE</p>
        <p>
          <ul class="list-group">
          <li class="list-group-item">AutoStart<span class="pull-right"><input type="checkbox" data-toggle="toggle" id=evse_autostart onChange="evseAutostartUpdate()"></span></li>
          </ul>
        </p>
      </li>
      <li class="list-group-item">
        <p>Global</p>
        <p>
          <ul class="list-group">
          <li class="list-group-item">alreadyStart<span class="pull-right"><input type="checkbox" data-toggle="toggle" id=global_alreadyStart onChange="globalAlreadyStartUpdate()"></span></li>
          <li class="list-group-item">softName<span class="pull-right" id=global_softName></span></li>
          </ul>
        </p>
      </li>
    </ul>
    </p>
</div>
</body>



<script>
function getData()
{
  var xhttp = new XMLHttpRequest();

  xhttp.onreadystatechange = function()
  {
    if (this.readyState == 4 && this.status == 200)
    {
      const obj = JSON.parse(this.responseText);
      console.log(obj.wifi);
      //document.getElementById("wifi_ssid").innerHTML = obj.wifi.ssid;
      document.getElementById("wifi_ssid").value = obj.wifi.ssid;
      document.getElementById("wifi_password").value = obj.wifi.password;
      document.getElementById("wifi_enable").checked = obj.wifi.enable;

      document.getElementById("evse_autostart").checked = obj.evse.autoStart;

      document.getElementById("global_alreadyStart").checked = obj.alreadyStart;
      document.getElementById("global_softName").innerHTML = obj.softName;
    }
  };
  xhttp.open("GET", "/api/config", true);
  xhttp.send();
}

function wifiEnableUpdate()
{
  var xhr = new XMLHttpRequest();
  var url = "/write?wifienable=";

  if (document.getElementById("wifi_enable").checked == true)
  {
    url = url + "1";
  }
  else
  {
    url = url + "0";
  }
  xhr.onreadystatechange = function () {
    if (xhr.readyState === 4 && xhr.status === 200) {
        // console.log(xhr.responseText);
    }
  };

  xhr.open("GET", url, true);
  xhr.send();
}

function evseAutostartUpdate()
{
  var xhr = new XMLHttpRequest();
  var url = "/write?autostart=";

  if (document.getElementById("evse_autostart").checked == true)
  {
    url = url + "1";
  }
  else
  {
    url = url + "0";
  }
  xhr.onreadystatechange = function () {
    if (xhr.readyState === 4 && xhr.status === 200) {
        // console.log(xhr.responseText);
    }
  };

  xhr.open("GET", url, true);
  xhr.send();
}

function globalAlreadyStartUpdate()
{
  var xhr = new XMLHttpRequest();
  var url = "/write?alreadyboot=";

  if (document.getElementById("global_alreadyStart").checked == true)
  {
    url = url + "1";
  }
  else
  {
    url = url + "0";
  }
  xhr.onreadystatechange = function () {
    if (xhr.readyState === 4 && xhr.status === 200) {
        // console.log(xhr.responseText);
    }
  };

  xhr.open("GET", url, true);
  xhr.send();
}

function wifiUpdate()
{
  var xhr = new XMLHttpRequest();
  var url = "/setting?ssid=" + document.getElementById("wifi_ssid").value + "&pass=" + document.getElementById("wifi_password").value;

  xhr.onreadystatechange = function () {
    if (xhr.readyState === 4 && xhr.status === 200) {
        // console.log(xhr.responseText);
    }
  };

  xhr.open("GET", url, true);
  xhr.send();
}

getData();
//setInterval(getData, 10000);

</script>
</html>

)rawliteral";

  server.send(200, "text/html", html);
}





void web_index()
{
  
  String index_html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link rel="stylesheet" href="https://maxcdn.bootstrapcdn.com/bootstrap/3.4.1/css/bootstrap.min.css">
  <script src="https://ajax.googleapis.com/ajax/libs/jquery/3.5.1/jquery.min.js"></script>
  <script src="https://maxcdn.bootstrapcdn.com/bootstrap/3.4.1/js/bootstrap.min.js"></script>

  <link rel="stylesheet" href="https://use.fontawesome.com/releases/v5.7.2/css/all.css" integrity="sha384-fnmOCqbTlWIlj8LyTjo7mOUStjsKC4pOpQbqyi7RrhN7udi9RwhKkMHpvLbHG9Sr" crossorigin="anonymous">

</head>
<body>

<div class="container" style="width: 80%; margin: auto;max-width: 564px">
  <h2>bicoqueEVSE</h2>
  <p align=center>
    <i id="icone_chargeStation" class="fas fa-charging-station" style="color:#BDBDBD;font-size: 48px;"></i>
    <i id="icone_chargeEV" class="fas fa-car" style="color:#BDBDBD;font-size: 48px;"></i>
  </p>

  <p><center><span id=evse_status style="text-align: center;">-</span></center></p>

    <p>
    <ul class="list-group">
      <li class="list-group-item">Hardware Current Limit<span class="pull-right"><span id=hw_current>-</span> A</span></li>
      <li class="list-group-item">Actual Current Limit<span class="pull-right">
<select id=actual_current onChange="powerUpdate()">
<option value=6>6</option>
<option value=7>7</option>
<option value=8>8</option>
<option value=9>9</option>
<option value=10>10</option>
<option value=11>11</option>
<option value=12>12</option>
<option value=13>13</option>
<option value=14>14</option>
<option value=15>15</option>
<option value=16>16</option>
<option value=17>17</option>
<option value=18>18</option>
<option value=19>19</option>
<option value=20>20</option>
<option value=21>21</option>
<option value=22>22</option>
<option value=23>23</option>
<option value=24>24</option>
<option value=25>25</option>
<option value=26>26</option>
<option value=27>27</option>
<option value=28>28</option>
<option value=29>29</option>
<option value=30>30</option>
<option value=31>31</option>
<option value=32>32</option>
</select>
</span> A</span></li>
      <li class="list-group-item">Consommation Actuelle<span class="pull-right"><span id=actual_conso>-</span> kWh</span></li>
      <li class="list-group-item">Derniere Consommation<span class="pull-right"><span id=last_conso>-</span> kWh</span></li>
      <li class="list-group-item">Consommation Total<span class="pull-right"><span id=total_conso>-</span> kWh</span></li>
    </ul>
    </p>

  <p>
    <div class="hidden" id="evseActivate" align=center><button id="buttonActivate" type="button" class="btn btn-success" onclick="activateEVSE()">Activate EVSE</button></div>
    <div class="hidden" id="evseDeactivate" align=center><button id="buttonDeactivate" type="button" class="btn btn-danger" onclick="deactivateEVSE()">Deactivate EVSE</button></div>
  </p>


<footer style="position: absolute;bottom: 0;width: 100%;max-width: 520px;height: 30px;">
    <div class="pull-right">
                <a href="/config">Configuration</a>
    </div>
</footer>


</div>
</body>



<script>


function activateEVSE()
{
  var xhr = new XMLHttpRequest();
  xhr.open("POST", '/api/status', true);
  xhr.setRequestHeader("Content-Type", "application/json");
  xhr.onreadystatechange = function () {
    if (xhr.readyState === 4 && xhr.status === 200) {
        var json = JSON.parse(xhr.responseText);
        console.log(json);
    }
  };
  var data = JSON.stringify({"action": "on"});
  xhr.send(data);
}
function deactivateEVSE()
{
  var xhr = new XMLHttpRequest();
  xhr.open("POST", '/api/status', true);
  xhr.setRequestHeader("Content-Type", "application/json");
  xhr.onreadystatechange = function () {
    if (xhr.readyState === 4 && xhr.status === 200) {
        var json = JSON.parse(xhr.responseText);
        console.log(json);
    }
  };
  var data = JSON.stringify({"action": "off"});
  xhr.send(data);
}

function powerUpdate()
{
  var xhr = new XMLHttpRequest();
  xhr.open("POST", '/api/power', true);
  xhr.setRequestHeader("Content-Type", "application/json");
  xhr.onreadystatechange = function () {
    if (xhr.readyState === 4 && xhr.status === 200) {
        var json = JSON.parse(xhr.responseText);
        console.log(json);
    }
  };

  var data = '{"action": ' + document.getElementById("actual_current").value  + '}';
  xhr.send(data);
}


function getData()
{
  var xhttp = new XMLHttpRequest();

  xhttp.onreadystatechange = function()
  {
    if (this.readyState == 4 && this.status == 200)
    {
      const obj = JSON.parse(this.responseText);
      // console.log(obj.version);
      document.getElementById("hw_current").innerHTML = obj.powerHardwareLimit;
      document.getElementById("actual_current").selectedIndex = (obj.currentLimit - 6);
      document.getElementById("actual_conso").innerHTML = (obj.consumptionActual / 1000).toFixed(3);
      document.getElementById("last_conso").innerHTML = (obj.consumptionLastCharge / 1000).toFixed(3);
      document.getElementById("total_conso").innerHTML = (obj.consumptions / 1000).toFixed(0);


      // console.log(obj.status);

      if (obj.status == 0)
      {
        document.getElementById("icone_chargeStation").style.color = "#BDBDBD";
        document.getElementById("icone_chargeEV").style.color = "#BDBDBD";
      }
      if (obj.status == 1)
      {
        document.getElementById("icone_chargeStation").style.color = "#059e8a";
        document.getElementById("icone_chargeEV").style.color = "#BDBDBD";
      }
      if (obj.status == 2)
      {
        document.getElementById("icone_chargeStation").style.color = "#059e8a";
        document.getElementById("icone_chargeEV").style.color = "#059e8a";
      }
      if (obj.status > 2)
      {
        document.getElementById("icone_chargeStation").style.color = "#059e8a";
        document.getElementById("icone_chargeEV").style.color = "#0277BD";
      }


      document.getElementById("evse_status").innerHTML = obj.statusName;

      if (obj.enable == 1)
      {
        $("#evseActivate").addClass('hidden');
        $("#evseDeactivate").removeClass('hidden');
      }
      else
      {
        $("#evseActivate").removeClass('hidden');
        $("#evseDeactivate").addClass('hidden');
      }


      //document.getElementById("").innerHTML = obj.;
    }
  };
  xhttp.open("GET", "/jsonInfo", true);
  xhttp.send();
}



getData();
setInterval(getData, 10000);

</script>
</html>
)rawliteral";

  server.send(200, "text/html", index_html);
}




















// ------------------------------
// Wifi
// ------------------------------
bool wifiConnectSsid(const char* ssid, const char* password)
{
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  int c = 0;
  Serial.println("Waiting for Wifi to connect");
  while ( c < 80 ) {
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
void wifiDisconnect()
{
  WiFi.mode( WIFI_OFF );
}

bool wifiConnect(String ssid, String password)
{
    if ( DEBUG ) { Serial.println("Enter wifi config"); }
    lcd.setCursor(1, 1);
    lcd.print("wifi settings");

    lcd.setCursor(1, 2);
    lcd.print("   connecting...");
    if (DEBUG) { Serial.print("Wifi: Connecting to '"); Serial.print(ssid); Serial.println("'"); }

    // Unconnect from AP
    WiFi.mode(WIFI_AP);
    WiFi.disconnect();

    // Connecting to SSID
    WiFi.mode(WIFI_STA);
    WiFi.hostname(SOFT_NAME);

    bool wifiConnected = wifiConnectSsid(ssid.c_str(), password.c_str());

    if (wifiConnected)
    {
      if (DEBUG) { Serial.println("WiFi connected"); }
      internetConnection = 1;

      lcd.print(" .. ok");

      ip = WiFi.localIP();

      return true;
    }
    else
    {
        lcd.print("  ko");
    }

    // If SSID connection failed. Go into AP mode
    lcd.setCursor(1, 2);
    lcd.print("   standalone mode");

    // Disconnecting from Standard Mode
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    // Connect to AP mode
    WiFi.mode(WIFI_AP);
    WiFi.hostname(SOFT_NAME);

    if (DEBUG) { Serial.println("Wifi config not present. set AP mode"); }
    WiFi.softAP(wifiApSsid);
    if (DEBUG) {Serial.println("softap"); }

    ip = WiFi.softAPIP();

    return false;
}

void wifiCheck()
{
   // Check if we have a delay on wifi to disable it
   if (wifiActivationTempo > 0 )
   {
      if (wifiActivationTempo < (millis() / 1000) )
      {
        if (softConfig.wifi.enable)
        {
          // try to reconnect evry 10 mins
	  
          internetConnection = wifiConnect(softConfig.wifi.list[ softConfig.wifi.prefered ].ssid, softConfig.wifi.list[ softConfig.wifi.prefered ].password);

          if (internetConnection)
          {
            wifiActivationTempo = 0;
          }
          else
          {
	    // check other wifi found
            int wifiFound = WiFi.scanComplete();
            for (int i = 0; i < softConfig.wifi.nextRecord; i++)
	    {
		for (int j = 0; j < wifiFound; i++)
		{
			//if (strcmp(softConfig.wifi.list[i].ssid, WiFi.SSID(j)))
			if (softConfig.wifi.list[i].ssid = WiFi.SSID(j) )
			{
				internetConnection = wifiConnect(softConfig.wifi.list[i].ssid, softConfig.wifi.list[i].password);
				if (internetConnection)
				{
					break;
				}
			}
		}
    	    }

	    if (internetConnection)
            {
                wifiActivationTempo = 0;
            }
            else
	    {
               wifiActivationTempo = (millis() / 1000) + 600;
	    }
          }
        }
        else
        {
          // Need to de-active wifi
          WiFi.mode(WIFI_OFF);
          WiFi.disconnect();

          wifiActivationTempo = 0;
          networkEnable       = 0;
        }
      }
   }
   else
   {
     // if we are connected/ check if are still connected
     if (WiFi.status() != WL_CONNECTED )
     { 
        wifiActivationTempo = (millis() / 1000) + 600;
     }
   }
}

// ---------------------------------
// Wifi scan network
// ---------------------------------
void wifiScanNetworks()
{
  String st = "";
  WiFi.scanNetworks(true,true);

  while ( WiFi.scanComplete() == -1 )
  {
    // Wait end of scan
    delay(1000);
  }
  Serial.println("scan done");
  Serial.print(WiFi.scanComplete()); Serial.println(" networks found");

  for (int i = 0; i < WiFi.scanComplete() ; ++i)
  {
      // Print SSID and RSSI for each network found
      st += WiFi.SSID(i);
      st += WiFi.RSSI(i);
      st += (WiFi.encryptionType(i) == ENC_TYPE_NONE) ? " " : "*";
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.print(WiFi.SSID(i));
      Serial.print(" (");
      Serial.print(WiFi.RSSI(i));
      Serial.print(")");
      Serial.println((WiFi.encryptionType(i) == ENC_TYPE_NONE) ? " " : "*");
  }
}






// --------------------------------
// Update
// --------------------------------
void updateCheck(bool displayScreen)
{
    

    if (displayScreen)
    {
      lcd.setCursor(1, 1);
      lcd.print("check update       ");
      delay(1000);
    }
  
    // save power
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

    if (displayScreen)
    {
      lcd.setCursor(1, 1);
      lcd.print("update done  ");
    }
    lcd.backlight();

}








// -------------------------------
// Logger function
// -------------------------------
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
  if (softConfig.wifi.enable)
  {
    HTTPClient httpClientPost;
    String urlTemp = BASE_URL;
    urlTemp       += "log.php";
    String data    = "message=";
    data          += urlencode(message);

    httpClientPost.begin(urlTemp);
    httpClientPost.addHeader("Content-Type", "application/x-www-form-urlencoded");
    httpClientPost.POST(data);
    httpClientPost.end();

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

  // Scan for wifi network
  WiFi.scanNetworks(true,true);

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
    boolean configFileToCreate = 0;
    // check if we have et config file
    if (SPIFFS.exists("/config.json"))
    {
      Serial.println("Config.json found. read data");
      configRead(softConfig, "/config.json" );

      if (softConfig.softName != SOFT_NAME)
      {
        Serial.println("Not the same softname");
        Serial.print("Name from configFile : "); Serial.println(softConfig.softName);
        Serial.print("Name from code       : "); Serial.println(SOFT_NAME);

        configFileToCreate = 1;
        
        //softConfig.softName       = SOFT_NAME;
        //configSave();
      }
      else 
      {
 	// For update modifications
	if (softConfig.softVersion < "1.5.07" or softConfig.softVersion == "null" or softConfig.softVersion == "")
 	{
		// Change wifi config. Need to set it them in list
		softConfig.wifi.prefered         = 0;
		softConfig.wifi.nextRecord       = 1;
      		for (int i=1; i <10; i++)
      		{
        		softConfig.wifi.list[i].ssid     = "";
        		softConfig.wifi.list[i].password = "";
      		}
		configSave();
	}
      }
    }
    else
    {
      configFileToCreate = 1;
    }

    if (configFileToCreate == 1)
    {
      // No config found.
      // Start in AP mode to configure
      // debug create object here
      Serial.println("Config.json not found. Create one");
      
      softConfig.wifi.enable     = 1;
      softConfig.wifi.prefered   = 0;
      softConfig.wifi.nextRecord = 0;
      softConfig.evse.autoStart  = 1;
      softConfig.alreadyStart    = 0;
      softConfig.softName        = SOFT_NAME;
      softConfig.softVersion     = SOFT_VERSION;

      for (int i=0; i <10; i++)
      {
        softConfig.wifi.list[i].ssid     = "";
        softConfig.wifi.list[i].password = "";
      }

      Serial.println("Config.json : load save function");
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
    configDump(softConfig);
  }


  if (softConfig.alreadyStart == 0 && softConfig.wifi.enable == 0)
  {
    softConfig.wifi.enable = 1;
  }


  // Define Pins for button
  pinMode(inPin, INPUT_PULLUP);

  
  wifiCheck();
  /* Replace by wifiCheck
  internetConnection = wifiConnect(softConfig.wifi.ssid, softConfig.wifi.password);

  if (internetConnection)
  {
    wifiActivationTempo = 0;
  }
  else
  {
    if (DEBUG) { Serial.println("Deactive wifi in 5 mins."); }
    wifiActivationTempo = 600;
  }
  */


  Serial.println("End of wifi config");

  int wifiMode = 0;

  // Start the server
  lcd.setCursor(1, 1);
  lcd.print("load webserver");
  lcd.setCursor(1, 2);
  lcd.print("                 ");

  //server.on("/", webRoot);
  server.on("/", web_index);
  server.on("/config", web_config);
  server.on("/reload", webReload);
  server.on("/write", webWrite);
  server.on("/debug", webDebug);
  server.on("/reboot", webReboot);
  server.on("/jsonInfo", webJsonInfo);
  server.on("/api/status", webApiStatus);
  server.on("/api/power", webApiPower);
  server.on("/api/config", webApiConfig);

  server.on("/wifi", webInitRoot);
  server.on("/setting", webInitSetting);

  server.onNotFound(webNotFound);
  server.begin();

  if (DEBUG)
  {
    Serial.print("Http: Server started at http://");
    Serial.print(ip);
    Serial.println("/");
    Serial.print("Status : ");
    Serial.println(WiFi.RSSI());
  }


  if (internetConnection)
  {
    // check update
    updateCheck(1);

    // get time();
    timeClient.begin();

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

  if (softConfig.alreadyStart == 0)
  {
    softConfig.alreadyStart = 1;
    configSave();
    // eepromWrite(ALREADYBOOT, "1");
  }


  // Force activating EVSE if autostart is on
  if (softConfig.evse.autoStart == 1)
  {
    evseWrite("evseStatus", EVSE_ACTIVE);
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
  }

}


void loop()
{
  // put your main code here, to run repeatedly:
  int timeNow = timeClient.getEpochTime();

  // Check if wifi is not up
  wifiCheck();

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
          if (softConfig.evse.autoStart == 1) // We are into autostart mode
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
  if (networkEnable)
  {
    // check web client connections
    server.handleClient();
  }


  // get stats for power consumption
  if (evseStatus >= 3)
  {

    if (consumptionLastTime == 0)
    {
      // need to decale all int
      consumptionLastTime = timeClient.getEpochTime();
      statsLastWrite      = consumptionLastTime;
    }
    else
    {
      consumptionActual   = evseCurrentLimit * AC_POWER ;

      long timestamp = timeClient.getEpochTime(); // We work in seconds
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

      long timestamp = timeClient.getEpochTime(); // We work in seconds
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
      if (softConfig.evse.autoStart == 1)
      {
        if (evseEnable == 0)
        {
          evseWrite("evseStatus", EVSE_ACTIVE);
        }
      }
      else
      {
        evseWrite("evseStatus", EVSE_DISACTIVE);
      }
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
      logger("shutdown light on screen");
      lcd.noBacklight();
      sleepModeTimer = 0;
      sleepMode = 1;

    }
  }


  //-- To do evry x secs
  if ( (timerPerHourLast + 3600) < timeNow )
  {
    updateCheck(0);
    timeClient.update();
    timerPerHourLast = timeNow;

    // String messageToLog = "Datalog: temp int: "; messageToLog += temperature; messageToLog += " / Temp ext : "; messageToLog += Meteo.temp;
    // logger(messageToLog);
  }



  delay(100); // Wait 1 sec if we are not in sleepMode

}
