// ongoing project, current progress

/*
 * This file is part of WiFi Smart Water Pump Controller S1 project.
 *
 * WiFi Smart Water Pump Controller S1 is free software and hardware: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 */

// Libraries used:
// WiFiManager by tzapu version: 2.0.16-rc.2 
// U8g2 by oliver version: 2.34.22 
// Board: ESP32-C3-MINI-1U-H4 
// Arduino Framework version: 2.3.2
// Arduino Board Module: ESP32C3 Dev Module
// Board Manager URL: https://arduino.esp8266.com/stable/package_esp8266com_index.json 

#include <Arduino.h>
#include <U8g2lib.h>

#include <TimeLib.h>


#include <WiFiManager.h>
#include <HTTPClient.h>
//#include "certs.h"

#include <nvs_flash.h>
#include <String.h>

#include <Update.h>
#include <math.h>


#define RELAY_PIN_1 0

// DEV MODE allow app update remote
const bool DEV_MODE = false; // used for ON/OFF live-updates and other other safety instructions for easy development
const bool LIVE_UPDATE = true; // used for ON/OFF live-updates and other other safety instructions for easy development

const char* firmwareUrl = "https://waterlevel.pro/static/fw";
int FIRMW_VER = 11;

// Pin del botón wifi-setup/reset
const int pinBoton = 1;

// Replace this with the URL you want to send the GET request to
const char* host_url = "https://api.waterlevel.pro/relay-update";
char* api_key = "PASTE Your Key Here"; // use "-" for updates // generate new key in developer zone at https://waterlevel.pro/settings

const char* WIFI_NAME = "AutoConnectSetup";
const char* WIFI_PASSW = "1122334455";

WiFiManager wm;
nvs_handle_t my_nvs_handle;

unsigned long tiempoArranque;

// relay status 0-off / 1-on
int RelayStatus = 0;

int ALGO = 0; // 0 none, 1 (llenar a 90% al ancanzar 30%)
int START_LEVEL = 0; // in %
int END_LEVEL = 0; // in %
int AUTO_OFF = 0;
int AUTO_ON = 0;
int ACTION = 0; //0 none, -1 off, 1 on
int IDLE_CONNECTION_SECS = 0; // calc. based on sensor pool-time
int MIN_FLOW_MM_X_MIN = 0;


int sens_percent = 0; // filled percent
int sens_event_time = 0;
int sens_current_time = 0;

int sensor_last_distance = 0;
int sensor_last_pool_time = 0;

unsigned long LAST_DATA_TIME = 0;
unsigned long IDLE_FLOW_EVENT_TIME = 0;
unsigned long IDLE_FLOW_EVENT_TIME_UNIX = 0;
int IDLE_FLOW_EVENT_LAST_DIST = 0;
bool IS_WAITING = false;

// FLOW IDDLE VARIABLES
unsigned long past_flow_time = 0;
int past_percent = 0;
const int IDLE_PERCENT = 2; // IDLE FRLOW CHECK REQUIRE MIN 2% variation
const int IDLE_RECOVERY_SEC = 60*60*3; // IDLE RECOVERY TIME before try again after low flow detected (3 hours)
int32_t rssi = 0;

int BLIND_DIST = 22;
String FLOW_EVENTS = "";

enum ConfigStatus {
  WIFI_SETUP,
  WIFI
};

ConfigStatus CurrentStatus = WIFI_SETUP;


U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(U8G2_R0, /*clock=*/9, /*data=*/8, U8X8_PIN_NONE);


