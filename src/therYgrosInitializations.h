#include <DHT.h>
#include <time.h>


/* ===================================================== */
/* DHT variables and pins initialization */

#define DHTPIN 4
DHT dht(DHTPIN, DHT22);
float temperature;
float humidity;
String databasePath;
/* ========================================================================= */
/* Reset Button and Variables Initialization */

#define resetPin 5
#define statusPin 2
int buttonState = 0;
unsigned long buttonPressTime = 0;
bool buttonPressed = false;
bool provisioned_status = 0;
/* ========================================================================= */
/* NTP Time Initializaton*/

const char *ntp1Server = "time.nist.gov";
// const char *ntp2Server = "pool.ntp.org";
// const char *ntp3Server = "1.asia.pool.ntp.org";
const long gmtOffset_sec = 25200;
const int daylightOffset_sec = 3600;
bool timeRetrieved = false;
/* ========================================================================= */
/* TCP KeepAlive */

bool tcp_keep_alive_set = false;
int keepAlive = 1000; // Milliseconds
int keepIdle = 5;     // Seconds
int keepInterval = 5; // Seconds
int keepCount = 1;
/* ===================================================== */