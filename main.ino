/*
  New in this version is an I2C display,
  and motion sensing backlight control
*/
// This #include statement was automatically added by the Particle IDE.
#include "EmonLib.h"

// This #include statement was automatically added by the Particle IDE.
#include <LiquidCrystal_I2C.h>
#include <Wire.h>

// This #include statement was automatically added by the Particle IDE.
#include "EmonLib.h"

// This #include statement was automatically added by the Particle IDE.

#include <time.h>

// This #include statement was automatically added by the Particle IDE.
#include <string>
STARTUP(WiFi.selectAntenna(ANT_EXTERNAL));
char city[] = "Klamath Falls"; //up to 24 characters, this will be overwritten with API info on connect()
char zip[] = "97601"; //only for US, no +4
//char country[] = "US"; // 2-letter code only
bool useZip = true;//Here.com can give location key by either zip and country, or city and country
char appID[] = "SiGnUp4FReeUserKey"; //Here.com user keys
char appCode[] = "GitYerOwnAPPCode";
char pubString[200] = "";
char DeviceID[26] = "";
char condition[20] = "";

void onFlag(void);
void setRunFlag(void);
void getWeather(void) ;//Prototype for timer callback method
void printToLCD(void);
void pubLish(void);
int SetWattHrCount(String somenum);
int Publish_Now(String command);
int GetShutDown(String command);

float RealPower, ApparentPower, PowerFactor, LineVoltage, LineCurrent, WattHrCount = 0, PowerCount, ApparentPowerCount, temp;
unsigned long looptime, lastloop;
bool pub0 = false, pub10 = false, pub20 = false, pub30 = false,  pub40 = false, pub50 = false, runflag = true;
unsigned int printtick, loopcount, calls;
int8_t zone = 0;

struct locationData {
  //char locationKey[10];

  int8_t utcOffset;
  uint8_t magic_number;
};
locationData myLocation = { -7, 0};

struct sunTimes {
  char Hour[3];
  char Minute[3];
  uint8_t magic_number;
};
sunTimes sunrise = {"04", "00", 0};
sunTimes sunset = {"23", "00", 0};

EnergyMonitor emon1;             // Create an instance
LiquidCrystal_I2C lcd(0x27, 20, 4);
///get the weather every 10 min, set the wh counter to shut off at sundown.
Timer thymer(600 * 1000, getWeather);
Timer thymer2(10 * 3600 * 1000, onFlag, true); //needs changing

