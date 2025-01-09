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

// IDE and Board
// Board: ESP32-C3-MINI-1U-H4 
// Arduino Framework version: 2.3.4
// Arduino Board Module: ESP32C3 Dev Module
// Board Manager URL: https://arduino.esp8266.com/stable/package_esp8266com_index.json 

// Libraries used:
// WiFiManager by tzapu version: 2.0.16-rc.2 
// U8g2 by oliver version: 2.35.30 
// Time by Michael Margolis version: 1.6.1


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
#define EMAIL_MAX_LENGTH 128
#define DEBUG  // Comment this line to disable serial prints
#define BLIND_MARGIN_EXTRA 2 // extra margin added

// DEV MODE allow app update remote
const bool DEV_MODE = false; // used for ON/OFF live-updates and other other safety instructions for easy development
const bool LIVE_UPDATE = true; // used for ON/OFF live-updates and other other safety instructions for easy development

const char* firmwareUrl = "https://waterlevel.pro/static/fw";
int FIRMW_VER = 18;

// Pin del botón wifi-setup/reset
const int pinBoton = 1;

// Replace this with the URL you want to send the GET request to
const char* host_url = "https://api.waterlevel.pro/relay-update";

const char* host_link_url = "https://api.waterlevel.pro/link";

char api_key[128] = "-";  //  PRIVATE KEY HERE // "-" none-null-api-key
// use value "-" for automatic api key setup
// or manually generate and use new key in developer zone at https://waterlevel.pro/settings   https://waterlevel.pro/add_relay


const char* WIFI_NAME = "WaterLevelProSetup";
const char* WIFI_PASSW = "1122334455";
char email[EMAIL_MAX_LENGTH] = "-";  // Variable to store email

WiFiManager wm;
// Set custom email parameter
WiFiManagerParameter custom_email("email", "Enter Email", email, EMAIL_MAX_LENGTH, "type='email' required placeholder='example@example.com'");


nvs_handle_t my_nvs_handle;
unsigned long tiempoArranque;

// relay status 0-off / 1-on
int RelayStatus = 0;

int ALGO = 0; // 0 none, 1 (llenar a 90% al ancanzar 30%)
int SAFE_MODE = 1;
int START_LEVEL = 0; // in %
int END_LEVEL = 0; // in %
int AUTO_OFF = 0;
int AUTO_ON = 0;
int ACTION = 0; //0 none, -1 off, 1 on
int IDLE_CONNECTION_SECS = 0; // calc. based on sensor pool-time
int MIN_FLOW_MM_X_MIN = 0;
int BLIND_DISTANCE = 22;
String HOURS_OFF = "-";
int WIFI_POOL_TIME = 10000; // wifi usage pool time in mili-seconds

int Sensor5History[5] = {-1, -1, -1, -1, -1};


int sens_percent = 0; // filled percent
int sens_event_time = 0;
int sens_current_time = 0;

int sensor_last_distance = 0;
int sensor_last_pool_time = 0;
int sens_event_last_time = 0;

unsigned long LAST_DATA_TIME = 0;
unsigned long IDLE_FLOW_EVENT_TIME = 0;
unsigned long relay_on_start_time = 0;
bool IS_WAITING = false;

// FLOW IDDLE VARIABLES
const int IDLE_RECOVERY_SEC = 60*60*3; // IDLE RECOVERY TIME before try again after low flow detected (1 hours)
int32_t rssi = 0;

String FLOW_EVENTS = "";

int Reset_BTN_Count = 0;
enum ConfigStatus {
  WIFI_SETUP,
  WIFI
};

ConfigStatus CurrentStatus = WIFI_SETUP;

enum EventLogs {
  NO_EVENT,
  BLIND_AREA,
  BLIND_AREA_DANGER,
  NOT_FLOW,
  OFFLINE,
  IDDLE_SENSOR,
  END_LEVEL_EVENT,
  START_LEVEL_EVENT,
  SETUP_WIFI,
  BOOT,
  PUMP_ON,
  PUMP_OFF,
  DATA_POST_FAIL,
  BTN_PRESS,
  SENSOR_FAULT
};

