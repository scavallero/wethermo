//
//   Firmware.ino
//   Arduino firmware for Wifi controlled Chronothermostat
//   Copyright (C) 2016  Salvatore Cavallero (salvatore.cavallero@gmail.com)
//
//    This program is free software: you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>
#include <UTFT.h>
#include <TimeLib.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#define NTP_PACKET_SIZE 48

#define TEMP_X_POS    150
#define TEMP_Y_POS    100
#define MODE_X_POS    250
#define MODE_Y_POS    185
#define CLOCK_X_POS    20
#define CLOCK_Y_POS   170
#define CRONO_X_POS    16
#define CRONO_Y_POS     4
#define TARGET_X_POS  190
#define TARGET_Y_POS  200

#define AUTO_MODE 0
#define OFF_MODE  1
#define HEAT_MODE 2
#define NO_ICE_MODE 3

#define DEFAULT_OFFSET 0.0
#define DEFAULT_THRESHOLD 0.2
#define VER  "1.2"

// ****** Globals ******

byte ThermoStatus = AUTO_MODE;

char ssid[] = "XXXXX";  
char pass[] = "XXXXXXXXXX";

extern uint8_t SmallFont[];
extern uint8_t BigFont[];
extern uint8_t SevenSegNumFont[];
extern uint8_t SymbolFonts32x32[];

int timeZone = 1; // Local time zone
int ntp_count = 0;

char myclock[] = "00:00";
const char * s_day[] = {"DOM","LUN","MAR","MER","GIO","VEN","SAB"};
const char * s_month[] = {"GEN","FEB","MAR","APR","MAG","GIU",
                          "LUG","AGO","SET","OTT","NOV","DIC"};

byte packetBuffer[NTP_PACKET_SIZE];

//               00   01   02   03   04   05   06   07   08   09   10   11  
byte crono[] = { 1,1, 1,1, 1,1, 1,1, 1,1, 1,1, 1,2, 2,2, 1,1, 1,1, 1,1, 1,1, 
//               12   13   14   15   16   17   18   19   20   21   22   23      
                 2,2, 1,1, 1,1, 1,1, 1,1, 3,3, 3,3, 3,3, 3,3, 3,3, 3,3, 1,1};

float Temp;
float T_Target[] = {-999, 16.0, 19.0, 19.5, 20.0};
float T_Threshold = DEFAULT_THRESHOLD;
float Offset = DEFAULT_OFFSET;

boolean heat = false;

boolean toggle=false;
int mm; // minute();
int hh; // hour();
int dd; // weekday();
int DD; // day();
int MM; // month();

const int relayPin = D1;

IPAddress timeServerIP;
WiFiUDP udp;
ESP8266WebServer server(80);
UTFT myGLCD(ILI9341_S5P, 15, 0, 4);
OneWire oneWire(D4);

DallasTemperature sensors(&oneWire);
DeviceAddress devices[10]; // debug arrays to hold device addresses
int devicesFound = 0;

void getClock(int x,int y) {
  mm = minute();
  hh = hour();
  dd = weekday();
  DD = day();
  MM = month();
  
  // Edit the digit
  myclock[3]=(mm/10)+48;
  myclock[4]=(mm%10)+48;
  myclock[0]=(hh/10)+48;
  myclock[1]=(hh%10)+48;
    
  // Handle the toggle heart beat
  if (toggle) {
    toggle = false;
    myclock[2]=' ';
  } else {
    toggle = true;
    myclock[2]=':';
  }
  
  myGLCD.setColor(VGA_WHITE);
  myGLCD.setBackColor(VGA_BLACK);
  myGLCD.setFont(SymbolFonts32x32);
  myGLCD.print(myclock,x,y);
  myGLCD.setFont(BigFont);
  myGLCD.print(s_day[dd-1],x,y+32);
  myGLCD.printNumI(DD,x+64,y+32,2,' ');
  myGLCD.print(s_month[MM-1],x+112,y+32);
}

boolean wifiConnect() {
  int count=0;
  while (WiFi.status() != WL_CONNECTED) {
    count++;
    delay(500);
    if (count == 60) return false;
  }
  return true;
}

unsigned long sendNTPpacket(IPAddress& address)
{
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  udp.beginPacket(address, 123); //NTP requests are to port 123
  udp.write(packetBuffer, NTP_PACKET_SIZE);
  udp.endPacket();
}