void setup() {

  Serial.begin(9600);
  while (!Serial) { //really only need this for debugging.
    delay(500)  ; //Just wait for serial to connect
  }
  //for debugging only
  /*  while (Serial.available()==0){
        delay(500); //just wait for incoming message
    }
    while (Serial.available()>0){
        Serial.write(Serial.read());
    } */

  Serial.printf("Currently %f hrs offset, and setting to 0", Time.getDSTOffset());
  Serial.println();
  Time.zone(zone); //set to UTC, which is probably default anyway.
  String dID = System.deviceID();//don't really want to use strings, but...
  dID.toCharArray(DeviceID, 26);
  Particle.function("SetWattHr", SetWattHrCount);
  Particle.function("Publish", Publish_Now);
  Particle.function("GetShutdown", GetShutdown);

  //just printing and subscribing using the same buffer here - why not??
  /*snprintf(pubString, sizeof(pubString), "%s_locationHook", DeviceID );
    Particle.subscribe(pubString, getLocationHandler, MY_DEVICES); //<< subscribe to the event
    Serial.printf("Subscribed to %s", pubString);
    Serial.println(); **/
  snprintf(pubString, sizeof(pubString), "%s_hereBweather", DeviceID );
  Particle.subscribe(pubString, getWeatherHandler, MY_DEVICES);
  Serial.printf("Subscribed to %s", pubString);
  Serial.println();
  snprintf(pubString, sizeof(pubString), "%s_hereBsunTime", DeviceID );
  Particle.subscribe(pubString, sunTimesHandler, MY_DEVICES); //<< subscribe to the event
  Serial.printf("Subscribed to %s", pubString);
  Serial.println();

  //initialize lcd screen
  lcd.init();
  // turn on the backlight
  lcd.backlight();

  emon1.voltage(A0, 271.266, 1.3); // Voltage: input pin, calibration, phase_shift
  emon1.current(A4, 13.6);       // Current: input pin, calibration.

  getWeather();
  delay(5000);
  for (int i = 0; i < 3 ; i++) { //publish location hook 3 more times
    if (zone == 0) {
      getWeather();
      Serial.printf("Currently on getWeather #%i", i + 2);
      Serial.println();
      delay(7000);
    }
  }
  if (zone == 0) { //if still failed, try to use EEPROM and start timer to check every 30 minutes
    EEPROM.get(0, myLocation); //using yesterday's stuff should work ok
    if (myLocation.magic_number == 1) {
      // use the stored info if we have it
      Serial.printf("Using zone info in memory %i %u", myLocation.utcOffset, myLocation.magic_number);
      Serial.println();
      Time.zone(myLocation.utcOffset);
    } else {
      Serial.printf("Using hardcoded zone");
      Serial.println();
      Time.zone(-7);
    }

    //the stored info will be used first, if nothing is stored, then hardcoded info is used

  }
  getSunTimes();
  delay(5000);
  //go through the same procedure of publish/wait for the sun times
  for (int i = 0; i < 3 ; i++) { //publish sunTimes hook 3 more times
    if (!strcmp(sunrise.Hour, "04")) {
      getSunTimes();
      Serial.printf("Currently on getSunTimes #%i", i + 2);
      Serial.println();
      delay(7000);
    }
  }
  if (sunrise.magic_number == 0) { //if still failed, try to use EEPROM and start timer to check every 30 minutes
    EEPROM.get(50, sunrise); //using yesterday's stuff should work ok
    EEPROM.get(100, sunset);
    if (sunrise.magic_number == 1) { //we have stored info

      Serial.printf("Stored sunrise(%s:%s) and sunset(%s:%s) times found and loaded", sunrise.Hour, sunrise.Minute, sunset.Hour, sunset.Minute);
      Serial.println();
      //start function timer

    } else {
      strcpy(sunrise.Hour, "04");
      strcpy(sunset.Hour, "23");
      strcpy(sunrise.Minute, "00");
      strcpy(sunset.Minute, "00");
      Serial.printf("Memory not found - defaults loaded Sunset: %s:%s tonight, Sunrise %s:%s tomorrow.", sunset.Hour, sunset.Minute, sunrise.Hour, sunrise.Minute );
      Serial.println();
    }
  }


  if ((Time.hour() * 60 + Time.minute()) > (atoi(sunset.Hour) * 60 + atoi(sunset.Minute)) ) {
    // if after sundown, go back to sleep until dawn, sleep in seconds
    int sleeptime = 24 * 3600 - Time.hour() * 3600 - Time.minute() * 60 + atoi(sunrise.Hour) * 3600 + atoi(sunrise.Minute) * 60;
    Particle.publish("Sleep", "After sundown, sleeping for " + String::format("%u", sleeptime) + " seconds", 60, PRIVATE);
    Serial.printf("After sundown, going back to sleep for %i seconds", sleeptime);
    Serial.println();
    System.sleep(SLEEP_MODE_DEEP, sleeptime);
  }
  else if ( (atoi(sunrise.Hour) * 60 + atoi(sunrise.Minute)) - (Time.hour() * 60 + Time.minute()) > 10 ) {
    //or it's more than 10 minutes before sunup, why split h
    int sleeptime = atoi(sunrise.Hour) * 3600 + atoi(sunrise.Minute) * 60 - Time.hour() * 3600 - Time.minute() * 60;
    // Particle.publish("Sleep", "Before sunup, going back to sleep for " + String::format("%u", sleeptime) + " seconds", 60, PRIVATE);
    snprintf(pubString, sizeof(pubString), "Before sunup - sleeping for %u seconds - waking at %s:%s am ", sleeptime, sunrise.Hour, sunrise.Minute);
    //snprintf(pubString, sizeof(pubString), "{\"SearchTerm\":\"zipcode\",\"Data\":\"%s\",\"ID\":\"%s\",\"Code\":\"%s\"}",zip,appID,appCode);
    Particle.publish("Sleep", pubString, 60, PRIVATE);


    Serial.printf("Before sunup, going back to sleep for %i seconds", sleeptime);
    Serial.println();
    System.sleep(SLEEP_MODE_DEEP, sleeptime);
  }

  //so it's normal operation time, set the timer in ms to shut down at sundown
  thymer2.changePeriod(atoi(sunset.Hour) * 3600 * 1000 + atoi(sunset.Minute) * 60 * 1000 - Time.hour() * 3600 * 1000 - Time.minute() * 60 * 1000);
  Serial.printf("Run timer set for %i minutes", atoi(sunset.Hour) * 60 + atoi(sunset.Minute) - Time.hour() * 60 - Time.minute());
  Serial.println();
  //if we are starting after a power failure or flash, get the WattHr count
  EEPROM.get(150, WattHrCount);
  if (WattHrCount > 0) {
    Serial.printf("WattHrCount is %f watthrs", WattHrCount);
    Serial.println();
  }
  else {
    WattHrCount = 0;
    EEPROM.put(150, WattHrCount);
    Serial.printf("WattHrCount Set to zero");
    Serial.println();
  }
  thymer.start();
  thymer2.start();

}
void loop() {
  lastloop = millis();
  // Calculate all. No.of half wavelengths (crossings), time-out
  emon1.calcVI(20, 2000);
  RealPower       = emon1.realPower;        //extract Real Power into variable
  ApparentPower   = emon1.apparentPower;    //extract Apparent Power into variable
  PowerFactor     = emon1.powerFactor;      //extract Power Factor into Variable
  LineVoltage   = emon1.Vrms;             //extract Vrms into Variable
  LineCurrent   = emon1.Irms;
  // emon1.serialprint();           // Print out all variables (realpower, apparent power, Vrms, Irms, power factor)

  if (runflag) {

    PowerCount += RealPower;
    ApparentPowerCount += ApparentPower;
    loopcount++;
    printtick++;

    switch (Time.minute()) {
      case 10 : {
          if (!pub10) {
            WattHrCount += ((PowerCount / loopcount) / 6);
            EEPROM.put(150, WattHrCount);
            Serial.printf("Stored WattHrCount in memory: %f watthrs", WattHrCount);
            Serial.println();
            if (PowerCount / loopcount / 6 > 0) {
              pubLish();
            } pub10 = true;
            ApparentPowerCount = 0;
            PowerCount = 0;
            loopcount = 0;
          }
          pub0 = false;
          break;
        }
      case 20 : {
          if (!pub20) {
            WattHrCount += ((PowerCount / loopcount) / 6);
            EEPROM.put(150, WattHrCount);
            Serial.printf("Stored WattHrCount in memory: %f watthrs", WattHrCount);
            Serial.println();
            if (PowerCount / loopcount / 6 > 0) {
              pubLish();
            } pub20 = true;
            ApparentPowerCount = 0;
            PowerCount = 0;
            loopcount = 0;
          }
          pub10 = false;
          break;
        }
      case 30 : {
          if (!pub30) {
            WattHrCount += ((PowerCount / loopcount) / 6);
            EEPROM.put(150, WattHrCount);
            Serial.printf("Stored WattHrCount in memory: %f watthrs", WattHrCount);
            Serial.println();
            if (PowerCount / loopcount / 6 > 0) {
              pubLish();
            } pub30 = true;
            ApparentPowerCount = 0;
            PowerCount = 0;
            loopcount = 0;
          }
          pub20 = false;
          break;
        }
      case  40 : {
          if (!pub40) {
            WattHrCount += ((PowerCount / loopcount) / 6);
            EEPROM.put(150, WattHrCount);
            Serial.printf("Stored WattHrCount in memory: %f watthrs", WattHrCount);
            Serial.println();
            if (PowerCount / loopcount / 6 > 0) {
              pubLish();
            } pub40 = true;
            ApparentPowerCount = 0;
            PowerCount = 0;
            loopcount = 0;
          }
          pub30 = false;
          break;
        }
      case 50 : {
          if (!pub50) {
            WattHrCount += ((PowerCount / loopcount) / 6);
            EEPROM.put(150, WattHrCount);
            Serial.printf("Stored WattHrCount in memory: %f watthrs", WattHrCount);
            Serial.println();
            if (PowerCount / loopcount / 6 > 0) {
              pubLish();
            } pub50 = true;
            ApparentPowerCount = 0;
            PowerCount = 0;
            loopcount = 0;
          }
          pub40 = false;
          break;
        }
      case  0 : {
          if (!pub0) {
            WattHrCount += ((PowerCount / loopcount) / 6);
            EEPROM.put(150, WattHrCount);
            Serial.printf("Stored WattHrCount in memory: %f watthrs", WattHrCount);
            Serial.println();
            if (PowerCount / loopcount / 6 > 0) {
              pubLish();
            } pub0 = true;
            ApparentPowerCount = 0;
            PowerCount = 0;
            loopcount = 0;
          }
          pub50 = false;
          break;
        }

    }
  } else {
    WattHrCount = 0;  // 7hrs = 25200 seconds 9hrs = 32400
    EEPROM.put(150, WattHrCount); //clear the memory
    ApparentPowerCount = 0;
    PowerCount = 0;
    loopcount = 0;
    int sleeptime = 24 * 3600 - Time.hour() * 3600 - Time.minute() * 60 + atoi(sunrise.Hour) * 3600 + atoi(sunrise.Minute) * 60;
    // Particle.publish("Sleep", "Normal shutdown sleep for " + String::format("%u", sleeptime) + " seconds", 60, PRIVATE);
    snprintf(pubString, sizeof(pubString), "Normal shutdown sleep for %u seconds - waking at %s:%s am ", sleeptime, sunrise.Hour, sunrise.Minute);
    //snprintf(pubString, sizeof(pubString), "{\"SearchTerm\":\"zipcode\",\"Data\":\"%s\",\"ID\":\"%s\",\"Code\":\"%s\"}",zip,appID,appCode);
    Particle.publish("Sleep", pubString, 60, PRIVATE);
    Serial.printf("After sundown, normal sleep for %u seconds - waking at  %s:%s am", sleeptime, sunrise.Hour, sunrise.Minute);
    Serial.println();
    lcd.noBacklight();
    System.sleep(SLEEP_MODE_DEEP, sleeptime);
  }


  if (printtick >= 6) {
    printToLCD();
    printtick = 0;
  }

  looptime = millis() - lastloop;


}
int SetWattHrCount(String somenum) {
  WattHrCount = atof(somenum);
  return 1;
}
int Publish_Now(String command) {
  pubLish();
  return 1;
}