EventLogs EventsArray[5] = {NO_EVENT, NO_EVENT, NO_EVENT, NO_EVENT, NO_EVENT};


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
  AddEvent(BOOT);

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

  // Add parameter to WiFiManager
  wm.addParameter(&custom_email);
  // Set config save callback
  wm.setSaveConfigCallback(saveConfigCallback);

  RelayStatus = 0;
  ALGO = 0;
  SAFE_MODE = 0;
  START_LEVEL = 0;
  END_LEVEL = 0;
  AUTO_OFF = 0;
  AUTO_ON = 0;
  ACTION = 0;
  MIN_FLOW_MM_X_MIN = 0;
  IDLE_FLOW_EVENT_TIME = 0;
  Reset_BTN_Count = 0;
  WIFI_POOL_TIME = 10000;

  IS_WAITING = false;

  LAST_DATA_TIME = millis();
  IDLE_CONNECTION_SECS = 0;

  load_local_settings();
  load_private_key();

  if(CurrentStatus == WIFI_SETUP){
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_7x14B_tr);
    u8g2.drawButtonUTF8(64, 10, U8G2_BTN_HCENTER, 0,  2,  2, "WiFi Reset/Setup" );
    u8g2.sendBuffer();

    AddEvent(SETUP_WIFI);
    SetupResetWifi();
    
  }else{
    ConnectWifi();
  }
  
}


void saveConfigCallback() {
  Serial.println("Enter saveConfigCallback");
  // Placeholder for additional code to handle saving to persistent storage, if needed

  // Guardar el email en EEPROM
  const char *newEmail = custom_email.getValue();
  strncpy(email, newEmail, sizeof(email) - 1); // Copiar el nuevo valor
  email[sizeof(email) - 1] = '\0'; // Asegurar que termina con '\0'


  if(myStrlen(email) >= 7 ){

    #ifdef DEBUG
      Serial.print("Email to linked: ");
      Serial.println(email);
    #else
       delay(5);
    #endif
  }else{
    Serial.print("Invalid email: ");
    Serial.println(email);
    strcpy(email, "-");
  }

}
  

int myStrlen(const char* str) {
  int len = 0;
  while (*str != '\0') {
    len++;
    str++;
  }
  return len;
}

void non_lock_delay(unsigned long mili_seconds) {
  static unsigned long previousMillis = 0;
  static bool isWaiting = false;

  unsigned long currentMillis = millis();

  // Inicio de espera
  if (!isWaiting) {
    previousMillis = currentMillis;
    isWaiting = true;
  }

  while (isWaiting) {
    currentMillis = millis();
    
    // Verificar si el tiempo ha pasado
    if (currentMillis - previousMillis >= mili_seconds) {
      isWaiting = false; // Reiniciar la espera

    }

    // Permitir que el sistema operativo maneje tareas críticas
    delay(1); // Mantener el sistema operativo funcionando mientras se espera
  }
}


void load_private_key(){

  size_t required_size = 0;
  nvs_get_str(my_nvs_handle, "PrivateKey", NULL, &required_size);
  if (required_size != 0 and required_size > 10) {
    char* tmp_key = (char*)malloc(required_size);
    nvs_get_str(my_nvs_handle, "PrivateKey", tmp_key, &required_size);
    strcpy(api_key, tmp_key);  
    free(tmp_key);
    
    Serial.print("Private Key loaded from memory: ");
    Serial.println(api_key);

  }else if(strcmp(api_key, "-") != 0){
    nvs_set_str(my_nvs_handle, "PrivateKey", api_key);
    nvs_commit(my_nvs_handle);

    Serial.print("Private Key loaded from code: ");
    Serial.println(api_key);
  }

  if(myStrlen(api_key) < 10 && CurrentStatus != WIFI_SETUP){
    Serial.print("ERROR: Short API KEY: ");
    Serial.println(api_key);

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_7x14B_tr);
    u8g2.drawButtonUTF8(64, 10, U8G2_BTN_HCENTER, 0,  2,  2, "Invalid Key!" );
    u8g2.sendBuffer();
    sleep(7);

    esp_system_abort("restart_after_wakeup");
  }

  
}