void setup() {

  // Inicializamos los pines de control de los relés como salidas
  pinMode(RELAY_PIN_1, OUTPUT);
  // Desactivamos los relés al inicio
  digitalWrite(RELAY_PIN_1, LOW);


  tiempoArranque = millis();
  // Configuramos el pin del botón como entrada
  pinMode(pinBoton, INPUT_PULLUP);
  // Asociamos la función botonPresionado() al pin del botón
  attachInterrupt(pinBoton, botonPresionado, FALLING);
  

  // Inicializamos la comunicación serial para debug (opcional)
  Serial.begin(115200);

  u8g2.begin();

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_7x14B_tr);
  u8g2.drawButtonUTF8(64, 10, U8G2_BTN_HCENTER, 0,  2,  2, "Starting ..." );
  u8g2.sendBuffer();

  // Iniciar la memoria flash
  esp_err_t err = nvs_flash_init();

  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      // NVS partition was truncated and needs to be erased
      // Retry nvs_flash_init
      ESP_ERROR_CHECK(nvs_flash_erase());
      err = nvs_flash_init();
  }
  ESP_ERROR_CHECK( err );
  err = nvs_open("storage", NVS_READWRITE, &my_nvs_handle);
  if (err != ESP_OK) {
        Serial.println("Error opening nvs handler");
  } else {
      Serial.println("Done opening nvs handler");
  }

  load_private_key();

  RelayStatus = 0;
  ALGO = 0;
  START_LEVEL = 0;
  END_LEVEL = 0;
  AUTO_OFF = 0;
  AUTO_ON = 0;
  ACTION = 0;
  MIN_FLOW_MM_X_MIN = 0;
  IDLE_FLOW_EVENT_TIME = 0;
  IDLE_FLOW_EVENT_LAST_DIST = 0;

  past_flow_time = millis();
  past_percent = 0;
  IS_WAITING = false;

  LAST_DATA_TIME = millis();
  load_local_settings();
  IDLE_CONNECTION_SECS = 0;

  if(CurrentStatus == WIFI_SETUP){
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_7x14B_tr);
    u8g2.drawButtonUTF8(64, 10, U8G2_BTN_HCENTER, 0,  2,  2, "WiFi Reset/Setup" );
    u8g2.sendBuffer();
    SetupResetWifi();
  }

  ConnectWifi();

}


int myStrlen(const char* str) {
  int len = 0;
  while (*str != '\0') {
    len++;
    str++;
  }
  return len;
}


void load_private_key(){

  size_t required_size;
  nvs_get_str(my_nvs_handle, "PrivateKey", NULL, &required_size);
  if (required_size != 0) {
    char* tmp_key = (char*)malloc(required_size);
    nvs_get_str(my_nvs_handle, "PrivateKey", tmp_key, &required_size);
    api_key = tmp_key;
    
    Serial.print("Private Key loaded from memory: ");
    Serial.println(api_key);

  }else if(api_key != "-"){
    nvs_set_str(my_nvs_handle, "PrivateKey", api_key);
    nvs_commit(my_nvs_handle);

    Serial.print("Private Key loaded from code: ");
    Serial.println(api_key);
  }

  if(myStrlen(api_key) < 10){
    Serial.print("ERROR: Short API KEY: ");
    Serial.println(api_key);
    esp_system_abort("restart_after_wakeup");
  }

  
}