void printToLCD() {
  //make it fit a 20 char display
  char lcdStr1[21];
  char lcdStr2[11];
  snprintf(lcdStr1, sizeof(lcdStr1), "%.2fV   ", LineVoltage);
  snprintf(lcdStr2, sizeof(lcdStr2), " %.2fA", LineCurrent);
  
  lcd.setCursor(0, 0);
  lcd.print(lcdStr1);
  lcd.setCursor(20-strlen(lcdStr2), 0);
  lcd.print(lcdStr2);
  
  snprintf(lcdStr1, sizeof(lcdStr1), "%.1fW   ", PowerCount / loopcount);
  snprintf(lcdStr2, sizeof(lcdStr2), "  %.1fVA", ApparentPowerCount / loopcount);
  
  lcd.setCursor(0, 1);
  lcd.print(lcdStr1);
  lcd.setCursor(20-strlen(lcdStr2), 1);
  lcd.print(lcdStr2);

  snprintf(lcdStr1, sizeof(lcdStr1), "PF:%.2f   ", PowerFactor);
  snprintf(lcdStr2, sizeof(lcdStr2), " %.1fkWh", WattHrCount / 1000);
 lcd.setCursor(0, 2);
  lcd.print(lcdStr1);
  lcd.setCursor(20-strlen(lcdStr2), 2);
  lcd.print(lcdStr2);
  snprintf(lcdStr1, sizeof(lcdStr1), "Cycle Time: %ums  ", looptime);

  for (int i = 0; i < 20 - strlen(lcdStr1); i++) {
    strcat(lcdStr1, " "); //add some spaces to fill the buffer
  }
  lcd.setCursor(0, 3);
  lcd.print(lcdStr1);


}
void pubLish() {

  String strtime = Time.format(Time.now(), "%H:%M");
  char chartime[10];
  strtime.toCharArray(chartime, 10);
  snprintf(pubString, sizeof(pubString),
           "{\"Watts\":\"%.2f\", \"WattHrs\":\"%.2f\",\"Volts\":\"%.2f\", \"Amps\":\"%.2f\", \
\"Time\":\"%s\",\"Date\":\"%i%0.2i%0.2i\",\"PF\":\"%.2f\" ,\"Weather\":\"%s\",\"Temp\":\"%.1fF\",\"WeatherCalls\":\"%u\"}",
           PowerCount / loopcount, WattHrCount, LineVoltage, LineCurrent, chartime,
           Time.year(), Time.month(), Time.day(), PowerCount / ApparentPowerCount, condition, temp, calls);
  Serial.printf("Published: %s ", pubString);
  Serial.println();
  Particle.publish("QHour", pubString, 60, PRIVATE);
  //Particle.publish("Anus",pubString,60,PRIVATE);
}