void loop() {

  //restart each 24 hours, when relay is OFF
  if(millis() > 86400000 && RelayStatus == 0){
    delay(5);
    esp_system_abort("restart_after_wakeup");
  }

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_7x14B_tr);

  if(!DEV_MODE && millis() <= 60000){
    Serial.println("Waiting 1 min. after start 4 safe-try");

    u8g2.setFont(u8g2_font_helvR08_tr);
    u8g2.drawButtonUTF8(62, 30, U8G2_BTN_HCENTER|U8G2_BTN_BW2, 34,  2,  2, "Waiting 1 min." );

    u8g2.sendBuffer();

    non_lock_delay(10000); // Esperamos 10 segundos
    return;
  }

  GetRemoteSettings();

  char textWithInt2[100]; 
  sprintf(textWithInt2, "Level: %d %%", sens_percent);
  u8g2.drawButtonUTF8(64, 10, U8G2_BTN_HCENTER, 0,  2,  2, textWithInt2);

  drawLevel(sens_percent);
  
  if(IDLE_CONNECTION_SECS != 0){
    if(sens_current_time - sens_event_time > IDLE_CONNECTION_SECS){
        AddEvent(IDDLE_SENSOR);
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_7x14B_tr);
        u8g2.drawButtonUTF8(64, 10, U8G2_BTN_HCENTER|U8G2_BTN_BW2, 34,  2,  2, "WARNING!" );
        u8g2.drawButtonUTF8(64, 35, U8G2_BTN_HCENTER|U8G2_BTN_BW2, 34,  2,  2, "IDLE SENSOR!" );
    }
  }

  if(ACTION == 1){
    ACTION = 0;
    RelayOn();

    IS_WAITING = false;
    IDLE_FLOW_EVENT_TIME = 0;
  }

  if(ACTION == -1){
    ACTION = 0;
    RelayOff();
  }

  if(SAFE_MODE == 1 && checkConsistency(Sensor5History, 5, 20) && sensor_last_distance <= (BLIND_DISTANCE - BLIND_MARGIN_EXTRA)){
    Serial.println("Danger blind distance detected");
    AddEvent(BLIND_AREA_DANGER);
    if(AUTO_OFF){
      RelayOff();

      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_7x14B_tr);
      u8g2.drawButtonUTF8(64, 10, U8G2_BTN_HCENTER|U8G2_BTN_BW2, 34,  2,  2, "AUTO OFF!" );
      u8g2.drawButtonUTF8(64, 35, U8G2_BTN_HCENTER|U8G2_BTN_BW2, 34,  2,  2, "Smart Mode OFF!" );

      non_lock_delay(20000); // Esperamos 20 segundos
    }
  }

  if(SAFE_MODE == 1 && Sensor5History[4] == 0 && Sensor5History[5] == 0){
    Serial.println("Danger blind distance or sensor fail detected");
    AddEvent(BLIND_AREA_DANGER);
    AddEvent(SENSOR_FAULT);
    if(AUTO_OFF){
      RelayOff();

      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_7x14B_tr);
      u8g2.drawButtonUTF8(64, 10, U8G2_BTN_HCENTER|U8G2_BTN_BW2, 34,  2,  2, "AUTO OFF!" );
      u8g2.drawButtonUTF8(64, 35, U8G2_BTN_HCENTER|U8G2_BTN_BW2, 34,  2,  2, "Smart Mode OFF!" );

      non_lock_delay(20000); // Esperamos 20 segundos
    }
  }

  if(AUTO_OFF > 0 && sensor_last_distance > 0 && sensor_last_distance <= BLIND_DISTANCE){
    AddEvent(BLIND_AREA);
    Serial.println("Top blind distance detected");
    RelayOff();

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_7x14B_tr);
    u8g2.drawButtonUTF8(64, 10, U8G2_BTN_HCENTER|U8G2_BTN_BW2, 34,  2,  2, "AUTO OFF!" );
    u8g2.drawButtonUTF8(64, 35, U8G2_BTN_HCENTER|U8G2_BTN_BW2, 34,  2,  2, "FULL" );

    non_lock_delay(20000); // Esperamos 20 segundos
  }

  if(AUTO_OFF > 0 && IDLE_CONNECTION_SECS != 0 &&  sens_current_time > 0 && sens_event_time > 0 && sens_current_time - sens_event_time > IDLE_CONNECTION_SECS){
    AddEvent(OFFLINE);
    Serial.println("Sensor offline AUTO_OFF");
    RelayOff();

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_7x14B_tr);
    u8g2.drawButtonUTF8(64, 10, U8G2_BTN_HCENTER|U8G2_BTN_BW2, 34,  2,  2, "AUTO OFF!" );
    u8g2.drawButtonUTF8(64, 35, U8G2_BTN_HCENTER|U8G2_BTN_BW2, 34,  2,  2, "SENSOR OFFLINE" );

    non_lock_delay(20000); // Esperamos 20 segundos
  }

  if(ALGO == 1){
    DoAlgo1();
  }
  u8g2.sendBuffer();
  non_lock_delay(WIFI_POOL_TIME); // Esperamos 10 segundos
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