void loop() {

  //restart each 24 hours, when relay is OFF
  if(millis() > 86400000 && RelayStatus == 0){
    delay(500);
    esp_system_abort("restart_after_wakeup");
  }

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_7x14B_tr);

  if(!DEV_MODE && millis() <= 120000){
    Serial.println("Waiting 2 min. after start 4 safetry");

    u8g2.setFont(u8g2_font_helvR08_tr);
    u8g2.drawButtonUTF8(62, 30, U8G2_BTN_HCENTER|U8G2_BTN_BW2, 34,  2,  2, "Waiting 2 min." );

    u8g2.sendBuffer();

    delay(10000); // Esperamos 10 segundos
    return;
  }

  GetRemoteSettings();

  char textWithInt2[100]; 
  sprintf(textWithInt2, "Level: %d %%", sens_percent);
  u8g2.drawButtonUTF8(64, 10, U8G2_BTN_HCENTER, 0,  2,  2, textWithInt2 );

  drawLevel(sens_percent);
  
  if(IDLE_CONNECTION_SECS != 0){
    if(sens_current_time - sens_event_time > IDLE_CONNECTION_SECS){
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_7x14B_tr);
        u8g2.drawButtonUTF8(64, 10, U8G2_BTN_HCENTER|U8G2_BTN_BW2, 34,  2,  2, "WARNING!" );
        u8g2.drawButtonUTF8(64, 35, U8G2_BTN_HCENTER|U8G2_BTN_BW2, 34,  2,  2, "IDLE SENSOR!" );
    }
  }

  if(ACTION == 1){
    ACTION = 0;
    past_flow_time = millis();
    RelayOn();

    IS_WAITING = false;
    IDLE_FLOW_EVENT_TIME = 0;
    IDLE_FLOW_EVENT_LAST_DIST = 0; 
  }

  if(ACTION == -1){
    ACTION = 0;
    past_flow_time = millis();
    RelayOff();
  }

  if(AUTO_OFF > 0 && sensor_last_distance > 0 && sensor_last_distance <= BLIND_DIST){
    Serial.println("Top blind distance detected");
    RelayOff();

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_7x14B_tr);
    u8g2.drawButtonUTF8(64, 10, U8G2_BTN_HCENTER|U8G2_BTN_BW2, 34,  2,  2, "AUTO OFF!" );
    u8g2.drawButtonUTF8(64, 35, U8G2_BTN_HCENTER|U8G2_BTN_BW2, 34,  2,  2, "FULL" );

    delay(20000); // Esperamos 20 segundos
  }

  if(AUTO_OFF > 0 && IDLE_CONNECTION_SECS != 0 &&  sens_current_time > 0 && sens_event_time > 0 && sens_current_time - sens_event_time > IDLE_CONNECTION_SECS){
    Serial.println("Sensor offline AUTO_OFF");
    RelayOff();

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_7x14B_tr);
    u8g2.drawButtonUTF8(64, 10, U8G2_BTN_HCENTER|U8G2_BTN_BW2, 34,  2,  2, "AUTO OFF!" );
    u8g2.drawButtonUTF8(64, 35, U8G2_BTN_HCENTER|U8G2_BTN_BW2, 34,  2,  2, "SENSOR OFFLINE" );

    delay(20000); // Esperamos 20 segundos
  }

  if(ALGO == 1){
    DoAlgo1();
  }

  if(ALGO == 2){
    DoAlgo2();
  }
  u8g2.sendBuffer();
  delay(10000); // Esperamos 10 segundos
}


void drawLevel(int percentage) {
  int barWidth = map(percentage, 0, 100, 0, u8g2.getWidth() - 11);  // Restar 20 para dejar espacio alrededor
  int barHeight = u8g2.getHeight();  // 1/4 de la altura de la pantalla
  int frameThickness = 1;  // Grosor del marco

  // Dibujar el contorno del indicador de batería
  u8g2.drawFrame(5, 18, u8g2.getWidth() - 10, u8g2.getHeight() - 35);

  // Dibujar el área de nivel de batería
  u8g2.drawBox(7, 20, barWidth, u8g2.getHeight() - 39);

}


int GetHour(int utcTimestamp, int gmt_number){
  // Apply the GMT-4 offset (4 hours behind UTC)
  time_t gmtMinus4Timestamp = (time_t)utcTimestamp - (gmt_number * 3600); // Subtract 4

  // Convert GMT-4 timestamp to a tm struct
  tmElements_t tm;
  breakTime(gmtMinus4Timestamp, tm);

  Serial.print("Hour: ");
  Serial.println(tm.Hour);

  return tm.Hour;
}

