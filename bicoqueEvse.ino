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


// Instantiate ModbusMaster object as slave ID 1
ModbusMaster232 node(1);


// firmware version
#define SOFT_VERSION "1.4.68"
#define SOFT_DATE "2019-03-06"
#define EVSE_VERSION 10

// address for EEPROM
#define WIFIENABLE 0
#define WIFIENABLE_SIZE 1
#define WIFISSID 1
#define WIFISSID_SIZE 32
#define WIFIPASS 33
#define WIFIPASS_SIZE 64
#define AUTOSTART 97
#define AUTOSTART_SIZE 1
#define ALREADYBOOT 98
#define ALREADYBOOT_SIZE 1

#define STATS_TOTAL 200
#define STATS_TOTAL_CENTS 202
#define STATS_BEGIN 204
#define STATS_END 128


// Wifi AP info for configuration
const char* wifiApSsid = "bicoqueEVSE";
const char* wifiApPasswd = "randomPass";



// Update info
#define BASE_URL "http://mangue.net/ota/esp/bicoqueEvse/"
#define UPDATE_URL "http://mangue.net/ota/esp/bicoqueEvse/update.php"

// Web server info
ESP8266WebServer server(80);


// EVSE info
char* evseStatusName[] = { "n/a", "Waiting Car","Car Connected","Charging","Charging" } ;
String evseRegisters[23]; // indicate the number of registers
int registers[] = { 1000,1001,1002,1003,1004,1005,1006,2000,2001,2002,2003,2004,2005,2006,2007,2010,2011,2012,2013,2014,2015,2016,2017 };
int simpleEvseVersion ; // need to have it to check a begining and not start if it's not the same
int evseCurrentLimit  = 0;
int evsePowerOnLimit  = 0;
int evseHardwareLimit = 0;
int evseStatus        = 0;
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
LiquidCrystal_I2C lcd(0x27,20,4);  // set the LCD address to 0x27 for a 20 chars and 4 line display


// Json allocation
StaticJsonDocument<200> jsonBuffer;

// screen default page
int page = 0;

// Menu setings
//char* menu1[] = { "SETINGS", "Amperage","AutoStart","Infos","Consommation","Wifi","Exit" } ;
char* menu1[] = { "SETINGS", "Amperage","AutoStart","Infos","Wifi","Reboot","Exit" } ;
char* menu2[] = { "WIFI","Activation","Reset","Back" } ;
//char* menu3[] = { "CONSOMMATION","Sur 24h","Sur 30 jours","Total","Back" };

char* enum101[] = { "AMPERAGE","5A","6A","7A","8A","9A","10A","11A","12A","13A","14A","15A","16A","17A","18A","19A","20A","21A","22A","23A","24A","25A","26A","27A","28A","29A","30A" };
char* enum201[] = { "AUTOSTART","Active","Desactive" };
char* enum301[] = { "WIFI ACTIVATION","Active","Desactive" };
char* enum401[] = { "WIFI RESET","Oui","Non" };

char* pages1001[] = { "INFOS","page1","page2" };

int menuStatus = 1;
char menuValue[30];
#define NUMITEMS(arg) ((unsigned int) (sizeof (arg) / sizeof (arg [0])))
#define MENU_DW 1
#define MENU_SELECT 2


// current consumptions
#define AC_POWER 230 
int consumptions[31] = { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 };
int consumptionLastTime    = 0;
float consumptionCounter   = 0;
int consumptionActual      = 0;
int consumptionTotal       = 0;
float consumptionTotalTemp = 0;
int statsLastWrite         = 0;

// Time
long timeAtStarting = 0; // need to get from ntp server timestamp when ESP start


