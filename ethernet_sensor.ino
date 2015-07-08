#include <SPI.h>
#include <Ethernet.h>
#include <PubSubClient.h>
#include "DHT.h"
#include <EEPROM.h>  // Contains EEPROM.read() and EEPROM.write()

// Sound Sensor Pin
#define SOUNDSENSORPIN 3
#define SOUNDLIMIT 10

// START DHT
// URL: 
#define DHTPIN 2     // what pin we're connected to

// Uncomment whatever type you're using!
//#define DHTTYPE DHT11   // DHT 11 
#define DHTTYPE DHT22   // DHT 22  (AM2302)
//#define DHTTYPE DHT21   // DHT 21 (AM2301)

// Connect pin 1 (on the left) of the sensor to +5V
// NOTE: If using a board with 3.3V logic like an Arduino Due connect pin 1
// to 3.3V instead of 5V!
// Connect pin 2 of the sensor to whatever your DHTPIN is
// Connect pin 4 (on the right) of the sensor to GROUND
// Connect a 10K resistor from pin 2 (data) to pin 1 (power) of the sensor

// Initialize DHT sensor for normal 16mhz Arduino
DHT dht(DHTPIN, DHTTYPE);
// NOTE: For working with a faster chip, like an Arduino Due or Teensy, you
// might need to increase the threshold for cycle counts considered a 1 or 0.
// You can do this by passing a 3rd parameter for this threshold.  It's a bit
// of fiddling to find the right value, but in general the faster the CPU the
// higher the value.  The default for a 16mhz AVR is a value of 6.  For an
// Arduino Due that runs at 84mhz a value of 30 works.
// Example to initialize DHT sensor for Arduino Due:
//DHT dht(DHTPIN, DHTTYPE, 30);
// END DHT

// A simple data logger for the Arduino analog pins
// https://learn.adafruit.com/adafruit-data-logger-shield/for-the-mega-and-leonardo

// how many milliseconds between grabbing data and logging it. 1000 ms is once a second
// #define LOG_INTERVAL  1000 // mills between entries (reduce to take more/faster data)

// how many milliseconds before writing the logged data permanently to disk
// set it to the LOG_INTERVAL to write each time (safest)
// set it to 10*LOG_INTERVAL to write all data every 10 datareads, you could lose up to 
// the last 10 reads if power is lost but it uses less power and is much faster!
// #define SYNC_INTERVAL 60000 // mills between calls to flush() - to write data to the card
// uint32_t syncTime = 0; // time of last sync()


// Update these with values suitable for your network.
byte mac[]    = {  0x20, 0x73, 0x76, 0x7C ,0xA0, 0xDA };
byte server[] = { 192, 168, 10, 230 };
// byte ip[]     = { 192, 168, 10, 210 };

EthernetClient ethClient;
PubSubClient client(server, 1883, callback, ethClient);

// the digital pins that connect to the LEDs
#define redLEDpin 13

int soundSum = 0;
boolean soundOn = false;
float rh; // humidity in %
float temp; // temp in celcius

//SETTINGS
// ID of the settings block
#define CONFIG_VERSION "TL1"

// Tell it where to store your config data in EEPROM
#define CONFIG_START 1


//  settings structure
struct StoreStruct {
  // This is for mere detection if they are your settings
  char version[4];
  // The variables of your settings
  //int logfileNumber;
} storage = {
  CONFIG_VERSION,
  // The default values
  //0
};

void loadConfig() {
  // To make sure there are settings, and they are YOURS!
  // If nothing is found it will use the default settings.
  if (EEPROM.read(CONFIG_START + 0) == CONFIG_VERSION[0] &&
      EEPROM.read(CONFIG_START + 1) == CONFIG_VERSION[1] &&
      EEPROM.read(CONFIG_START + 2) == CONFIG_VERSION[2])
    for (unsigned int t=0; t<sizeof(storage); t++)
      *((char*)&storage + t) = EEPROM.read(CONFIG_START + t);
}

void saveConfig() {
  for (unsigned int t=0; t<sizeof(storage); t++)
    EEPROM.write(CONFIG_START + t, *((char*)&storage + t));
}

void error(char *str)
{
  Serial.print("error: ");
  Serial.println(str);
  
  // red LED indicates error
  digitalWrite(redLEDpin, HIGH);
}

void normal(char *str)
{
  Serial.println(str);
  
  // red LED indicates error
  digitalWrite(redLEDpin, LOW);
}

void callback(char* topic, byte* payload, unsigned int length) {
  // handle message arrived
  sendSensorData();
}

void setup()
{
  Serial.begin(9600);
  Serial.println(F("Temp/RH sensor V0.1 started."));
  
  Serial.print(F("Loading config ..."));
  loadConfig();
  Serial.println(F("done"));
  
  // sound
  pinMode(SOUNDSENSORPIN, INPUT);
  
  // use debugging LEDs
  pinMode(redLEDpin, OUTPUT);
  
  connectToMQTT();
}

void connectToMQTT() {
  Serial.print(F("Starting network ..."));
  if(Ethernet.begin(mac) == 0) {
    error("failed");
    return;
  } else {
    normal("done");	
  }
  
  Serial.print(F("Connect to MQTT server..."));
  if (client.connect("kellerSensor")) {
    client.subscribe("keller/sensor/get");
    normal("done");
  } else {
    error("failed");
  }
}

void loop()
{
  handleSound();
  
  checkConnection();
  
  client.loop();
  
  delay(1000);
}

int keepalive = 0;
void checkConnection() {
  keepalive++;
  // check every 5 minutes 
  if(keepalive > 5 * 60) {
    keepalive = 0;
    if(!client.connected()) {
      connectToMQTT();
    }
  }
}

void handleSound(void) {
  int soundReading = 0;
  for(int i=0; i<50; i++) {
    // 1 = off, no sound; 0 = on, sound -> convert to 1 = on and 0 = off
    soundReading += ((digitalRead(SOUNDSENSORPIN) - 1 )* -1);
    delay(10);
  }
  if(soundReading == 0 && soundSum > 0) {
      soundSum--;
  } else if(soundReading >= 1 && soundSum < SOUNDLIMIT) {
    soundSum++;
  }
  // sound hysterese
  if(soundSum == SOUNDLIMIT && !soundOn) {
    soundOn = true;
  } else if(soundSum == 0 && soundOn) {
    soundOn = false;
  }
#if ECHO_TO_SERIAL
  Serial.print("soundSum:");
  Serial.print(soundSum);
  Serial.print(" soundOn:");
  Serial.println(soundOn);
#endif
  
}

void sendSensorData() {
  digitalWrite(redLEDpin, HIGH);
  
  // Reading temperature or humidity takes about 250 milliseconds!
  // Sensor readings may also be up to 2 seconds 'old' (its a very slow sensor)
  rh = dht.readHumidity();
  // Read temperature as Celsius
  temp = dht.readTemperature();
  
#if ECHO_TO_SERIAL
  Serial.print(", ");   
  Serial.print(temp);
  Serial.print(", ");    
  Serial.print(rh);
  Serial.print(", ");    
  Serial.print(soundOn);
#endif //ECHO_TO_SERIAL
  
  char buffer[10];
  dtostrf(temp, 4, 2, buffer);
  client.publish("keller/sensor/temp", buffer);
  
  dtostrf(rh, 4, 2, buffer);
  client.publish("keller/sensor/rh", buffer);
   
  sprintf(buffer, "%d", soundOn?1:0);
  client.publish("keller/sensor/lueftung", buffer);

  digitalWrite(redLEDpin, LOW);
}