// ALGO #1 - try auto-start if capacity is under 30%, stop at 90%
// use complex iddle flow detection based in speed [not working]
void DoAlgo1(){

  /*
  RelayStatus = 0;
  ALGO = 0;
  START_LEVEL = 0;
  END_LEVEL = 0;
  AUTO_OFF = 0;
  AUTO_ON = 0;
  ACTION = 0;

  MIN_FLOW_MM_X_MIN
  sensor_last_distance
  sensor_last_pool_time 
  */
 
  if(RelayStatus == 1 && IDLE_CONNECTION_SECS > 0 && (millis() - LAST_DATA_TIME >= IDLE_CONNECTION_SECS*1000)){
    Serial.println("Long iddle connection time");
    RelayStatus = 0;
    sens_current_time = 0;
    sens_event_time = 0;
    RelayOff();
    return;
  }

  // Stop ALGO after 10 PM and before 9 AM
  if(sens_current_time > 0){
      int gtm_4_hour = GetHour(sens_current_time, 4);
      if(gtm_4_hour >= 22 || gtm_4_hour <= 8){
        Serial.println("Late night detected");
        RelayOff();
        return;
      }
  }
  
  if(sens_current_time == 0 || sens_event_time == 0 ){
      Serial.println("Not sensor data detected!");
      sens_current_time = 0;
      sens_event_time = 0;
      return;
  }

  if(IDLE_CONNECTION_SECS != 0){
    if(sens_current_time - sens_event_time > IDLE_CONNECTION_SECS){
      Serial.println("Idle sensor detected!");
      sens_current_time = 0;
      sens_event_time = 0;
      RelayOff();
      return;
    }
  }

  /*  
    MIN_FLOW_MM_X_MIN
    sensor_last_distance
    sensor_last_pool_time 
  */

  // case: waiting after IDDLE FLOW
  if(IS_WAITING && (IDLE_FLOW_EVENT_TIME + IDLE_RECOVERY_SEC*1000) >= millis()){
    Serial.println("Waiting 3 hours for better flow");
    return;
  }else if(IS_WAITING){
    IS_WAITING = false;
    IDLE_FLOW_EVENT_TIME = 0;
    IDLE_FLOW_EVENT_LAST_DIST = 0; 
  }

  // case: IDDLE FLOW 
  if(RelayStatus == 1 && MIN_FLOW_MM_X_MIN > 0 && sensor_last_distance > 0 && sensor_last_pool_time > 0){
    Serial.print("IDDLE FLOW CHECK, IDLE_FLOW_EVENT_TIME: ");
    Serial.println(IDLE_FLOW_EVENT_TIME);

    Serial.print("IDDLE FLOW CHECK, sensor_last_pool_time: ");
    Serial.println(sensor_last_pool_time);

    Serial.print("IDDLE FLOW CHECK, millis: ");
    Serial.println(millis());

    if(IDLE_FLOW_EVENT_TIME == 0 && IDLE_FLOW_EVENT_LAST_DIST ==0 && sensor_last_distance > 0){
      Serial.print("IDDLE FLOW REBOOT DETECTED");
      // update event data
      IDLE_FLOW_EVENT_TIME = millis();
      IDLE_FLOW_EVENT_TIME_UNIX = sens_event_time;
      IDLE_FLOW_EVENT_LAST_DIST = sensor_last_distance;

    }

    // collect dist-sample based on sensor pool time
    if((IDLE_FLOW_EVENT_TIME + sensor_last_pool_time*2000 + 30000) < millis()){
      Serial.println("IDDLE FLOW CHECK LV2");

      //check is not first time
      if(IDLE_FLOW_EVENT_TIME != 0 && IDLE_FLOW_EVENT_LAST_DIST !=0 && sensor_last_distance > 0){
        int diff_dist = (sensor_last_distance - IDLE_FLOW_EVENT_LAST_DIST)*10;
        int diff_time = IDLE_FLOW_EVENT_TIME_UNIX - sens_event_time; // unix sec. diff.
        float speed = (float)diff_dist / ((float)diff_time/60.0);
        int int_speed = round(speed);

        Serial.println("*************************************");
        Serial.print("int_speed: ");
        Serial.println(int_speed);

        if(int_speed <= MIN_FLOW_MM_X_MIN){
          Serial.println("slow flow detected");
          RelayOff();
          IS_WAITING = true;
          return;
        }
      }

      // update event data
      IDLE_FLOW_EVENT_TIME = millis();
      IDLE_FLOW_EVENT_TIME_UNIX = sens_event_time;
      IDLE_FLOW_EVENT_LAST_DIST = sensor_last_distance;
    }

  }

  sens_current_time = 0;
  sens_event_time = 0;

  Serial.print("sens_percent: ");
  Serial.println(sens_percent);

  if(sens_percent <= START_LEVEL && RelayStatus == 0 && AUTO_ON == 1){
    Serial.println("AUTO_ON ON");
    RelayOn();
  }

  if(sens_percent >= END_LEVEL && RelayStatus == 1 && AUTO_OFF == 1){
    Serial.println("AUTO_OFF OFF");
    RelayOff();
  }
}

