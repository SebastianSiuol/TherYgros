#include <DHT.h>

#define DHTPIN 4

DHT dht(DHTPIN, DHT22);
float temperature;
float humidity;