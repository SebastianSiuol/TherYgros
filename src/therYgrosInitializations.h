#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <lwip/sockets.h>
#include <Preferences.h>
#include <RTClib.h>

/* Downloaded Packages */
#include <DHT.h>
#include <ArduinoJson.h>


const char* therygrosVersion = "TherYgros_1.0.2";


//          DHT variables and pins initialization 
/* ===================================================== */
#define DHTPIN 32
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
#define resetPin 23
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
StaticJsonDocument<60> decodedMessage;
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
//
//
//
//
//
/* RTC and Time*/
/* ===================================================== */
RTC_DS3231 rtc;
String retrievedTime;