// ALGO #2 - try auto-start if capacity is under 30%, stop at 90%
// use simple iddle flow detection based in basic check
void DoAlgo2(){

  /*
  RelayStatus = 0;
  ALGO = 0;
  START_LEVEL = 0;
  END_LEVEL = 0;
  AUTO_OFF = 0;
  AUTO_ON = 0;
  ACTION = 0;

  MIN_FLOW_MM_X_MIN
  sensor_last_distance
  sensor_last_pool_time 
  */
 
  if(RelayStatus == 1 && IDLE_CONNECTION_SECS > 0 && (millis() - LAST_DATA_TIME >= IDLE_CONNECTION_SECS*1000)){
    Serial.println("Long iddle connection time");
    RelayStatus = 0;
    sens_current_time = 0;
    sens_event_time = 0;
    RelayOff();
    return;
  }

  // Stop ALGO after 10 PM and before 9 AM
  if(sens_current_time > 0){
      int gtm_4_hour = GetHour(sens_current_time, 4);
      if(gtm_4_hour >= 22 || gtm_4_hour <= 8){
        Serial.println("Late night detected");
        RelayOff();
        return;
      }
  }
  
  if(sens_current_time == 0 || sens_event_time == 0 ){
      Serial.println("Not sensor data detected!");
      sens_current_time = 0;
      sens_event_time = 0;
      return;
  }

  if(IDLE_CONNECTION_SECS != 0){
    if(sens_current_time - sens_event_time > IDLE_CONNECTION_SECS){
      Serial.println("Idle sensor detected!");
      sens_current_time = 0;
      sens_event_time = 0;
      RelayOff();
      return;
    }
  }

  /*  
    MIN_FLOW_MM_X_MIN
    sensor_last_distance
    sensor_last_pool_time 
  */

  // case: waiting after IDDLE FLOW
  if(IS_WAITING && (IDLE_FLOW_EVENT_TIME + IDLE_RECOVERY_SEC*1000) >= millis()){
    Serial.println("Waiting 3 hours for better flow");
    return;
  }else if(IS_WAITING){
    IS_WAITING = false;
    IDLE_FLOW_EVENT_TIME = 0;
    IDLE_FLOW_EVENT_LAST_DIST = 0; 
  }

  // case: IDDLE FLOW 
  if(RelayStatus == 1 && MIN_FLOW_MM_X_MIN > 0 && sensor_last_distance > 0 && sensor_last_pool_time > 0){
    Serial.print("IDDLE FLOW CHECK, IDLE_FLOW_EVENT_TIME: ");
    Serial.println(IDLE_FLOW_EVENT_TIME);

    Serial.print("IDDLE FLOW CHECK, sensor_last_pool_time: ");
    Serial.println(sensor_last_pool_time);

    Serial.print("IDDLE FLOW CHECK, millis: ");
    Serial.println(millis());

    if(IDLE_FLOW_EVENT_TIME == 0 && IDLE_FLOW_EVENT_LAST_DIST == 0 && sensor_last_distance > 0){
      Serial.print("IDDLE FLOW REBOOT DETECTED");
      // update event data
      IDLE_FLOW_EVENT_TIME = millis();
      IDLE_FLOW_EVENT_TIME_UNIX = sens_event_time;
      IDLE_FLOW_EVENT_LAST_DIST = sensor_last_distance;

    }

    // collect dist-sample based on sensor pool time
    if((IDLE_FLOW_EVENT_TIME + sensor_last_pool_time*2000 + 30000) < millis()){
      Serial.println("IDDLE FLOW CHECK LV2");

      //check is not first time
      if(IDLE_FLOW_EVENT_TIME != 0 && IDLE_FLOW_EVENT_LAST_DIST !=0 && sensor_last_distance > 0){
        int diff_dist = (sensor_last_distance - IDLE_FLOW_EVENT_LAST_DIST)*10;
        
        if(diff_dist <= 0){
          Serial.println("slow flow detected");
          RelayOff();
          IS_WAITING = true;
          return;
        }
      }

      // update event data
      IDLE_FLOW_EVENT_TIME = millis();
      IDLE_FLOW_EVENT_TIME_UNIX = sens_event_time;
      IDLE_FLOW_EVENT_LAST_DIST = sensor_last_distance;
    }

  }

  sens_current_time = 0;
  sens_event_time = 0;

  Serial.print("sens_percent: ");
  Serial.println(sens_percent);

  if(sens_percent <= START_LEVEL && RelayStatus == 0 && AUTO_ON == 1){
    Serial.println("AUTO_ON ON");
    RelayOn();
  }

  if(sens_percent >= END_LEVEL && RelayStatus == 1 && AUTO_OFF == 1){
    Serial.println("AUTO_OFF OFF");
    RelayOff();
  }
}