// ********************************************
// All Menu Functions
// ********************************************
int menuGetFromAction(void)
{
  Serial.print("Debug menu: menuValue : "); Serial.println(menuValue);
  
  if (menuStatus >= 100 && menuStatus <200) // AMPERAGE
  {
    menuStatus = 1; // menu to return to
    // remove the 'A' at the end of value ex: 5A -> 5
    menuValue[ strlen(menuValue) - 1 ] = '\0';
    String tempValue = menuValue;
    
    //-- write data
    evseUpdatePower(tempValue.toInt(), NORMAL_STATE);
  }
  else if (menuStatus >= 200 && menuStatus <300) // AUTOSTART
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
    eepromWrite(AUTOSTART, String(evseAutoStart) );
  }
  else if (menuStatus >= 300 && menuStatus <400)
  {
    menuStatus = 11;
    if ( strcmp(menuValue, enum301[1]) == 0) { wifiEnable = 1; }
    else if ( strcmp(menuValue, enum301[2]) == 0) 
    { 
      wifiEnable = 0;       
      WiFi.mode(WIFI_OFF); 
      WiFi.disconnect();
    }
    
    eepromWrite(WIFIENABLE, String(wifiEnable) );
  }
  else if (menuStatus >= 400 && menuStatus <500)
  {
    menuStatus = 12;
    if ( strcmp(menuValue, enum401[1]) == 0)
    {
      // Reset Wifi info from eeprom
      wifiReset();
    }
  }
  else if (menuStatus >= 1000 && menuStatus <2000)
  {
    menuStatus = 3;
  }
  else
  {
    // continue navigation into menu
    switch (menuStatus)
    {
      case 1: if (evsePowerOnLimit < 5) { evsePowerOnLimit = 5; }; menuStatus = 101 + evsePowerOnLimit - 5 ; break;
      case 2: if (evseAutoStart) { menuStatus = 201; } else { menuStatus = 202; }; break;
      case 3: menuStatus = 1001; evseAllInfo() ; break;
      case 4: menuStatus = 11; break;
      case 5: ESP.restart(); return 0;
      case 6: menuStatus = 1 ; internalMode = 3 ; return 0; // exit menu
      
      case 11:
      // get actual value and set the good menu
      if (wifiEnable) { menuStatus = 301; } else { menuStatus = 302; };
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

  //printf("Menu : %d", menu); //printf(" / action : "); printf(action); printf("\n");

  // Need to check select action
  if (action == MENU_SELECT)
  {
    int fnret = menuGetFromAction();
    if (fnret == 0) { return; } // Exit wanted
    //printf("Value to store : %s\n", menuValue);
  }

  // set array to generic one
  if (menuStatus < 10)
  {
    menuTemp = 0; itemsToPrint = NUMITEMS(menu1); for ( int i =0 ; i < itemsToPrint ; i++) { menuToPrint[i] = menu1[i]; }
  }
  else if (menuStatus >= 10 && menuStatus < 20)
  {
    menuTemp = 10; itemsToPrint = NUMITEMS(menu2); for ( int i =0 ; i < itemsToPrint ; i++) { menuToPrint[i] = menu2[i]; }
  }
  /*
  else if (menuStatus >= 20 && menuStatus < 30)
  {
        menuTemp = 20; itemsToPrint = NUMITEMS(menu3); for ( int i =0 ; i < itemsToPrint ; i++) { menuToPrint[i] = menu3[i]; }
  }
  else if (menuStatus >= 30 && menuStatus < 40)
  {
        menuTemp = 30; itemsToPrint = NUMITEMS(menu4); for ( int i =0 ; i < itemsToPrint ; i++) { menuToPrint[i] = menu4[i]; }
  }
  else if (menuStatus >= 40 && menuStatus < 50)
  {
        menuTemp = 40; itemsToPrint = NUMITEMS(menu5); for ( int i =0 ; i < itemsToPrint ; i++) { menuToPrint[i] = menu5[i]; }
  }
  */
  else if (menuStatus >= 100 && menuStatus < 200)
  {
    menuTemp = 100; itemsToPrint = NUMITEMS(enum101); for ( int i =0 ; i < itemsToPrint ; i++) { menuToPrint[i] = enum101[i]; }
    valueSelecting = 1;
  }
  else if (menuStatus >= 200 && menuStatus < 300)
  {
    menuTemp = 200; itemsToPrint = NUMITEMS(enum201); for ( int i =0 ; i < itemsToPrint ; i++) { menuToPrint[i] = enum201[i]; }
    valueSelecting = 1;
  }
  else if (menuStatus >= 300 && menuStatus < 400)
  {
    menuTemp = 300; itemsToPrint = NUMITEMS(enum301); for ( int i =0 ; i < itemsToPrint ; i++) { menuToPrint[i] = enum301[i]; }
    valueSelecting = 1;
  }
  else if (menuStatus >= 400 && menuStatus < 500)
  {
    menuTemp = 400; itemsToPrint = NUMITEMS(enum401); for ( int i =0 ; i < itemsToPrint ; i++) { menuToPrint[i] = enum401[i]; }
    valueSelecting = 1;
  }
  else if (menuStatus > 1000)
  {
    menuTemp = 1000;
    itemsToPrint = NUMITEMS(evseRegisters);
    for ( int i =0 ; i < itemsToPrint ; i++) 
    {
      menuToPrint[i] = evseRegisters[i];
      //Serial.print("pointer address : "); Serial.println((long)&menuToPrint[i]);
      //Serial.print(i); Serial.print(" ----> "); Serial.println(menuToPrint[i]);
    }
    valueSelecting = 3;
  }

  if (action == MENU_DW) { menuStatus++; }
  if ( (menuStatus - menuTemp) >= itemsToPrint) { menuStatus = menuTemp + 1; }

  menuToPrint[ (menuStatus - menuTemp) ].toCharArray(menuValue, 30);
  
  // Printing on screen
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(menuToPrint[0]);
  
  /*
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0,0);
  display.println(menuToPrint[0]);
  display.drawLine(0, 9, 125, 9, WHITE);
  display.setCursor(0,11);1
  */
  
  if (valueSelecting == 0)
  {
    int screenCursor = 1;
    int myPosition = menuStatus - menuTemp;

    // we have only 3 lines to print.
    int startArray = 0;
    if ( myPosition < 4) { startArray = 1;}
    else if (myPosition >= 4) { startArray = myPosition - 2;}

    for (int i = startArray ; i< itemsToPrint; i++)
    {
      char temp2[18] = "                 ";
      int tempLength = menuToPrint[i].length();
      int lenghToAdd = 17-tempLength; // align on right
      lenghToAdd = 1; // align on left
      for (int j = 0; j < tempLength ; j++) { temp2[(j+lenghToAdd)] = menuToPrint[i][j]; }

      if (screenCursor < 4)
      {
        if ( i == myPosition )
        {
          lcd.setCursor( 1, screenCursor);
          lcd.write(0);
  //      display.setTextColor(BLACK, WHITE);
  //      display.println(temp2);
  //      display.setTextColor(WHITE);
        }
        else
        {
          lcd.setCursor( 2, screenCursor);
        }
        lcd.print(temp2);
      }

      screenCursor++;
  //      display.println(temp2);
      
    }
  }
  else if (valueSelecting == 1)
  {
//    display.println("");
//    display.println("");
//    display.println("");
    char temp2[20] = "                   ";
    int tempLength = menuToPrint[(menuStatus - menuTemp)].length();
    int lenghToAdd = 19-tempLength;
    for (int j = 0; j < tempLength ; j++) { temp2[(j+lenghToAdd)] = menuToPrint[(menuStatus - menuTemp)][j]; }
//    display.println(temp2);
    lcd.setCursor(0, 1);
    lcd.print(temp2);
  }
  else if (valueSelecting == 2)
  {
    for (int i = 1; i< itemsToPrint; i++)
    {
      //char temp2[22] = "                     ";
      char temp2[22] = "";
      int tempLength = menuToPrint[i].length(); //strlen(menuToPrint[i]);
      int lenghToAdd = 21-tempLength;
      for (int j = 0; j < tempLength ; j++) { temp2[(j+lenghToAdd)] = menuToPrint[i][j]; }
      
         
      if (i == (menuStatus - menuTemp)) 
      {
        lcd.setCursor(20-tempLength , 3);
        lcd.print(temp2);
//        display.setTextColor(BLACK, WHITE);
//        display.println(temp2);
//        display.setTextColor(WHITE);
      } 
      else 
      { 
//         display.println(temp2);
      }
    }
  }
  else if (valueSelecting == 3)
  {
    int beginArray = (menuStatus - menuTemp - 1);
    if (beginArray > itemsToPrint ) { beginArray = 0; }
    
    for( int i=beginArray ; i < (beginArray + 4) ; i++)
    {
      lcd.setCursor(0, (i - beginArray));
      lcd.print(menuToPrint[i]);
      Serial.print("menu debug = "); Serial.print(i); Serial.print("  --  "); Serial.println(menuToPrint[i]);
    }

    menuStatus = menuTemp + beginArray + 4;
  }
  
//  display.display();
}









// ********************************************
// EVSE Functions
// ********************************************
void evseReload()
{
  //return;
  node.readHoldingRegisters(1000, 1);
  evseCurrentLimit = node.getResponseBuffer(0);
  //delay(100);
  node.clearResponseBuffer();
  node.readHoldingRegisters(1002, 1);
  evseStatus = node.getResponseBuffer(0);
  //delay(100);
  node.clearResponseBuffer();
  node.readHoldingRegisters(1003, 1);
  evseHardwareLimit = node.getResponseBuffer(0);
  //delay(100);
  node.clearResponseBuffer();
  node.readHoldingRegisters(2000, 1);
  evsePowerOnLimit = node.getResponseBuffer(0);
  //delay(100);
  node.clearResponseBuffer();
}
void evseAllInfo()
{
  for (int i = 0; i< NUMITEMS(registers); i++)
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
     	String messageToLog = "Read%20register%20:%20"; messageToLog += registers[i]; messageToLog += "%20-%20value%20:%20"; messageToLog += registerResult ;
   	logger2(messageToLog);
     }
     //Serial.print("Debug : "); Serial.println(evseRegisters[i]);
     //delay(100);
  }
  //for (int i = 0; i < 20; i++) { Serial.print(i); Serial.print(" - "); Serial.println(evseRegisters[i]); }
}
void evseStatusCheck()
{
  node.readHoldingRegisters(1002, 1);
  evseStatus = node.getResponseBuffer(0);
  //delay(100);
  node.clearResponseBuffer();
}
void evseUpdatePower(int power, int currentOnly)
{
  int powerCurrent = 0;
  // to start or stop charging when we are in non autostart mode
  if (evseAutoStart == 1 || currentOnly == 1 || evseStatus == 3 || evseStatus == 4)
  {
    powerCurrent = power;
  }

  // to be test. Now we stop/start with other function.
  powerCurrent = power;

  
  evseWrite("currentLimit", powerCurrent);
  evseCurrentLimit = powerCurrent;
  
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


   String messageToLog = "Write%20register%20:%20"; messageToLog += RegisterToWriteIn; messageToLog += "%20-%20value%20:%20"; messageToLog += value ;
   logger2(messageToLog);

   node.setTransmitBuffer(0, value);
   node.writeMultipleRegisters(RegisterToWriteIn, 1); 
   //delay(100); // Do not DDoS modbus :)
   
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
  
  //Serial.print("Debug eeprom: read address : "); Serial.print(startAddress); Serial.print(" - string : "); Serial.println(stringRead);
  return stringRead;
}