bool checkConsistency(const int arr[], int size, int threshold) {
  // Check if last 2 sensor values have sense (>=0 and diff < threshold)

    Serial.println("checkConsistency");
    PrintHistory();
    // Check if there are at least two elements in the array
    if (size < 2) {
        return false; // Not enough elements to compare
    }

    // Get the last two values and calculate their absolute difference
    int lastValue = arr[size - 1];
    int secondLastValue = arr[size - 2];
    if(lastValue < 0 ||  secondLastValue < 0){
      return false; // not data yet
    }
    int difference = abs(lastValue - secondLastValue);

    // Return true if the difference is within the threshold
    return difference <= threshold;
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


void PrintHistory(){
  #ifdef DEBUG
    Serial.println("======================");
      Serial.println("Sensor5History: ");

      for (int i = 0; i <= 4; i++) {
        Serial.println(Sensor5History[i]);
      }
    Serial.println("======================");
  #endif
}

// ALGO #1 - try auto-start if capacity is under 30%, stop at 90%
// use simple iddle flow detection based in basic check
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
    AddEvent(OFFLINE);
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
      if(isHourInList(HOURS_OFF, gtm_4_hour)){
        Serial.println("Off hours detected");

        u8g2.sendBuffer();
        non_lock_delay(10000);
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_7x14B_tr);
        u8g2.drawButtonUTF8(64, 10, U8G2_BTN_HCENTER|U8G2_BTN_BW2, 34,  2,  2, "WARNING!" );
        u8g2.drawButtonUTF8(64, 35, U8G2_BTN_HCENTER|U8G2_BTN_BW2, 34,  2,  2, "Disabled Hours!" );
        u8g2.sendBuffer();
        non_lock_delay(5000);

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
      AddEvent(IDDLE_SENSOR);
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

    u8g2.sendBuffer();
    non_lock_delay(10000);
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_7x14B_tr);
    u8g2.drawButtonUTF8(64, 10, U8G2_BTN_HCENTER|U8G2_BTN_BW2, 34,  2,  2, "WARNING!" );
    u8g2.drawButtonUTF8(64, 35, U8G2_BTN_HCENTER|U8G2_BTN_BW2, 34,  2,  2, "Low Flow, Waiting!" );
    u8g2.sendBuffer();
    non_lock_delay(5000);

    return;
  }else if(IS_WAITING){
    Serial.println("Waiting done!");
    IS_WAITING = false;
    IDLE_FLOW_EVENT_TIME = 0;
    RelayOn();
  }

  // case: IDDLE FLOW 
  if(RelayStatus == 1 && MIN_FLOW_MM_X_MIN > 0 && sensor_last_distance > 0 && sensor_last_pool_time > 0){

    unsigned long runing_time = millis() - relay_on_start_time;
    if(MIN_FLOW_MM_X_MIN*60000 < runing_time){

      PrintHistory();

      if(!FlowHistoryGrow(Sensor5History, 5)){
        AddEvent(NOT_FLOW);
        IDLE_FLOW_EVENT_TIME = millis();
        RelayOff();
        IS_WAITING = true;
        Serial.println("Slow flow detected");
        return;
      }
      
      }
    
  }

  
  sens_current_time = 0;
  sens_event_time = 0;

  Serial.print("sens_percent: ");
  Serial.println(sens_percent);

  if(sens_percent <= START_LEVEL && RelayStatus == 0 && AUTO_ON == 1 && checkConsistency(Sensor5History, 5, 20)){
    AddEvent(START_LEVEL_EVENT);
    Serial.println("AUTO_ON ON");
    RelayOn();
  }

  if(sens_percent >= END_LEVEL && RelayStatus == 1 && AUTO_OFF == 1 && checkConsistency(Sensor5History, 5, 20)){
    AddEvent(END_LEVEL_EVENT);
    Serial.println("AUTO_OFF OFF");
    RelayOff();
  }
}