boolean getNtpTime() {
   boolean WaitUdp = true;
   unsigned long secsSince1900;
   int count=0,cb;
   sendNTPpacket(timeServerIP);
   while(WaitUdp) {
     cb = udp.parsePacket();
     count ++;
     if (cb) {
       WaitUdp = false;
       udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer

       // convert four bytes starting at location 40 to a long integer
       secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
       secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
       secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
       secsSince1900 |= (unsigned long)packetBuffer[43];
       
       setTime( secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR);
    
     } else {
       if (count == 60) return false;
       delay(500);
     }
   }

   return true;
}

float getTemp(int x,int y) {

  static int tempCount = 0;
  static float tempOld = 0.0; 
  float tempC;
  int t_integer;
  int t_decimal;

  sensors.requestTemperatures();

  if (tempCount == 0) {
    tempC = sensors.getTempC(devices[1])+Offset;
  } else {
    // Avoid continous temperature reading
    tempC = tempOld;
    delay(500);
  }
  
  t_integer = (int)tempC;
  t_decimal = (int)((tempC-t_integer)*10.0);
  
  myGLCD.setColor(VGA_BLUE);
  myGLCD.setBackColor(VGA_BLACK);
  myGLCD.setFont(SevenSegNumFont);
  myGLCD.printNumI(t_integer,x,y);
  myGLCD.fillRect(x+65,y+47,x+68,y+50);
  myGLCD.printNumI(t_decimal,x+70,y);
  myGLCD.setFont(SymbolFonts32x32);
  myGLCD.print("$",x+102,y);

  tempOld = tempC;
  tempCount++;

  if (tempCount >= 10) 
    tempCount = 0;
    
  return tempC;
}

byte drawCrono(int x, int y) {
  int i;
  int deltay=8;
  
  for (i=0;i<48;i++) {
    switch(crono[i]) {
      case 1:
        myGLCD.setColor(VGA_BLUE);
        myGLCD.fillRect(i*6+x,y+deltay+deltay,i*6+x+4,y+deltay+deltay+4);
        break;
      case 2: 
        myGLCD.setColor(VGA_BLUE);
        myGLCD.fillRect(i*6+x,y+deltay+deltay,i*6+x+4,y+deltay+deltay+4);
        myGLCD.setColor(VGA_YELLOW);
        myGLCD.fillRect(i*6+x,y+deltay,i*6+x+4,y+deltay+4);
        break;
      case 3:
        myGLCD.setColor(VGA_BLUE);
        myGLCD.fillRect(i*6+x,y+deltay+deltay,i*6+x+4,y+deltay+deltay+4);
        myGLCD.setColor(VGA_YELLOW);
        myGLCD.fillRect(i*6+x,y+deltay,i*6+x+4,y+deltay+4);
        myGLCD.setColor(VGA_RED);
        myGLCD.fillRect(i*6+x,y,i*6+x+4,y+4);
        break; 
    }
  }

  i = hh*2;
  if (mm >= 30) i++;
  
  // Handle the toggle heart beat
  if(toggle) {
      myGLCD.setColor(VGA_BLACK);
      myGLCD.fillRect(i*6+x,y+deltay+deltay,i*6+x+4,y+deltay+deltay+4);
      myGLCD.fillRect(i*6+x,y+deltay,i*6+x+4,y+deltay+4);
      myGLCD.fillRect(i*6+x,y,i*6+x+4,y+4);   
  }

  return crono[i];
}

byte getCrono() {
  int i;
  
  i = hh*2;
  if (mm >= 30) i++;
  
  return crono[i];
}