void RelayOn(){
  digitalWrite(RELAY_PIN_1, HIGH); // Activamos el primer relé
  Serial.println("Relay ON");
  RelayStatus = 1;

  IS_WAITING = false;
  IDLE_FLOW_EVENT_TIME = 0;
  IDLE_FLOW_EVENT_LAST_DIST = 0;
}

void RelayOff(){
  digitalWrite(RELAY_PIN_1, LOW); // Activamos el primer relé
  Serial.println("Relay OFF");
  RelayStatus = 0;
  ACTION = 0;
}

void load_local_settings(){

  int32_t status;
  nvs_get_i32(my_nvs_handle, "0-status", &status);

  Serial.print("Loaded Status: ");
  Serial.println(status);

  if(status >= 0 && status <= 1){
    CurrentStatus = (ConfigStatus)status;
  }else{
    Serial.println("EEPROM DEF!");
    nvs_set_i32(my_nvs_handle, "0-status", (int32_t)WIFI_SETUP);
    nvs_commit(my_nvs_handle);
    CurrentStatus = WIFI_SETUP;
  }

  Serial.print("Status Loaded = ");
  Serial.println(CurrentStatus);

}



bool ConnectWifi(){

    //WiFi.setTxPower(WIFI_POWER_8_5dBm);
    WiFi.mode(WIFI_STA); // explicitly set mode
    //WiFi.setTxPower(WIFI_POWER_8_5dBm);
    //wifi_station_connect();
    delay(250);

    wm.setConnectRetries(3);
    wm.setConnectTimeout(15);
    wm.setTimeout(15);

    std::vector<const char *> menu = {"wifi","restart","exit"};
    wm.setMenu(menu);
  
    // set configportal timeout 10 sec
    wm.setConfigPortalTimeout(15);

    if (!wm.autoConnect(WIFI_NAME, WIFI_PASSW)) {
      Serial.println("failed to connect and hit timeout");
      return false;
    }

    Serial.println("WiFi Connected!");

    rssi = WiFi.RSSI();
    Serial.print("Signal strength (RSSI): ");
    Serial.print(rssi);
    Serial.println(" dBm");


    return true;
}


void SetLocalConf(int _ALGO, int _START_LEVEL, int _END_LEVEL, int _AUTO_OFF, int _AUTO_ON, int _MIN_FLOW_MM_X_MIN, int _ACTION){

  Serial.print("SetLocalConf: _MIN_FLOW_MM_X_MIN: ");
  Serial.println(_MIN_FLOW_MM_X_MIN);

  if(_ACTION == 1 || _ACTION == -1){
    ACTION = _ACTION;
    return;
  }
  
  if(_ALGO >=0 && _ALGO <= 100){
    ALGO = _ALGO;
  }

  if(_START_LEVEL >=0 && _START_LEVEL <= 800){
    START_LEVEL = _START_LEVEL;
  }

  if(_END_LEVEL >=0 && _END_LEVEL <= 800){
    END_LEVEL = _END_LEVEL;
  }

  if(_AUTO_OFF >=0 && _AUTO_OFF <= 1){
    AUTO_OFF = _AUTO_OFF;
  }

  if(_AUTO_ON >=0 && _AUTO_ON <= 1){
    AUTO_ON = _AUTO_ON;
  }

  if(_MIN_FLOW_MM_X_MIN >=0){
    MIN_FLOW_MM_X_MIN = _MIN_FLOW_MM_X_MIN;
  }

}