bool FlowHistoryGrow(const int arr[], int size) {

  for (int i = 1; i < size - 1; i++) { // Iterate from the second to the second-last element
        bool hasGreaterLeft = false;  // To check if there's a greater number on the left

        // Check if the current element is default -1
        if (arr[i] == -1) {
            return true;
        }

        // Check for a greater number on the left
        for (int j = 0; j < i; j++) {
            if (arr[j] > arr[i]) {
                hasGreaterLeft = true;
                break;
            }
        }

        // If both conditions are met, return true
        if (hasGreaterLeft) {
            return true;
        }
    }

    // Check if the first is default -1
    if (arr[0] == -1 ) {
        return true;
    }

    return false; // Return false if no element meets the conditions
}


void RelayOn(){
  if(RelayStatus == 0){
    AddEvent(PUMP_ON);
  }
  digitalWrite(RELAY_PIN_1, HIGH); // Activamos el primer relé
  Serial.println("Relay ON");
  RelayStatus = 1;
  Reset_BTN_Count = 0;

  IS_WAITING = false;
  IDLE_FLOW_EVENT_TIME = 0;
  relay_on_start_time = millis();

}

void RelayOff(){
  if(RelayStatus == 1){
    AddEvent(PUMP_OFF);
  }
  digitalWrite(RELAY_PIN_1, LOW); // Activamos el primer relé
  Serial.println("Relay OFF");
  RelayStatus = 0;
  ACTION = 0;
  Reset_BTN_Count = 0;
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
    delay(5);

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


void SetLocalConf(int _ALGO, int _START_LEVEL, int _END_LEVEL, int _AUTO_OFF, int _AUTO_ON, int _MIN_FLOW_MM_X_MIN, int _ACTION, int _BLIND_DISTANCE, String _HOURS_OFF, int _SAFE_MODE){

  Serial.print("SetLocalConf: _MIN_FLOW_MM_X_MIN: ");
  Serial.println(_MIN_FLOW_MM_X_MIN);

  if(_ACTION == 1 || _ACTION == -1){
    ACTION = _ACTION;
    return;
  }
  
  if(_ALGO >=0 && _ALGO <= 100){
    ALGO = _ALGO;
  }

  if(_SAFE_MODE >=0 && _SAFE_MODE <=1){
    SAFE_MODE = _SAFE_MODE;
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

  if(_BLIND_DISTANCE >= 0){
    BLIND_DISTANCE = _BLIND_DISTANCE + BLIND_MARGIN_EXTRA;
  }

  HOURS_OFF = _HOURS_OFF;

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


bool isHourInList(String hourList, int hour) {
  // Validate that the hour is within the allowed range
  if (hour < 0 || hour > 23) {
    return false;
  }

  // Convert the hour to a string
  String hourStr = String(hour);

  // Add commas to handle matches at the start, middle, and end of the list
  String formattedList = "," + hourList + ",";
  String formattedHour = "," + hourStr + ",";

  // Check if the formatted hour is in the formatted list
  return formattedList.indexOf(formattedHour) != -1;
}


String urlEncode(String str) {
  String encoded = "";
  char c;
  char hex[4]; // Para almacenar el formato %XX

  for (size_t i = 0; i < str.length(); i++) {
    c = str.charAt(i);

    // Codificar caracteres especiales según el estándar
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += c; // Dejar caracteres seguros sin cambios
    } else {
      sprintf(hex, "%%%02X", c); // Convertir a %XX
      encoded += hex;
    }
  }

  return encoded;
}


bool HttpRegDevice(){

  // Check for successful connection
  if (WiFi.status() == WL_CONNECTED) {
    #ifdef DEBUG
      Serial.println("Connected to WiFi");
    #else
       delay(5);
    #endif

    long timeout = 15000;

    rssi = WiFi.RSSI();
    Serial.print("Signal strength (RSSI): ");
    Serial.print(rssi);
    Serial.println(" dBm");

    HTTPClient https;
    https.setTimeout(timeout);
    https.setConnectTimeout(timeout);

    #ifdef DEBUG
      Serial.print("[HTTPS] begin...\n");
    #else
       delay(5);
    #endif


    char urlout[355];

    String email_encoded = urlEncode(email);
    // we are going to start here
    strcpy(urlout, host_link_url);
    strcat(urlout, "?key=");
    strcat(urlout, api_key);
    strcat(urlout, "&dtype=3");
    strcat(urlout, "&email=");
    strcat(urlout, email_encoded.c_str());

    if (https.begin(urlout)) {  // HTTPS

      //https.setVerify .setVerify(true);
      //https.setReuse(false);

      // Add a custom HTTP header (replace "Your-Header" and "Header-Value" with your actual header and value)
      https.addHeader("FW-Version", (String)FIRMW_VER);
      https.addHeader("RSSI", (String)rssi);

      https.setTimeout(timeout);

      // prepare to read response headers
      const char *headerKeys[] = {"fw-version", "wpl-key"}; // Agrega todas las claves de los encabezados que deseas recopilar
      const size_t headerKeysCount = sizeof(headerKeys) / sizeof(headerKeys[0]);
      // Recopila las cabeceras HTTP especificadas
      https.collectHeaders(headerKeys, headerKeysCount);

      // start connection and send HTTP header
      int httpCode = https.GET();

      // httpCode will be negative on error
      if (httpCode > 0) {
        // HTTP header has been send and Server response header has been handled

        #ifdef DEBUG
          Serial.printf("[HTTPS] GET... code: %d\n", httpCode);
        #else
          delay(5);
        #endif

        // file found at server
        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {

          String payload = https.getString();

          #ifdef DEBUG
            Serial.print("response payload: ");
            Serial.println(payload);
          #else
            delay(5);
          #endif

          if(payload == "OK"){

            String latest_relay_fw = https.header("fw-version");
            String wlp_key = https.header("wpl-key");

            #ifdef DEBUG
              Serial.print("assigned wpl-key: ");
              Serial.println(wlp_key);
              Serial.print("Old api_key: ");
              Serial.println(api_key);
            #endif

            if(wlp_key.length() >= 7)
            {
                strcpy(api_key, wlp_key.c_str());
                nvs_set_str(my_nvs_handle, "PrivateKey", api_key);
                nvs_commit(my_nvs_handle);
                delay(5);
            }
            #ifdef DEBUG
              else{
                Serial.println("Invalid key status");
              }
            #endif
            

          }

          https.end();
          return true;

        }
      } else {
        #ifdef DEBUG
          Serial.printf("[HTTPS] GET... failed, error: %s\n", https.errorToString(httpCode).c_str());
        #else
          delay(5);
        #endif
        https.end();
        return false;
      }

    } else {
      #ifdef DEBUG
        Serial.printf("[HTTPS] Unable to connect\n");
      #else
       delay(5);
    #endif
      return false;
    }

  } else {
    #ifdef DEBUG
      Serial.println("Failed to connect to WiFi");
    #else
       delay(5);
    #endif
  }
  return false;
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

      PrintEvents();

      https.addHeader("FW-Version", (String)FIRMW_VER);
      https.addHeader("RSSI", (String)rssi);
      https.addHeader("EVENTS", EventsLogString());
      
      https.setTimeout(timeout);

      // prepare to read response headers

      // Agrega todas las claves de los encabezados que deseas recopilar
      const char *headerKeys[] = {"percent", "event-time", "current-time", "fw-version", "distance", "pool-time",
        "ALGO", "START_LEVEL", "END_LEVEL", "AUTO_OFF", "AUTO_ON", "MIN_FLOW_MM_X_MIN", "ACTION", "BLIND_DISTANCE", "HOURS_OFF", "SAFE_MODE"
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
            ResetEvents();
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

            if(sens_event_time != sens_event_last_time){
              sens_event_last_time = sens_event_time;

              for (int i = 0; i < 4; i++) {
                  Sensor5History[i] = Sensor5History[i + 1];
              }
              Sensor5History[4] = sensor_distance.toInt();
            }
            
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
              https.header("AUTO_OFF").toInt(), https.header("AUTO_ON").toInt(), https.header("MIN_FLOW_MM_X_MIN").toInt(), 
              https.header("ACTION").toInt(), https.header("BLIND_DISTANCE").toInt(), https.header("HOURS_OFF"), https.header("SAFE_MODE").toInt());

          }else{
            AddEvent(DATA_POST_FAIL);
            Serial.println("Response error!");
          }

          https.end();
          return true;

        }
      } else {
        AddEvent(DATA_POST_FAIL);
        Serial.printf("[HTTPS] GET... failed, error: %s\n", https.errorToString(httpCode).c_str());
        https.end();
        return false;
      }

    } else {
      AddEvent(DATA_POST_FAIL);
      Serial.printf("[HTTPS] Unable to connect\n");
      return false;
    }

  } else {
    AddEvent(DATA_POST_FAIL);
    Serial.println("Failed to connect to WiFi");
    ConnectWifi();
  }
  return false;
}