void getWeather(void) {  //Handler for the timer, will be called automatically
  //https://weather.cit.api.here.com/weather/1.0/report.json?product=observation&name=Klamath%20Falls&app_id=DemoAppId01082013GAL&app_code=AJKnXv84fjrb0KIHawS0Tg
  //gotta send name, or zipcode, appid, appcode,
  if (useZip) { //let's get location info anyway, and handle EEPROM in the response
    snprintf(pubString, sizeof(pubString), "{\"SearchTerm\":\"zipcode\",\"Data\":\"%s\",\"ID\":\"%s\",\"Code\":\"%s\"}", zip, appID, appCode);

  } else {
    snprintf(pubString, sizeof(pubString), "{\"SearchTerm\":\"name\",\"Data\":\"%s\",\"ID\":\"%s\",\"Code\":\"%s\"}", city, appID, appCode);

  }

  Particle.publish("hereBweather", pubString, 60, PRIVATE);
  Serial.printf("Get Weather: %s", pubString);
  Serial.println();
  calls ++;
}
void getWeatherHandler(const char * event, const char * data) {
  //Sunny~70.48~2019-05-04T20:56:00.000-07:00~
  char weatherReturn[strlen(data + 1)];
  strcpy(weatherReturn, data);
  strcpy(condition, strtok(weatherReturn, "\"~"));
  temp = atof(strtok(NULL, "~"));
  char * useless = strtok(NULL, ".");

  useless = strtok(NULL, "-+");
  zone = 0 - atoi(strtok(NULL, ":"));
  //you're so negative in my hemisphere
  Time.zone(zone);
  //store that motherfucker
  myLocation = {zone, 1};
  EEPROM.put(0, myLocation);

  Serial.printf("Got weather: %s, and zone offset %i", condition, myLocation.utcOffset );
  Serial.printf(" Temp: %.1fF", temp);
  Serial.println();

  String timeNow = Time.format(Time.now(), "%H:%M");
  char time2[strlen(timeNow) + 1] = "";
  timeNow.toCharArray(time2, strlen(timeNow) + 1);
  Serial.printf("Time Now: %s", time2);
  Serial.println();


}
void getSunTimes(void) {  //ask cloud for sunrise and sunset times
  //https://weather.api.here.com/weather/1.0/report.json?product=forecast_astronomy&name=Klamath Falls&app_id="kf6DrfDq9RkOZ4WSeqzM"&app_code="Una3agbHljqiCMkZaERmgw"&metric=false
  if (useZip) { //let's get location info anyway, and handle EEPROM in the response
    snprintf(pubString, sizeof(pubString), "{\"SearchTerm\":\"zipcode\",\"Data\":\"%s\",\"ID\":\"%s\",\"Code\":\"%s\"}", zip, appID, appCode);

  } else {
    snprintf(pubString, sizeof(pubString), "{\"SearchTerm\":\"name\",\"Data\":\"%s\",\"ID\":\"%s\",\"Code\":\"%s\"}", city, appID, appCode);

  }

  Particle.publish("hereBsunTime", pubString, 60, PRIVATE);
  Serial.printf("Get sun times: %s", pubString);
  Serial.println();
  calls ++;
}
void sunTimesHandler(const char * event, const char * data) {
  char sunTimesBuffer[strlen(data + 1)] = "";
  strcpy(sunTimesBuffer, data);
  //Not the best way, but it will work
  //7:52PM~5:43AM~2019-05-04T20:56:00.000-07:00~
  char zeroplus[3] = "0";
  int MILTime = 12 + atoi(strtok(sunTimesBuffer, ":"));
  //add 12 hrs to their noob time.

  itoa(MILTime, sunset.Hour, 10);


  strcpy(sunset.Minute, strtok(NULL, "AP"));
  if (atoi(sunset.Minute) < 10) {
    strcat(zeroplus, sunset.Minute);
    strcpy(sunset.Minute, zeroplus);
    strcpy(zeroplus, "0");
  }

  char * useless = strtok(NULL, "~");
  strcpy(sunrise.Hour, strtok(NULL, ":"));
  if (atoi(sunrise.Hour) < 10) {
    strcat(zeroplus, sunrise.Hour);
    strcpy(sunrise.Hour, zeroplus);
    strcpy(zeroplus, "0");
  }
  strcpy(sunrise.Minute, strtok(NULL, "AP"));
  if (atoi(sunrise.Minute) < 10) {
    strcat(zeroplus, sunrise.Minute);
    strcpy(sunrise.Minute, zeroplus);

  }
  useless = strtok(NULL, ".");
  useless = strtok(NULL, "-+");
  zone = 0 - atoi(strtok(NULL, ":"));
  sunrise.magic_number = 1;
  sunset.magic_number = 1;
  Time.zone(zone);
  Serial.printf("Sunrise tomorrow: %s:%s Sunset tonight:%s:%s, Timezone: %i", sunrise.Hour, sunrise.Minute, sunset.Hour, sunset.Minute, zone);
  Serial.println();
  //debug sun times
  snprintf(pubString, sizeof(pubString), "SunriseHr: %s, SunriseMin: %s, SunsetHr: %s, SunsetMin: %s", sunrise.Hour, sunrise.Minute, sunset.Hour, sunset.Minute);
  Particle.publish("sunTimeDebug", pubString, 60, PRIVATE);

  String timeNow = Time.format(Time.now(), "%H:%M");
  char time2[strlen(timeNow) + 1] = "";
  timeNow.toCharArray(time2, strlen(timeNow) + 1);
  Serial.printf("Time Now: %s", time2);
  Serial.println();
  myLocation = {zone, 1};

  EEPROM.put(0, myLocation);
  EEPROM.put(50, sunrise);
  EEPROM.put(100, sunset);

}

int GetShutdown(String command) {  //

  return atoi(sunset.Hour) * 100 + atoi(sunset.Minute);
}
void onFlag(void) {  //Handler for the timer, will be called automatically

  runflag = false;
}