void InitDisplay() {
  myGLCD.InitLCD();
  myGLCD.clrScr();

  switch(ThermoStatus) {
    case AUTO_MODE:
        myGLCD.setColor(VGA_BLACK);
        myGLCD.setBackColor(VGA_LIME);
        myGLCD.setFont(SmallFont);
        myGLCD.print("AUTO",MODE_X_POS,MODE_Y_POS);
        myGLCD.setBackColor(VGA_BLACK);
        myGLCD.setFont(SymbolFonts32x32);
        myGLCD.print(" ",TEMP_X_POS-40,TEMP_Y_POS+9);
        break;
    case HEAT_MODE:
        myGLCD.setColor(VGA_BLACK);
        myGLCD.setBackColor(VGA_RED);
        myGLCD.setFont(SmallFont);
        myGLCD.print("HEAT",MODE_X_POS,MODE_Y_POS);
        myGLCD.setBackColor(VGA_BLACK);
        myGLCD.setFont(SymbolFonts32x32);
        myGLCD.print(" ",TEMP_X_POS-40,TEMP_Y_POS+9);
        break;
    case OFF_MODE:
        myGLCD.setColor(VGA_BLUE);
        myGLCD.setBackColor(VGA_BLACK);
        myGLCD.setFont(SymbolFonts32x32);
        myGLCD.print("!",TEMP_X_POS-40,TEMP_Y_POS+9);
        myGLCD.setBackColor(VGA_BLACK);
        myGLCD.setFont(SmallFont);
        myGLCD.print("    ",MODE_X_POS,MODE_Y_POS);
        myGLCD.setFont(BigFont);
        myGLCD.print("     ",TARGET_X_POS+48,TARGET_Y_POS);
        break;  
  }
  myGLCD.setColor(VGA_RED);
  myGLCD.setBackColor(VGA_BLACK);
  myGLCD.setFont(SymbolFonts32x32);
  
  if (heat) {
    myGLCD.print("\"",TEMP_X_POS-40,TEMP_Y_POS+9);
  } else {
    myGLCD.print(" ",TEMP_X_POS-40,TEMP_Y_POS+9);
  }
}

void heatOn() {
  myGLCD.setColor(VGA_RED);
  myGLCD.setBackColor(VGA_BLACK);
  myGLCD.setFont(SymbolFonts32x32);
  heat = true;
  myGLCD.print("\"",TEMP_X_POS-40,TEMP_Y_POS+9);
  digitalWrite(relayPin, HIGH);

  // Reset display caused by EMC pulse
  delay(1000);
  InitDisplay();
}

void heatOff() {
  myGLCD.setColor(VGA_RED);
  myGLCD.setBackColor(VGA_BLACK);
  myGLCD.setFont(SymbolFonts32x32);
  heat = false;
  myGLCD.print(" ",TEMP_X_POS-40,TEMP_Y_POS+9);
  digitalWrite(relayPin, LOW);

  // Reset display caused by EMC pulse
  delay(1000);
  InitDisplay();
}

/********** API REST **********/

