#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <lwip/sockets.h>
#include <Preferences.h>

/* Downloaded Packages */
#include <DHT.h>
#include <ArduinoJson.h>



//          DHT variables and pins initialization 
/* ===================================================== */
#define DHTPIN 4
DHT dht(DHTPIN, DHT22);
float temperature;
float humidity;
String databasePath;
/* ===================================================== */
//
//
//
//
//
//          Reset Button and Variables Initialization 
/* ===================================================== */
#define resetPin 5
#define statusPin 2 //Onboard LED
int buttonState = 0;
unsigned long buttonPressTime = 0;
bool buttonPressed = false;
bool is_provisioned = false;
/* ===================================================== */
//
//
//
//
//
//          NTP Time Initializaton
/* ===================================================== */
const char *ntp1Server = "time.nist.gov";
const long gmtOffset_sec = 25200; //GMT+8 Timezone
const int daylightOffset_sec = 3600; //An Hour of Daylight Saving
bool timeRetrieved = false; //Initially false
const unsigned long time_retrieval_timeout = 15000; // 15 seconds
/* ===================================================== */
char timeYear[5];
char timeMonth[3];
char timeDay[3];
char timeHour[3];
char timeMinute[3];
char timeSecond[3];
/* ===================================================== */
//
//
//
//
//
/* TCP KeepAlive */
/* ===================================================== */
bool tcp_keep_alive_set = false;
int keepAlive = 1000; // Milliseconds
int keepIdle = 5;     // Seconds
int keepInterval = 5; // Seconds
int keepCount = 1;
/* ===================================================== */
//
//
//
//
//
/* TCP Initialization */
/* ===================================================== */
WiFiServer server(80); // <==== Creates a TCP Server with Port 80
/* ===================================================== */
//
//
//
//
//
/* Initializes JSON Doc */
/* ===================================================== */
StaticJsonDocument<60> receivedMessage;
/* ===================================================== */
//
//
//
//
//
/* Preferences (Flash Writing) */
/* ===================================================== */
Preferences preferences;
/* ===================================================== */