/*Internet LED Clock (ESP8266 and MAX7219)
Code written/modified by Scott Yeager to tweak clock/date timing but heavily borrowed
from and inspired by the following sources:

  Rui Santos
    Complete project details at https://RandomNerdTutorials.com/esp8266-nodemcu-date-time-ntp-client-server-arduino/
  
    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files.

  Techlogics
    https://techlogics.net/esp8266-clock-with-max7219-matrix-display-date-time-display/

To use code, update values in cofig box below for ssid, password, clock brightness, and utcOffset.
*/

#include "Arduino.h"
#include <ESP8266WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

// =======================================================================
// CHANGE YOUR CONFIG HERE:
// =======================================================================
const char *ssid     = "YOURNETWORK";
const char *password = "YOURNETWORKPASSWORD";
int bright = 4; // Adjust the brightness between 0 and 15
float DSTon = -4.0; // Time Zone setting during daylight savings (comment out if no DST)
float DSToff = -5.0; // Time Zone setting during standard time (comment out if no DST)
float utcOffset = -5.0; // Time Zone offset
// =======================================================================

WiFiClient client;
String date;

#define NUM_MAX 4
#define LINE_WIDTH 16
#define ROTATE  90

// for NodeMCU 1.0
#define DIN_PIN 15  // D8
#define CS_PIN  13  // D7
#define CLK_PIN 12  // D6

// Local libraries to include:
#include "max7219.h"
#include "fonts.h"

// Define NTP Client to get time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "north-america.pool.ntp.org");

// =======================================================================
//Week Days
String weekDays[7]={"SUN", "MON", "TUES", "WED", "THUR", "FRI", "SAT"};