String splitString(const String &dataString, int *resultArray, int arraySize) {
  // Utilizar strtok para dividir la cadena en tokens basados en el delimitador '|'
  char *token = strtok(const_cast<char*>(dataString.c_str()), "|");

  // Iterar a través de los tokens y convertirlos en enteros
  int i = 0;
  String sensor_key;
  while (token != NULL && i < arraySize) {
    // Utilizar atoi para convertir el token en un entero
    if(i == 6){
        sensor_key = String(token);
    }else{
      resultArray[i] = atoi(token);
    }
    

    // Obtener el siguiente token
    token = strtok(NULL, "|");

    // Incrementar el índice
    i++;
  }

  return sensor_key;
}


bool GetRemoteSettings(){

  // Check for successful connection
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Connected to WiFi");

    rssi = WiFi.RSSI();
    Serial.print("Signal strength (RSSI): ");
    Serial.print(rssi);
    Serial.println(" dBm");
    
    long timeout = 10000;

    HTTPClient https;
    https.setTimeout(timeout);
    https.setConnectTimeout(timeout);
    
    Serial.print("[HTTPS] begin...\n");

    char relay_str[2];
    itoa(RelayStatus, relay_str, 2);


    char urlout[255];
    // we are going to start here
    strcpy(urlout, host_url);
    strcat(urlout, "?key=");
    strcat(urlout, api_key);
    strcat(urlout, "&status=");
    strcat(urlout, relay_str);


    if (https.begin(urlout)) {  // HTTPS

      //https.setVerify .setVerify(true);
      https.setReuse(false);

      https.addHeader("FW-Version", (String)FIRMW_VER);
      https.addHeader("RSSI", (String)rssi);
      
      https.setTimeout(timeout);


      // prepare to read response headers

      // Agrega todas las claves de los encabezados que deseas recopilar
      const char *headerKeys[] = {"percent", "event-time", "current-time", "fw-version", "distance", "pool-time",
        "ALGO", "START_LEVEL", "END_LEVEL", "AUTO_OFF", "AUTO_ON", "MIN_FLOW_MM_X_MIN", "ACTION",
      }; 

      const size_t headerKeysCount = sizeof(headerKeys) / sizeof(headerKeys[0]);
      // Recopila las cabeceras HTTP especificadas
      https.collectHeaders(headerKeys, headerKeysCount);


      Serial.print("[HTTPS] GET...\n");
      // start connection and send HTTP header
      int httpCode = https.GET();

      // httpCode will be negative on error
      if (httpCode > 0) {
        // HTTP header has been send and Server response header has been handled
        Serial.printf("[HTTPS] GET... code: %d\n", httpCode);

        // file found at server
        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {

          String payload = https.getString(); 
          Serial.print("response payload: ");
          Serial.println(payload);

          if(payload == "OK" ){
            Serial.println("Remote setting OK");

            String sensor_percent = https.header("percent");
            String sensor_event_time = https.header("event-time");
            String sensor_current_time = https.header("current-time");
            String latest_relay_fw = https.header("fw-version");
            String sensor_distance = https.header("distance");
            String sensor_pool_time = https.header("pool-time");
            
            /*
            Serial.println("Response headers:");
            Serial.print("Percent: ");
            Serial.println(sensor_percent);
            Serial.print("sensor_event_time: ");
            Serial.println(sensor_event_time);
            Serial.print("sensor_current_time: ");
            Serial.println(sensor_current_time);
            Serial.print("latest_relay_fw: ");
            Serial.println(latest_relay_fw);
            */
            
            sens_percent = sensor_percent.toInt();
            sens_event_time = sensor_event_time.toInt();
            sens_current_time = sensor_current_time.toInt();

            sensor_last_distance = sensor_distance.toInt();
            sensor_last_pool_time = sensor_pool_time.toInt();
            IDLE_CONNECTION_SECS = sensor_last_pool_time*2 + 30; // sensor is iddle if past x2 times sensor pool-time + 30 sec

            int fw_ver = latest_relay_fw.toInt();
            if(LIVE_UPDATE && FIRMW_VER < fw_ver){
                Serial.println("new FW release");
                updateFirmware(fw_ver);
            }

            if(sens_event_time > 0 && sens_current_time > 0){
              LAST_DATA_TIME = millis();
            }

            SetLocalConf(https.header("ALGO").toInt(), https.header("START_LEVEL").toInt(), https.header("END_LEVEL").toInt(), 
              https.header("AUTO_OFF").toInt(), https.header("AUTO_ON").toInt(), https.header("MIN_FLOW_MM_X_MIN").toInt(), https.header("ACTION").toInt());

          }else{
            Serial.println("Response error!");
          }

          https.end();
          return true;

        }
      } else {
        Serial.printf("[HTTPS] GET... failed, error: %s\n", https.errorToString(httpCode).c_str());
        https.end();
        return false;
      }

    } else {
      Serial.printf("[HTTPS] Unable to connect\n");
      return false;
    }

  } else {
    Serial.println("Failed to connect to WiFi");
    ConnectWifi();
  }
  return false;
}