void handleNotFound(){
  String message = "URL Not Found (ERROR 404) \n\n";
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

void handleHeat() {
  
  ThermoStatus = HEAT_MODE;
  server.send(200, "text/plain", "Set mode HEAT");
  myGLCD.setColor(VGA_BLACK);
  myGLCD.setBackColor(VGA_RED);
  myGLCD.setFont(SmallFont);
  myGLCD.print("HEAT",MODE_X_POS,MODE_Y_POS);
  myGLCD.setBackColor(VGA_BLACK);
  myGLCD.setFont(SymbolFonts32x32);
  myGLCD.print(" ",TEMP_X_POS-40,TEMP_Y_POS+9);
}

void handleOff() {
 
  ThermoStatus = OFF_MODE;
  heatOff();
  myGLCD.setColor(VGA_BLUE);
  myGLCD.setBackColor(VGA_BLACK);
  myGLCD.setFont(SymbolFonts32x32);
  myGLCD.print("!",TEMP_X_POS-40,TEMP_Y_POS+9);
  server.send(200, "text/plain", "Set mode OFF");
  myGLCD.setBackColor(VGA_BLACK);
  myGLCD.setFont(SmallFont);
  myGLCD.print("    ",MODE_X_POS,MODE_Y_POS);
  myGLCD.setFont(BigFont);
  myGLCD.print("     ",TARGET_X_POS+48,TARGET_Y_POS);
}

void handleAuto() {
  ThermoStatus = AUTO_MODE;
  server.send(200, "text/plain", "Set mode AUTO");
  myGLCD.setColor(VGA_BLACK);
  myGLCD.setBackColor(VGA_LIME);
  myGLCD.setFont(SmallFont);
  myGLCD.print("AUTO",MODE_X_POS,MODE_Y_POS);
  myGLCD.setBackColor(VGA_BLACK);
  myGLCD.setFont(SymbolFonts32x32);
  myGLCD.print(" ",TEMP_X_POS-40,TEMP_Y_POS+9);
}

void handleGet_T() {
  String message = String(Temp);
  
  server.send(200, "text/plain", message);
}

void handleGet_Offset() {
  String message = String(Offset);
  
  server.send(200, "text/plain", message);
}

void handleGet_Threshold() {
  String message = String(T_Threshold);
  
  server.send(200, "text/plain", message);
}

void handleGet_T1() {
  String message = String(T_Target[1]);
  
  server.send(200, "text/plain", message);
}

void handleGet_T2() {
  String message = String(T_Target[2]);
  
  server.send(200, "text/plain", message);
}

void handleGet_T3() {
  String message = String(T_Target[3]);
  
  server.send(200, "text/plain", message);
}

void handleGet_T4() {
  String message = String(T_Target[4]);
  
  server.send(200, "text/plain", message);
}

void handleMode() {
  String message = "";
  
  switch(ThermoStatus) {
    case AUTO_MODE:
        message = "AUTO";
        break;
    case HEAT_MODE:
        message = "HEAT";
        break;
    case OFF_MODE:
        message = "OFF";
        break;  
    default:
        message = "UNKNOWN";
  }
  
  server.send(200, "text/plain", message);
}

void handleRelays() {
  String message = "";
  
  if (heat) {
    message = "ON";
  } else {
    message = "OFF";
  }
  
  server.send(200, "text/plain", message);
}

void handleRoot() {

  String message = "WeThermo ";
  message += VER;
  
  server.send(200, "text/plain", message);
}

void handleInfo() {
  byte C;
  int i;
  float tempC;
  
  tempC = sensors.getTempC(devices[0])+Offset;
  
  
  String message = "{\n  \"Version\":\""; message += VER; message +="\",";
  message += "\n  \"Temp\":"; message += String(Temp); message +=",";
  message += "\n  \"TempDev0\":"; message += String(tempC); message +=",";
  message += "\n  \"Offset\":"; message += String(Offset); message +=",";
  message += "\n  \"Threshold\":"; message += String(T_Threshold); message +=",";
  message += "\n  \"TargetT1\":"; message += String(T_Target[1]); message +=",";
  message += "\n  \"TargetT2\":"; message += String(T_Target[2]); message +=",";
  message += "\n  \"TargetT3\":"; message += String(T_Target[3]); message +=",";
  message += "\n  \"TargetT4\":"; message += String(T_Target[4]); message +=",";
  message += "\n  \"mode\":";
  switch(ThermoStatus) {
    case AUTO_MODE:
        message += "\"AUTO\",";
        break;
    case HEAT_MODE:
        message += "\"HEAT\",";
        break;
    case OFF_MODE:
        message += "\"OFF\",";
        break;  
    default:
        message += "\"UNKNOWN\",";
  }
  message += "\n  \"crono\":[";
  for (i=0;i<47;i++) {
    message += String(crono[i]); message +=",";
  }
  message += String(crono[47]); message +="],";   
  C=getCrono();
  message += "\n  \"level\":";
  message += String(C); message +=",";

  message += "\n  \"time\":";
  message += "\""+String(hh)+":"+String(mm)+"\""; message +=",";
  message += "\n  \"date\":";
  message += "\""+String(s_day[dd-1])+" "+String(DD)+" "+String(s_month[MM-1])+"\""; message +=",";
  
  message += "\n  \"relays\":";
  if (heat) {
    message += "\"ON\"";
  } else {
    message += "\"OFF\"";
  }
  message += "\n}";
  
  server.send(200, "text/plain", message);
}

void handleSet() {
  boolean Ok = true;
  float T;
  String message = "Set Error";
  
  for (uint8_t i=0; i<server.args(); i++){
    if (server.argName(i).equals("T1")) {
      T =  server.arg(i).toFloat();
      if (T > 0.0 && T < T_Target[2]) {
        T_Target[1] = T;
      } else {
        Ok = false;
        message = "Set invalid T1 value: "+server.arg(i);
      }
    } else if (server.argName(i).equals("T2")) {
      T =  server.arg(i).toFloat();
      if (T > T_Target[1] && T < T_Target[3]) {
        T_Target[2] = T;
      } else {
        Ok = false;
        message = "Set invalid T2 value: "+server.arg(i);
      }
    } else if (server.argName(i).equals("T3")) {
      T =  server.arg(i).toFloat();
      if (T > T_Target[2] && T < T_Target[4]) {
        T_Target[3] = T;
      } else {
        Ok = false;
        message = "Set invalid T3 value: "+server.arg(i);
      }
    } else if (server.argName(i).equals("T4")) {
      T =  server.arg(i).toFloat();
      if (T > T_Target[3] && T < 30.0) {
        T_Target[4] = T;
      } else {
        Ok = false;
        message = "Set invalid T4 value: "+server.arg(i);
      }
    } else if (server.argName(i).equals("Offset")) {
      T =  server.arg(i).toFloat();
      if (T >= -10.0 && T <= 10.0) {
        Offset = T;
      } else {
        Ok = false;
        message = "Set invalid Offset value: "+server.arg(i);
      }
    } else if (server.argName(i).equals("Threshold")) {
      T =  server.arg(i).toFloat();
      if (T >= -0.5 && T <= 0.5) {
        T_Threshold = T;
      } else {
        Ok = false;
        message = "Set invalid Threshold value: "+server.arg(i);
      }
    }
  }

  if (Ok) {
    server.send(200, "text/plain", "Set Ok");
  } else {
    server.send(400, "text/plain", message);
  }
}

void handleInitDisplay() {
  String message = "Init Display Ok";

  InitDisplay();
  
  server.send(200, "text/plain", message);
}

// function to print a device address
void printAddress(DeviceAddress deviceAddress)
{
  for (uint8_t i = 0; i < 8; i++)
  {
    // zero pad the address if necessary
    if (deviceAddress[i] < 16) Serial.print("0");
    Serial.print(deviceAddress[i], HEX);
  }
}

void setup() {

  Serial.begin(115200);
  Serial.println("WeThermo Serial Debug");
  
  boolean SetupLoop=true;

  // Setup mode fore relayPin
  pinMode(relayPin, OUTPUT);
  
  // Setup the LCD
  myGLCD.InitLCD();

  // Setup Wifi
  while(SetupLoop) {
    myGLCD.clrScr();
    
    WiFi.begin(ssid, pass);
      
    myGLCD.setColor(VGA_WHITE);
    myGLCD.setBackColor(VGA_BLACK);
    myGLCD.setFont(BigFont);
    myGLCD.print("WeThermo v1.2",CENTER,8);
    myGLCD.print("Wifi ...... ",16,32); 
    if (wifiConnect()) {
      myGLCD.setColor(VGA_LIME);
      myGLCD.print("[OK]",224,32);
      SetupLoop = false;
    } else {
      myGLCD.setColor(VGA_RED);
      myGLCD.print("[FAIL]",224,32);
      myGLCD.setColor(VGA_YELLOW);
      myGLCD.print("Retry in 5 secs.",CENTER,160);
      delay(5000);
    }
  }
  myGLCD.setColor(VGA_WHITE);
  myGLCD.print("IP: ",16,48);
  myGLCD.print(WiFi.localIP().toString(),80,48); 

  // Setup time server IP and udp connection
  WiFi.hostByName("time.nist.gov", timeServerIP); 
  udp.begin(2390);

  myGLCD.print("Ntp ....... ",16,64);
  if (getNtpTime()) {
    myGLCD.setColor(VGA_LIME);
    myGLCD.print("[OK]",224,64);
    SetupLoop = false;
  } else {
    myGLCD.setColor(VGA_RED);
    myGLCD.print("[FAIL]",224,64);
  }

  // Setup DS18B20
  sensors.begin();
  Serial.print("Locating devices...");
  Serial.print("Found ");
  Serial.print(sensors.getDeviceCount(), DEC);
  Serial.println(" devices.");

  devicesFound = sensors.getDeviceCount();  

  // report parasite power requirements
  Serial.print("Parasite power is: "); 
  if (sensors.isParasitePowerMode()) Serial.println("ON");
  else Serial.println("OFF");

  for (int i = 0; i < devicesFound; i++)
    if (!sensors.getAddress(devices[i], i)) 
      Serial.println("Unable to find address for Device" + i); 

  // show the addresses we found on the bus
  for (int i = 0; i < devicesFound; i++)
  {    
    Serial.print("Device " + (String)i + " Address: ");
    printAddress(devices[i]);
    Serial.println();
  }

  for (int i = 0; i < devicesFound; i++)
    sensors.setResolution(devices[i], 12);
    
  myGLCD.setColor(VGA_WHITE);
  myGLCD.print("DS18B20 ... ",16,80);
  if (devicesFound > 0) {
    myGLCD.setColor(VGA_LIME);
    myGLCD.print("[OK]",224,80);
  } else {
    myGLCD.setColor(VGA_RED);
    myGLCD.print("[FAIL]",224,80);
  }

  server.on("/", handleRoot);
  server.on("/heat", handleHeat);
  server.on("/off", handleOff);
  server.on("/auto", handleAuto);
  server.on("/set",handleSet);
  server.on("/get/Offset",handleGet_Offset);
  server.on("/get/Threshold",handleGet_Threshold);
  server.on("/get/T",handleGet_T);
  server.on("/get/T1",handleGet_T1);
  server.on("/get/T2",handleGet_T2);
  server.on("/get/T3",handleGet_T3);
  server.on("/get/T4",handleGet_T4);
  server.on("/display",handleInitDisplay);
  server.on("/info",handleInfo);
  server.on("/mode",handleMode);
  server.on("/relays",handleRelays);
  server.onNotFound(handleNotFound);
  server.begin();
  
  myGLCD.setColor(VGA_YELLOW);
  myGLCD.print("Starting ...",CENTER,160);
  delay(3000);
  myGLCD.clrScr();

  /*** START IN AUTO-MODE ***/
  ThermoStatus = AUTO_MODE;
  myGLCD.setColor(VGA_BLACK);
  myGLCD.setBackColor(VGA_LIME);
  myGLCD.setFont(SmallFont);
  myGLCD.print("AUTO",MODE_X_POS,MODE_Y_POS);
  heatOff();
}


void loop() {
  float Ttarget;
  byte C;
  
  getClock(CLOCK_X_POS,CLOCK_Y_POS);
  Temp=getTemp(TEMP_X_POS,TEMP_Y_POS);
  C=drawCrono(CRONO_X_POS,CRONO_Y_POS);



  /************* HEATING *************/
  myGLCD.setBackColor(VGA_BLACK);
  myGLCD.setColor(VGA_RED);
  if (heat) {
    myGLCD.print("\"",TEMP_X_POS-40,TEMP_Y_POS+9);
  } else {
    myGLCD.print(" ",TEMP_X_POS-40,TEMP_Y_POS+9);
  }

  switch(ThermoStatus) {
      case AUTO_MODE:
      case HEAT_MODE:
        if (ThermoStatus == HEAT_MODE) {
          C = 4;
          myGLCD.setColor(VGA_RED);
        } else {
          myGLCD.setColor(VGA_LIME);
        }
        
        Ttarget = T_Target[C];
        
        myGLCD.setBackColor(VGA_BLACK);
        myGLCD.setFont(BigFont);
        //myGLCD.print("T",TARGET_X_POS,TARGET_Y_POS);
        //myGLCD.printNumI(C,TARGET_X_POS+16,TARGET_Y_POS);
        myGLCD.printNumF(Ttarget,1,TARGET_X_POS+48,TARGET_Y_POS,'.',4,' ');
        
        if (heat) {
          if (Temp >= (Ttarget+T_Threshold)) {
            heatOff();
          }
        } else {
           if (Temp <= (Ttarget-T_Threshold)) {
             heatOn();
           }
        }
        break;
        
      case OFF_MODE:
        myGLCD.setColor(VGA_BLUE);
        myGLCD.setBackColor(VGA_BLACK);
        myGLCD.setFont(SymbolFonts32x32);
        myGLCD.print("!",TEMP_X_POS-40,TEMP_Y_POS+9);
        break;  
  }
  
  /************* UPDATE NTP TIME *************/ 
  
  ntp_count++;
  if (ntp_count > 2000) {
    ntp_count = 0;
    getNtpTime();
  }

  /************* CHECK-WIFI *************/ 

  myGLCD.setColor(VGA_BLUE);
  myGLCD.setBackColor(VGA_BLACK);
  myGLCD.setFont(SymbolFonts32x32);
  if (WiFi.status() == WL_CONNECTED) {
    myGLCD.print("#",TEMP_X_POS-80,TEMP_Y_POS+9);
  } else {
    myGLCD.print(" ",TEMP_X_POS-80,TEMP_Y_POS+9);
  }

  server.handleClient();
  delay (1000);
}