bool SetupResetWifi(){

  //WiFi.setTxPower(WIFI_POWER_8_5dBm);
  WiFi.mode(WIFI_AP); // explicitly set mode
  //WiFi.setTxPower(WIFI_POWER_8_5dBm);
  delay(5);
  
  wm.setCaptivePortalEnable(true);

  std::vector<const char *> menu = {"wifi","restart","exit"};
  wm.setMenu(menu);

  // set configportal timeout 50 min
  wm.setConfigPortalTimeout(3000);
  wm.setSaveConnectTimeout(20);

  if (!wm.startConfigPortal(WIFI_NAME, WIFI_PASSW)) {
    Serial.println("failed to connect and hit timeout");

    nvs_set_i32(my_nvs_handle, "0-status", (int32_t)WIFI_SETUP);
    nvs_commit(my_nvs_handle);
    CurrentStatus = WIFI_SETUP;
    wm.resetSettings();

    delay(5);
    //reset and try again, or maybe put it to deep sleep
    esp_system_abort("restart_after_wakeup");
    delay(5);
  }

  delay(5);
  if(myStrlen(email) > 7){
    HttpRegDevice();
  }

  delay(5);
  nvs_set_i32(my_nvs_handle, "0-status", (int32_t)WIFI);
  nvs_commit(my_nvs_handle);
  CurrentStatus = WIFI;

  Serial.println("Setup Wifi Success");
  delay(5);
  esp_system_abort("restart_after_wakeup");

}


