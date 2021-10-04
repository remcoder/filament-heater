#include <ESP32AnalogRead.h>

#include <SimpleDHT.h>

#include <U8g2lib.h>
#include <U8x8lib.h>

const int THERMAL_RUNAWAY_TIMOUT = 60; //seconds
const int MIN_BED_TEMP = 12;
const int MAX_BED_TEMP = 100;
const int MIN_AIR_TEMP = 12;
const int MAX_AIR_TEMP = 80;

/*
LCD12864 (ST7920 128X64)
 - clock: 18 
 - data: 23
 - CS: 5
*/
U8G2_ST7920_128X64_F_SW_SPI u8g2(U8G2_R0, /* clock=*/ 18, /* data=*/ 23, /* CS=*/ 5, /* reset=*/ 22); // ESP32

/*
Relay (digital out)
 - 21
*/
int pinHeaterRelay = GPIO_NUM_21;

/*
DHT22 (digital in/out)
 - 27
*/
int pinDHT22 = GPIO_NUM_33;
SimpleDHT22 dht22(pinDHT22);

/*
Bed thermistor (analog)
- 
*/
ESP32AnalogRead adc;
int pinBedThermistor = A6; // GPIO 14


bool isHeaterRelayOn = false;
int bedTemp = -1;
int airTemp = -1;
int humidity = -1;
int targetTemp = 40;
int targetOffset = 5; // seems to result in ±5 degree overshoot
int bedTempMaxDiff = 15;
int initialTemp = -1;
int fontBBXHeight = 11;
int lineHeight = fontBBXHeight;

void(* resetFunc) (void) = 0;

void gotoLine(int line) {
  int y = (line+1) * lineHeight;
  u8g2.setCursor(0, y);
}

void updateScreen(int temp, int hum) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_t0_11_mf); // w=6, h=11, 
  gotoLine(0);
  u8g2.print("current: ");
  u8g2.print(temp);
  u8g2.print("C, ");

  u8g2.print(hum);
  u8g2.print("H%");

  gotoLine(1);
  u8g2.print("bed temp: ");
  u8g2.print(bedTemp);
  u8g2.print("C");

  gotoLine(2);
  u8g2.print("target temp: ");
  u8g2.print(targetTemp);
  u8g2.print("C");


  if (isHeaterRelayOn) {
    gotoLine(4);
    u8g2.print("HEATING! ");
  }

  u8g2.sendBuffer();
}

void turnOnHeater(bool state) {
  digitalWrite(pinHeaterRelay, state);
}

void updateHeater() {
  isHeaterRelayOn = (airTemp < (targetTemp - targetOffset)) && bedTemp < (targetTemp + bedTempMaxDiff);
  turnOnHeater(isHeaterRelayOn);
}

void getAirTemp() {
  // read without samples.
  byte temperature = 0;
  byte humidity = 0;
  int err = SimpleDHTErrSuccess;
  if ((err = dht22.read(&temperature, &humidity, NULL)) != SimpleDHTErrSuccess) {
    Serial.print("Read DHT22 failed, err="); Serial.print(SimpleDHTErrCode(err));
    Serial.print(","); Serial.println(SimpleDHTErrDuration(err)); delay(2000);
    return;
  }
  
  // Serial.print("Sample OK: ");
  airTemp = (int)temperature;
  humidity = (int)humidity;
  // Serial.print(airTemp); Serial.print(" *C, ");
  // Serial.print(humidity); Serial.println(" RH%");

  delay(500);
}

void getBedTemp()
{
    // Converts input from a thermistor voltage divider to a temperature value.
    // The voltage divider consists of thermistor Rt and series resistor R0.
    // The value of R0 is equal to the thermistor resistance at T0.
    // You must set the following constants:
    //                  adcMax  ( ADC full range value )
    //                  analogPin (Arduino analog input pin)
    //                  invBeta  (inverse of the thermistor Beta value supplied by manufacturer).
    // Use Arduino's default reference voltage (5V or 3.3V) with this module.
    //

  const float invBeta = 1.00 / 3980.00;   // replace "Beta" with beta of thermistor

  const float adcMax = 4095.00;
  const float invT0 = 1.00 / 298.15;   // room temp in Kelvin

  int adcVal, i, numSamples = 100;
  float  K, C, F;

  adcVal = 0;
  for (i = 0; i < numSamples; i++)
   {
     adcVal = adcVal + analogRead(pinBedThermistor);
     delay(1);
   }
  adcVal = adcVal/numSamples;
  K = 1.00 / (invT0 + invBeta*(log ( adcMax / (float) adcVal - 1.00)));
  C = K - 273.15;                      // convert to Celsius
  //F = ((9.0*C)/5.00) + 32.00;   // convert to Fahrenheit
  
  if (bedTemp == -1)
    bedTemp = C;
  else
    // dampen the changes
    bedTemp = (int)((float)bedTemp * 0.8 + C * 0.2);
}

void checkForThermalRunaway() {
  if (initialTemp == -1) {
    initialTemp = bedTemp;
  }

  if (millis() > (THERMAL_RUNAWAY_TIMOUT * 1000) ){
    if (bedTemp == initialTemp) {
      turnOnHeater(false);
      haltOnError("THERMAL RUNAWAY");
    }
  }
}

void haltOnError(String error) {
  turnOnHeater(false);

  Serial.println("ERROR: " + error);
  
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_t0_11_mf); // w=6, h=11, 
  gotoLine(0);
  u8g2.print("ERROR: " + error);
  u8g2.sendBuffer();

  while (true) {
    delay(1000);
  }
}

void startUpTest() {

  Serial.print("Getting bed temp");

  do {
    Serial.print(".");
    getBedTemp();
  } while(bedTemp == -1);
  Serial.println(bedTemp);

  Serial.print("Getting air temp");
  do {
    Serial.print(".");
    getAirTemp();
  } while(airTemp == -1);
  Serial.println(airTemp);

  checkMinMaxTemps();
}

void checkMinMaxTemps() {

  if (bedTemp < MIN_BED_TEMP) {
    haltOnError("MIN_BED_TEMP");
  }

  if (bedTemp > MAX_BED_TEMP) {
    haltOnError("MAX_BED_TEMP");
  }

  if (airTemp < MIN_AIR_TEMP) {
    haltOnError("MIN_AIR_TEMP");
  }

  if (airTemp > MAX_AIR_TEMP) {
    haltOnError("MAX_AIR_TEMP");
  }
}

void setup()
{
  adc.attach(pinBedThermistor);

  pinMode(pinHeaterRelay, OUTPUT);
  turnOnHeater(false);

  Serial.begin(115200);

  u8g2.begin();
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB10_tn);
  u8g2.drawStr(0,20,"Filament Heater");
  u8g2.sendBuffer();
	
  startUpTest();
}

void loop() {
  getBedTemp();
  Serial.print("Bed: "); Serial.print(bedTemp); Serial.print(" *C, ");

  getAirTemp();
  Serial.print("Air: "); Serial.print(airTemp); Serial.print(" *C, ");
  Serial.print("Hum: "); Serial.print(humidity); Serial.println(" RH%");


  checkForThermalRunaway();
  checkMinMaxTemps();

  updateHeater();

  updateScreen(airTemp, humidity);
  
  // DHT22 sampling rate is 0.5HZ.
  delay(500);
}