//Month names
String months[12]={"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

//Initialize date string
String newDateclk=("HELLO");

//DST physical switch
const int DSTswitchPin = 14; // D5 GPIO pin connected to the switch
int DSTswitchState = 0; 

// =======================================================================
#define MAX_DIGITS 16
byte dig[MAX_DIGITS]={0};
byte digold[MAX_DIGITS]={0};
byte digtrans[MAX_DIGITS]={0};
int updCnt = 0;
int dots = 0;
long dotTime = 0;
long clkTime = 0;
int dx=0;
int dy=0;
byte del=0;
int h,m,s;
long localEpoc = 0;
long localMillisAtUpdate = 0;

// =======================================================================
// Setup loop

void setup() {
  // Set DSTswitch pin as input
  pinMode(DSTswitchPin, INPUT_PULLUP);

  // Initialize Serial Monitor
  Serial.begin(115200);
  initMAX7219();
  sendCmdAll(CMD_SHUTDOWN,1);
  
  Serial.print("Connecting WiFi ");
  WiFi.begin(ssid, password);
  printStringWithShift("Connecting ",16);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected: "); Serial.println(WiFi.localIP());

  // Initialize a NTPClient to get time
  timeClient.begin();
  timeClient.setTimeOffset(utcOffset*3600);
}

// =======================================================================
// Main loop of code

void loop() {

  // DST switch state loop, comment out if no DST
  DSTswitchState = digitalRead(DSTswitchPin); // Read the switch state
  if (DSTswitchState == HIGH) {
      utcOffset = DSTon; 
    } else {
      utcOffset = DSToff;; 
    }

  if(updCnt<=0) { // every 10 scrolls, ~450s=7.5m
    updCnt = 10;
    Serial.println("Getting data ...");
    printStringWithShift("  Getting data",15);
   
    getTime();
    updateNTPtimeClient();
    Serial.println("Data loaded");
    clkTime = millis();
  }
 
  if(millis()-clkTime > 20000 && !del && dots) { // clock for 15s, then scrolls for about 30s
    printStringWithShift(newDateclk.c_str(),40);
   delay(4000);
    updCnt--;
    clkTime = millis();
  }
  
  if(millis()-dotTime > 500) {
    dotTime = millis();
    dots = !dots;
  }
  
  updateTime();
  showAnimClock();

  // Adjusting LED intensity.
  // 12am to 6am, lowest intensity 0
  if ( (h == 0) || ((h >= 1) && (h <= 6)) ) sendCmdAll(CMD_INTENSITY, 0);
  // 6pm to 11pm, intensity = 1
  else if ( (h >=18) && (h <= 23) ) sendCmdAll(CMD_INTENSITY, 1);
  // max brightness during bright daylight
  else sendCmdAll(CMD_INTENSITY, bright);
}


// =======================================================================
// Clock animation function

void showAnimClock()
{
  byte digPos[4]={1,8,17,25};
  int digHt = 12;
  int num = 4; 
  int i;
  if(del==0) {
    del = digHt;
    for(i=0; i<num; i++) digold[i] = dig[i];
    dig[0] = h/10 ? h/10 : 10;
    dig[1] = h%10;
    dig[2] = m/10;
    dig[3] = m%10;
    for(i=0; i<num; i++)  digtrans[i] = (dig[i]==digold[i]) ? 0 : digHt;
  } else
    del--;
  
  clr();
  for(i=0; i<num; i++) {
    if(digtrans[i]==0) {
      dy=0;
      showDigit(dig[i], digPos[i], dig6x8);
    } else {
      dy = digHt-digtrans[i];
      showDigit(digold[i], digPos[i], dig6x8);
      dy = -digtrans[i];
      showDigit(dig[i], digPos[i], dig6x8);
      digtrans[i]--;
    }
  }
  dy=0;
  setCol(15,dots ? B00100100 : 0);
  refreshAll();
 delay(30);
}

// =======================================================================
void showDigit(char ch, int col, const uint8_t *data)
{
  if(dy<-8 | dy>8) return;
  int len = pgm_read_byte(data);
  int w = pgm_read_byte(data + 1 + ch * len);
  col += dx;
  for (int i = 0; i < w; i++)
    if(col+i>=0 && col+i<8*NUM_MAX) {
      byte v = pgm_read_byte(data + 1 + ch * len + 1 + i);
      if(!dy) scr[col + i] = v; else scr[col + i] |= dy>0 ? v>>dy : v<<-dy;
    }
}

// =======================================================================
void setCol(int col, byte v)
{
  if(dy<-8 | dy>8) return;
  col += dx;
  if(col>=0 && col<8*NUM_MAX)
    if(!dy) scr[col] = v; else scr[col] |= dy>0 ? v>>dy : v<<-dy;
}

// =======================================================================
int showChar(char ch, const uint8_t *data)
{
  int len = pgm_read_byte(data);
  int i,w = pgm_read_byte(data + 1 + ch * len);
  for (i = 0; i < w; i++)
    scr[NUM_MAX*8 + i] = pgm_read_byte(data + 1 + ch * len + 1 + i);
  scr[NUM_MAX*8 + i] = 0;
  return w;
}

// =======================================================================
void printCharWithShift(unsigned char c, int shiftDelay) {
  
  if (c < ' ' || c > '~'+25) return;
  c -= 32;
  int w = showChar(c, font);
  for (int i=0; i<w+1; i++) {
    delay(shiftDelay);
    scrollLeft();
    refreshAll();
  }
}

// =======================================================================
void printStringWithShift(const char* s, int shiftDelay){
  while (*s) {
    printCharWithShift(*s, shiftDelay);
    s++;
  }
}

// =======================================================================
// Get time and date from online (Google)

void getTime()
{
  WiFiClient client;
  if (!client.connect("www.google.com", 80)) {
    Serial.println("connection to google failed");
    return;
  }

  client.print(String("GET / HTTP/1.1\r\n") +
               String("Host: www.google.com\r\n") +
               String("Connection: close\r\n\r\n"));
  int repeatCounter = 0;
  while (!client.available() && repeatCounter < 10) {
    delay(500);
    //Code troubleshooting (uncomment if needed):
    //Serial.println(".");
    repeatCounter++;
  }

  String line;
  client.setNoDelay(false);
  while(client.connected() && client.available()) {
    line = client.readStringUntil('\n');
    line.toUpperCase();
    if (line.startsWith("DATE: ")) {
      date = "     "+line.substring(6, 17);
      h = line.substring(23, 25).toInt();
      m = line.substring(26, 28).toInt();
      s = line.substring(29, 31).toInt();
      localMillisAtUpdate = millis();
      localEpoc = (h * 60 * 60 + m * 60 + s);
      //Code troubleshooting (uncomment if needed):
      //date = currentDate();
    }
  }
  client.stop();
}

// =======================================================================
// Update time

void updateTime()
{
  long curEpoch = localEpoc + ((millis() - localMillisAtUpdate) / 1000);
  long epoch = fmod(round(curEpoch + 3600 * utcOffset + 86400L), 86400L);
  h = ((epoch  % 86400L) / 3600) % 24;
  m = (epoch % 3600) / 60;
  s = epoch % 60;
}

// =======================================================================
// Update time using NTP time client

void updateNTPtimeClient()
{
  timeClient.update();
  time_t epochTime = timeClient.getEpochTime();
  String formattedTime = timeClient.getFormattedTime();
  int currentHour = timeClient.getHours();
  int currentMinute = timeClient.getMinutes();
  int currentSecond = timeClient.getSeconds();
  String weekDay = weekDays[timeClient.getDay()];
  //Get a time structure
  struct tm *ptm = gmtime ((time_t *)&epochTime); 
  int monthDay = ptm->tm_mday;
  int currentMonth = ptm->tm_mon+1;
  String currentMonthName = months[currentMonth-1];
  int currentYear = ptm->tm_year+1900;
  //Print complete date:
  String currentDate = String(currentYear) + "-" + String(currentMonth) + "-" + String(monthDay);
  //Updated "if" statement adds additional spaces when day of month is 31 or less to keep comma off date screen:
  if (monthDay < 32) {
  newDateclk = " " + String(weekDay) + ",  " + String(monthDay) + "  " + String(currentMonthName);
  } else {
  newDateclk = " " + String(weekDay) + ", " + String(monthDay) + " " + String(currentMonthName);
  }
}

// =======================================================================