// Función que se ejecutará cuando el botón se presione
ICACHE_RAM_ATTR void botonPresionado() {

  if(millis() < 1000) return;

  Serial.println("El botón está presionado");
  AddEvent(BTN_PRESS);

  // req x3 times press the reset btn
  Reset_BTN_Count = Reset_BTN_Count + 1;
  if(Reset_BTN_Count < 3) {
    return;
  }

  nvs_set_i32(my_nvs_handle, "0-status", (int32_t)WIFI_SETUP);
  nvs_commit(my_nvs_handle);

  CurrentStatus = WIFI_SETUP;

  //reset settings
  wm.resetSettings();

  delay(5);
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
            delay(5);
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

void AddEvent(EventLogs _event) {
  for (int i = 4; i > 0; i--) {
      EventsArray[i] = EventsArray[i - 1];
  }
  EventsArray[0] = _event;
}

String EventsLogString() {
  String result = "";
  
  for (int i = 0; i < 5; i++) {
    result += String(EventsArray[i]);
    
    // Añadir coma después de cada valor, excepto el último
    if (i < 4) {
      result += ",";
    }
  }
  
  return result;
}

void PrintEvents(){
  #ifdef DEBUG
    Serial.print("Events: ");
    Serial.println(EventsLogString());
  #endif
}

void ResetEvents(){

  for (int i = 0; i <= 4; i++) {
    EventsArray[i] = NO_EVENT;
  }
}