int eepromWrite(int startAddress, String stringToWrite)
{
  for (int i = 0; i < stringToWrite.length(); ++i) 
  { 
    EEPROM.write(startAddress + i, stringToWrite[i]); 
  } 
  EEPROM.commit();

  Serial.print("Debug eeprom: write address : "); Serial.print(startAddress); Serial.print(" - string : "); Serial.println(stringToWrite);
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
int eepromReadInt(int address){
   int value = 0x0000;
   value = value | (EEPROM.read(address) << 8);
   value = value | EEPROM.read(address+1);
   return value;
}
 
void eepromWriteInt(int address, int value){
   EEPROM.write(address, (value >> 8) & 0xFF );
   EEPROM.write(address+1, value & 0xFF);
   EEPROM.commit();
}
 
float eepromReadFloat(int address){
   union u_tag {
     byte b[4];
     float fval;
   } u;   
   u.b[0] = EEPROM.read(address);
   u.b[1] = EEPROM.read(address+1);
   u.b[2] = EEPROM.read(address+2);
   u.b[3] = EEPROM.read(address+3);
   return u.fval;
}
 
void eepromWriteFloat(int address, float value){
   union u_tag {
     byte b[4];
     float fval;
   } u;
   u.fval=value;
 
   EEPROM.write(address  , u.b[0]);
   EEPROM.write(address+1, u.b[1]);
   EEPROM.write(address+2, u.b[2]);
   EEPROM.write(address+3, u.b[3]);

   EEPROM.commit();
}

void eepromReadString(int offset, int bytes, char *buf){
  char c = 0;
  for (int i = offset; i < (offset + bytes); i++) {
    c = EEPROM.read(i);
    buf[i - offset] = c;
    if (c == 0) break;
  }
}

void eepromWriteString(int offset, int bytes, char *buf){
  char c = 0;
  //int len = (strlen(buf) < bytes) ? strlen(buf) : bytes;
  for (int i = 0; i < bytes; i++) {
    c = buf[i];
    EEPROM.write(offset + i, c); 
  }
  EEPROM.commit();
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
  timeAtStarting = timestamp.toInt() - (millis()/1000);
  
}




// ********************************************
// SCREEN Functions
// ********************************************

void screenDefault(int page)
{
  Serial.println("enter screen default");
  
  lcd.clear();

  //lcd.write(3);
  lcd.home();

  //lcd.setCursor(4,0);
  //lcd.print("bicoque EVSE");
    
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
      lcd.setCursor(0,0);
      lcd.print(statusLine1);
      lcd.setCursor(0,2);
      lcd.print(statusLine2);
      lcd.setCursor(0,3);
      lcd.print(statusLine3);
      break;
    case 1:
      lcd.setCursor(0,0);lcd.print("Wifi Power : ");lcd.print(WiFi.RSSI());
      lcd.setCursor(0,1);lcd.print("IP: "); lcd.print(ip);
      lcd.setCursor(0,2);lcd.print("Amps : "); lcd.print(evsePowerOnLimit); lcd.print("A / "); lcd.print(evseCurrentLimit); lcd.print("A");
      lcd.setCursor(0,3);lcd.print("Conso: "); lcd.print(int(consumptionTotal/1000)); lcd.print("kWh");
      //display.print("Current Amps : "); display.print(evseCurrentLimit); display.println("A");
      //display.print("PowerOn Amps : "); display.print(evsePowerOnLimit); display.println("A");
      break;
    case 2:
      lcd.setCursor(0,2);lcd.print("   SETTINGS");
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
//  if (evseStatus == 1) { message += "ready"; }
//  else if (evseStatus == 2) { message += "EV is present"; }
//  else if (evseStatus == 3) { message += "charging"; }
//  else if (evseStatus == 4) { message += "charging with ventillation"; }
//  else if (evseStatus == 0) { message += "EVSE not connected"; }
  message += "<br><br>";

  message += "Wifi power : "; message += WiFi.RSSI(); message +="<br>";
  message +="<br>";
  
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
  message += "<br><br>";

  int itemsToPrint = NUMITEMS(evseRegisters);
  for ( int i =0 ; i < itemsToPrint ; i++) 
  {
    message += evseRegisters[i]; message += "<br>";
  }
  message += "<br>";

  message += "<form action='/write' method='GET'>Amperage (0-80): <input type=text name=amperage><input type=submit></form><br>";
  message += "<form action='/write' method='GET'>Modbus (0-1) : <input type=text name=modbus><input type=submit></form><br>";
  message += "<form action='/write' method='GET'>Raw write : Register : <input type=text name=register> / Value : <input type=text name=value><input type=submit></form><br>";
  message += "<form action='/write' method='GET'>Start Charging: <input type=hidden name=chargeon value=yes><input type=submit></form><br>";
  message += "<form action='/write' method='GET'>Stop Charging: <input type=hidden name=chargeoff value=yes><input type=submit></form><br>";
  message += "<br><br>";
  message += "<a href='/reboot'>Rebbot device</a><br>";
  
  message += "</html>";
  server.send(200, "text/html", message);
}
void webJsonInfo()
{

  long timestamp = int(millis()/1000);
  
  String message = "{\n  \"version\": \""; message += SOFT_VERSION; message += "\",\n";
  //message += "  \"\": \""; message += ; message += "\",\n";

  message += "  \"powerOn\": \""; message += evsePowerOnLimit; message += "\",\n";
  message += "  \"currentLimit\": \""; message += evseCurrentLimit; message += "\",\n";
  message += "  \"consumptionLastTime\": \""; message += consumptionLastTime ; message += "\",\n";
  message += "  \"consumptionCounter\": \""; message += consumptionCounter ; message += "\",\n";
  message += "  \"consumptions\": \""; message += consumptionTotal ; message += "\",\n";
  message += "  \"consumptionActual\": \""; message += consumptionActual ; message += "\",\n";
  message += "  \"wifiSignal\": \""; message += WiFi.RSSI(); ; message += "\",\n";
  message += "  \"uptime\": \""; message += timestamp ; message += "\",\n";
  message += "  \"time\": \""; message += timestamp + timeAtStarting ; message += "\",\n";
  message += "  \"statusName\": \""; message += evseStatusName[ evseStatus ] ; message += "\",\n";


  message += "  \"status\": \""; message += evseStatus; message += "\"\n";
  message += "}";

  server.send(200, "application/json", message);
}
void webReboot() 
{
  String message = "<!DOCTYPE HTML>";
  message += "<html>";
  message += "Rebbot in progress...<b>";
  message += "</html>";
  server.send(200, "text/html", message);

  ESP.restart();
}  

void webApiStatus()
{
  String message = "{\n";

  if ( server.method() == HTTP_GET )
  {
        message += "  \"status\": \""; message += evseStatus ; message += "\",\n";
        message += "  \"statusName\": \""; message += evseStatusName[ evseStatus ] ; message += "\"\n}\n";
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
      const char* actionValue = jsonBuffer["action"];
      if (actionValue == "on")
      {
        evseWrite("evseStatus", EVSE_ACTIVE);
      }
      if (actionValue == "off")
      {
        evseWrite("evseStatus", EVSE_DISACTIVE);
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

void webNotFound(){
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i=0; i<server.args(); i++){
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
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
      message += "clear all stats";
      eepromClear(STATS_TOTAL, (STATS_END + 4));
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
    
    //delay(100);
    Serial.println("Write done");
    message += "Write done...\n";
    server.send(200, "text/plain", message);
    //evseReload();
    //webRoot();
}
void webInitRoot()
{
  String wifiList = wifiScan();
  String message = "<!DOCTYPE HTML>";
  message += "<html>";
 
  message += "Bicoque EVSE"; 
  message += "<br><br>";
  
  message += "Please configure your Wifi : <br>";
  message += "<p>";
  message += wifiList;
  message += "</p><form method='get' action='setting'><label>SSID: </label><input name='ssid' length=32><input name='pass' length=64><input type='submit'></form>";
  message += "</html>";
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
          Serial.println("clearing eeprom");
          for (int i = 0; i < 96; ++i) { EEPROM.write(1+i, 0); }
          Serial.println(qsid);
          Serial.println("");
          Serial.println(qpass);
          Serial.println("");
            
          Serial.println("writing eeprom ssid:");
          for (int i = 0; i < qsid.length(); ++i)
            {
              EEPROM.write(1+i, qsid[i]);
              Serial.print("Wrote: ");
              Serial.println(qsid[i]); 
            }
          Serial.println("writing eeprom pass:"); 
          for (int i = 0; i < qpass.length(); ++i)
            {
              EEPROM.write(33+i, qpass[i]);
              Serial.print("Wrote: ");
              Serial.println(qpass[i]); 
            }    
          EEPROM.commit();
          content = "{\"Success\":\"saved to eeprom... reset to boot into new wifi\"}";
          statusCode = 200;
        } else {
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
    if (WiFi.status() == WL_CONNECTED) { return true; } 
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
      st += (WiFi.encryptionType(i) == ENC_TYPE_NONE)?" ":"*";
      st += "</li>";
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.print(WiFi.SSID(i));
      Serial.print(" (");
      Serial.print(WiFi.RSSI(i));
      Serial.print(")");
      Serial.println((WiFi.encryptionType(i) == ENC_TYPE_NONE)?" ":"*");
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

  eepromClear(WIFISSID, WIFISSID_SIZE);
  eepromClear(WIFIPASS, WIFIPASS_SIZE);

  WiFi.mode(WIFI_OFF);
  WiFi.disconnect();
}


void logger(String message)
{
  if (wifiEnable)
  {
    HTTPClient httpClient;
    String urlTemp ;
    urlTemp += "http://mangue.net/ota/esp/bicoqueEvse/log.php?message=\"";
    urlTemp += message;
    urlTemp += "\"";
  
    //httpClient.begin(urlTemp);
    httpClient.begin("http://mangue.net/ota/esp/bicoqueEvse/log.php");
    httpClient.GET();
    httpClient.end();
  }
}

void logger2(String message)
{
  if (wifiEnable)
  {
    HTTPClient httpClient;
    String urlTemp ;
    urlTemp += "http://mangue.net/ota/esp/bicoqueEvse/log.php?message=";
    urlTemp += message;
    //urlTemp += "'";
  
    httpClient.begin(urlTemp);
    //httpClient.begin("http://mangue.net/ota/esp/bicoqueEvse/log.php?message=ici");
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

  // Screen Init
  lcd.init();
  lcd.backlight();
  uint8_t bell[8]  = {0x4, 0xe, 0xe, 0xe, 0x1f, 0x0, 0x4};
  uint8_t note[8]  = {0x2, 0x3, 0x2, 0xe, 0x1e, 0xc, 0x0};
  uint8_t clock[8] = {0x0, 0xe, 0x15, 0x17, 0x11, 0xe, 0x0};
  uint8_t heart[8] = {0x0, 0xa, 0x1f, 0x1f, 0xe, 0x4, 0x0};
  uint8_t duck[8]  = {0x0, 0xc, 0x1d, 0xf, 0xf, 0x6, 0x0};
  uint8_t check[8] = {0x0, 0x1 ,0x3, 0x16, 0x1c, 0x8, 0x0};
  uint8_t cross[8] = {0x0, 0x1b, 0xe, 0x4, 0xe, 0x1b, 0x0};
  uint8_t retarrow[8] = {  0x1, 0x1, 0x5, 0x9, 0x1f, 0x8, 0x4};
  uint8_t arrow[8] = { 0x0,0x4,0x2,0x1f,0x2,0x4 };
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
  lcd.setCursor(0,0);
  lcd.print("Booting...");
  lcd.setCursor(13,3);
  lcd.print("v"); lcd.print(SOFT_VERSION);
  

  // eeprom get values
  EEPROM.begin(512);
  // wifi enable addr 0 size 1 - 0/1
  String configWifiEnable = eepromRead(WIFIENABLE,WIFIENABLE_SIZE);
  wifiEnable = configWifiEnable.toInt();

  // wifi ssid addr 1 size 32
  String configWifiSsid = eepromRead(WIFISSID,WIFISSID_SIZE);
  const char* wifiSsid = configWifiSsid.c_str();
  
  // wifi passwd addr 33 size 64
  String configWifiPasswd = eepromRead(WIFIPASS,WIFIPASS_SIZE);
  const char* wifiPasswd = configWifiPasswd.c_str();

  // autostartCharging
  String configAutoStart = eepromRead(AUTOSTART,AUTOSTART_SIZE);
  evseAutoStart = configAutoStart.toInt();

  // alreadyBoot
  String configAlreadyBoot = eepromRead(ALREADYBOOT,ALREADYBOOT_SIZE);
  alreadyBoot = configAlreadyBoot.toInt();

  // all stats
  consumptionTotal = (eepromReadInt(STATS_TOTAL) * 1000) + eepromReadInt(STATS_TOTAL_CENTS) ;
  
/*
  Serial.println("Config info :");
  Serial.print("wifiEnable : "); Serial.println(wifiEnable);
  Serial.print("wifiSsid : '"); Serial.print(wifiSsid); Serial.println("'");
  Serial.print("  sizeof : "); Serial.println(sizeof(wifiSsid));
  Serial.print("alreadyBoot : "); Serial.println(alreadyBoot);
  Serial.print("autoStart : "); Serial.println(evseAutoStart);
*/

  if (alreadyBoot == 0 && wifiEnable == 0)
  {
    wifiEnable = 1;
  }


  // Define Pins for button
  pinMode(inPin, INPUT_PULLUP);
  //digitalWrite();

  int wifiMode = 0;
  String wifiList;
  if (wifiEnable)
  {
    lcd.setCursor(1,1);
    lcd.print("wifi settings");
    
    if (strcmp(wifiSsid, "") != 0)
    {
      Serial.print("Wifi: Connecting to -");
      Serial.print(wifiSsid); Serial.println("-");

      // Connecting to wifi
      WiFi.mode(WIFI_AP);
      WiFi.disconnect();
      WiFi.mode(WIFI_STA);
      WiFi.hostname(wifiApSsid);
      
      bool wifiConnected = wifiConnect(wifiSsid,wifiPasswd);

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
  }


  if (wifiEnable)
  {
    // Start the server
    lcd.setCursor(1,1);
    lcd.print("load webserver");

    
    if (wifiMode == 1) // normal mode
    {
      server.on("/",webRoot);
      server.on("/reload",webReload);
      server.on("/write",webWrite);
      server.on("/debug",webDebug);
      server.on("/reboot",webReboot);
      server.on("/jsonInfo",webJsonInfo);
      server.on("/api/status",webApiStatus);
      server.on("/api/power",webApiPower);
      ip = WiFi.localIP();
    }
    else if (wifiMode == 2) // AP mode for config
    {
      server.on("/",webInitRoot);
      server.on("/setting",webInitSetting);
      ip = WiFi.softAPIP();
    }
    server.onNotFound(webNotFound);
    server.begin();

    Serial.print("Http: Server started at http://");
    Serial.print(ip);
    Serial.println("/");
    Serial.print("Status : ");
    Serial.println(WiFi.RSSI());

    lcd.setCursor(1,1);
    lcd.print("check update       ");
    lcd.noBacklight();
    
    String updateUrl = UPDATE_URL;
    Serial.println("Check for new update at : "); Serial.println(updateUrl);
    ESPhttpUpdate.rebootOnUpdate(1);
    t_httpUpdate_return ret = ESPhttpUpdate.update(updateUrl, SOFT_VERSION);
    //t_httpUpdate_return ret = ESPhttpUpdate.update(updateUrl, ESP.getSketchMD5() );

    Serial.print("return : "); Serial.println(ret);
    switch(ret) {
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
             Serial.print("Undefined HTTP_UPDATE Code: ");Serial.println(ret);
    }
    lcd.backlight();
    lcd.setCursor(1,1);
    lcd.print("update done  ");

    // get time();
    getTimeOnStartup();
    logger2("Starting%20bicoqueEvse");
    String messageToLog = "ConsoTotalg%20:%20"; messageToLog += consumptionTotal; messageToLog += "%20-%20"; messageToLog += SOFT_VERSION ; messageToLog += "%20"; messageToLog += SOFT_DATE;
    logger2(messageToLog);
  }

  lcd.setCursor(1,1);
  lcd.print("init EVSE     ");
  // Read EVSE info
  evseWrite("minAmpsValue", 0);
  evseWrite("modbus", 1);
  evseReload();


  // Screen
  screenDefault(page);

  if (alreadyBoot == 0)
  {
    eepromWrite(ALREADYBOOT, "1");
  }


  // debug local
  /*
  evseAllInfo();

  char* menuToPrint[30] ;
  int itemsToPrint = NUMITEMS(evseRegisters); 
  for ( int i =0 ; i < itemsToPrint ; i++) 
  { 
    String tempValue = evseRegisters[i];
    char tempChar[30];
    tempValue.toCharArray(tempChar, 30);
    menuToPrint[i] = tempChar; 
    Serial.print("copy : "); 
    Serial.print(tempChar);
    Serial.print(" --> ");
    Serial.print(menuToPrint[i]); 
    Serial.print(" --> ");
    Serial.println(evseRegisters[i]); 
  }
*/
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
            // evseUpdatePower(evsePowerOnLimit, FORCE_CURRENT);
            evseWrite("evseStatus", EVSE_ACTIVE);
          }
        }
        else if (evseStatus == 2) // EV present
        {
          // set curentAmp with the powerOnAmp
          // never be in this case with the firmware we have. but if long press, set current to launch charging
          // evseUpdatePower(evsePowerOnLimit, FORCE_CURRENT);
          evseWrite("evseStatus", EVSE_ACTIVE);
        }
        else if (evseStatus >= 3) // Charging
        {
          // set curentAmp to 0
          // Need to stop charging. Send 0 tu actual current
          // evseUpdatePower(0, FORCE_CURRENT);
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
      if (page >= 3) { page = 0; }
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
      //logger2("EvseStatus%20is%20set%20to%200");
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

			consumptionCounter += comsumptionInterval;
	    consumptionTotal   += int(comsumptionInterval + consumptionTotalTemp);
			consumptionTotalTemp  = comsumptionInterval + consumptionTotalTemp - int(comsumptionInterval + consumptionTotalTemp);

			// do not write to often. Life of EEPROM is 100K commit.
			// Write every 10mins
			if ( (timestamp - statsLastWrite) > 600 )
			{
          long consumptionToWrite      = int(consumptionTotal/1000);
          long consumptionToWriteCents = consumptionTotal - (consumptionToWrite * 1000);
	   			eepromWriteInt(STATS_TOTAL, consumptionToWrite );                           // Write in memroy the total consumptions
          eepromWriteInt(STATS_TOTAL_CENTS, consumptionToWriteCents ); // Write in memroy the total consumptions
          statsLastWrite = timestamp;
          //String messageToLog = "Write.In.Memory.periodic%20:%20"; messageToLog += consumptionToWrite; messageToLog += "KWh%20+%20"; messageToLog += consumptionToWriteCents; messageToLog += "Wh";
          //logger2(messageToLog);
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

      // save and clear counters
      long consumptionToWrite      = int(consumptionTotal/1000);
      long consumptionToWriteCents = consumptionTotal - (consumptionToWrite * 1000);
      eepromWriteInt(STATS_TOTAL, consumptionToWrite );                           // Write in memroy the total consumptions
      eepromWriteInt(STATS_TOTAL_CENTS, consumptionToWriteCents);                 // Write in memroy the total consumptions
                
      String messageToLog = "Write.In.Memory.stop.charging%20:%20"; messageToLog += consumptionToWrite; messageToLog += "KWh%20+%20"; messageToLog += consumptionToWriteCents ; messageToLog += "Wh";
      logger2(messageToLog);
		}
	}
  



  if (evseStatusBackup != evseStatus)
  {

    // if connection with evse was down
    if (evseStatusBackup == 0)
    {
    	evseConnectionProblem = 0;
    }

    String messageToLog = "Changing%20state%20from%20";
    messageToLog += evseStatusBackup;
    messageToLog += "%20to%20";
    messageToLog += evseStatus;
    logger2(messageToLog);

    // only remove the power from cable if we unplug the cable.
    // We need it if the car got a programming function to heater or cold the car.
    if (evseStatus > 0 && evseStatus < 2)
    {
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