bool SetupResetWifi(){

  //WiFi.setTxPower(WIFI_POWER_8_5dBm);
  WiFi.mode(WIFI_AP); // explicitly set mode
  //WiFi.setTxPower(WIFI_POWER_8_5dBm);
  delay(250);
  
  wm.setCaptivePortalEnable(true);

  std::vector<const char *> menu = {"wifi","restart","exit"};
  wm.setMenu(menu);

  // set configportal timeout 5 min
  wm.setConfigPortalTimeout(300);

  if (!wm.startConfigPortal(WIFI_NAME, WIFI_PASSW)) {
    Serial.println("failed to connect and hit timeout");

    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    esp_system_abort("restart_after_wakeup");
    delay(5000);
  }

  nvs_set_i32(my_nvs_handle, "0-status", (int32_t)WIFI);
  nvs_commit(my_nvs_handle);
  CurrentStatus = WIFI;

  Serial.println("Setup Wifi Success");
  delay(3000);
  esp_system_abort("restart_after_wakeup");

}


// Función que se ejecutará cuando el botón se presione
ICACHE_RAM_ATTR void botonPresionado() {

  if(millis() < 1000) return;

  Serial.println("El botón está presionado");

  nvs_set_i32(my_nvs_handle, "0-status", (int32_t)WIFI_SETUP);
  nvs_commit(my_nvs_handle);

  CurrentStatus = WIFI_SETUP;

  //reset settings
  wm.resetSettings();

  delay(500);
  esp_system_abort("restart_after_wakeup");

}


void updateFirmware(int new_fw_vers) {

  if(millis() <= 60000){
    Serial.println("Waiting 60 sec. before updateFirmware");
    return;
  }

  HTTPClient http;
  Serial.print("Conectando al servidor para descargar el firmware...");

  char urlout[255];
  sprintf(urlout, "%s/relay%d.bin", firmwareUrl, new_fw_vers);
  
  if (http.begin(urlout)) {
    int httpResponseCode = http.GET();

    if (httpResponseCode == HTTP_CODE_OK) {
      Serial.println("Firmware encontrado. Iniciando actualización...");

      size_t updateSize = http.getSize();

      if (Update.begin(updateSize)) {
        WiFiClient& stream = http.getStream();

        if (Update.writeStream(stream)) {
          if (Update.end()) {
            Serial.println("Actualización exitosa. Reiniciando...");
            delay(500);
            esp_system_abort("restart_after_wakeup");
          } else {
            Serial.println("Error al finalizar la actualización.");
          }
        } else {
          Serial.println("Error al escribir en el firmware.");
        }
      } else {
        Serial.println("Error al iniciar la actualización.");
      }
    } else {
      Serial.println("Firmware no encontrado. Código de error HTTP: " + String(httpResponseCode));
    }

    http.end();
  } else {
    Serial.println("Error al conectarse al servidor.");
  }
}
