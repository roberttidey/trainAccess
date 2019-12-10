/*
 R. J. Tidey 2019/08/22
 Train times display
 Designed to run on battery with deep sleep after a timeoutbut can be overridden for maintenance
 WifiManager can be used to config wifi network
 
 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 version 2 as published by the Free Software Foundation.
 */
#define ESP8266

#include <ESP8266WiFi.h>
#include <WiFiClientSecureBearSSL.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <DNSServer.h>
#include <WiFiManager.h>
#include "FS.h"
#include <SPI.h>
#include <TFT_eSPI.h> // Hardware-specific library

#define POWER_HOLD_PIN 16
#define KEY1 0
#define KEY2 1
#define KEY3 2
#define LONG_PRESS 1000
int pinInputs[3] = {0,12,5};
int pinStates[3];
unsigned long pinTimes[3];
int pinChanges[3];
unsigned long pinTimers[3];


//put -1 s at end
int unusedPins[11] = {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};

/*
Wifi Manager Web set up
If WM_NAME defined then use WebManager
*/
#define WM_NAME "trains"
#define WM_PASSWORD "password"
#ifdef WM_NAME
	WiFiManager wifiManager;
#endif
char wmName[33];

//decoding response
#define SERVICE_START "service"
#define SERVICE_TIME "std"
#define SERVICE_EXPECTED "etd"
#define SERVICE_PLATFORM "platform"
#define SERVICE_ORIGIN "origin"
#define SERVICE_DESTINATION "destination"
#define SERVICE_LOCATION "locationName"
#define RESPONSE_FINDTAG 0
#define RESPONSE_FINDVAL 1
#define RESPONSE_BUFFSZ 512
#define RESPONSE_BUFFRD 64
char responseBuff[RESPONSE_BUFFSZ + 1] = {0};
String responseCurrent;
//0=origin, 1=destination
int responseLocation;

//uncomment to use a static IP
//#define WM_STATIC_IP 192,168,0,100
//#define WM_STATIC_GATEWAY 192,168,0,1

#define WIFI_CHECK_TIMEOUT 30000
#define BUTTON_INTTIME_MIN 250
int timeInterval = 50;
unsigned long elapsedTime;
unsigned long wifiCheckTime;
unsigned long startUpTime;
int updateInterval = 60;
unsigned long lastChangeTime;
unsigned long noChangeTimeout = 60000;
//holds the current upload
File fsUploadFile;

int8_t timeZone = 0;
int8_t minutesTimeZone = 0;
time_t currentTime;

//For update service
String host = "esp8266-trains";
const char* update_path = "/firmware";
const char* update_username = "admin";
const char* update_password = "password;

//AP definitions
#define AP_SSID "ssid"
#define AP_PASSWORD "password"
#define AP_MAX_WAIT 10
String macAddr;

#define AP_AUTHID "12345678"
#define AP_PORT 80

ESP8266WebServer server(AP_PORT);
ESP8266HTTPUpdateServer httpUpdater;
WiFiClient client;
HTTPClient http;
TFT_eSPI tft = TFT_eSPI();

#define CONFIG_FILE "/trainsConfig.txt"
//OFF= on all time with wifi. LIGHT=onll time no wifi, DEEP=one shot + wake up
#define SLEEP_MODE_OFF 0
#define SLEEP_MODE_DEEP 1
#define SLEEP_MODE_LIGHT 2

//general variables
int logging = 0;
int sleepMode = SLEEP_MODE_DEEP;
int sleepForce = 0;
float newTemp;

float ADC_CAL =0.96;
float battery_mult = 1220.0/220.0/1024;//resistor divider, vref, max count
float battery_volts;

#define MAX_ROWS 40
#define S_STD 0
#define S_ETD 1
#define S_PLATFORM 2
#define S_ORIGIN 3
#define S_DESTINATION 4
#define S_HEIGHT 5
#define S_MAX 6
#define STATIONS_MAX 4
String trainsAccessToken = "";
String trainsURL = "";
String trainsStationQuery = "";
String trainsFingerprint = "";
String trainsStationsString = "";
String trainsDestinationsString = "";
String trainsStations[STATIONS_MAX] = {"","","",""};
String trainsDestinations[STATIONS_MAX] = {"","","",""};
String trainsRows = "5";
String services[MAX_ROWS][S_MAX];
int stationIndex = 0;
int serviceCount;
int oldServiceCount;
int servicesChanged;
int servicesRefresh = 1;
int serviceOffset = 0;
int displayRows = 11;
String colWidthsHtString = "44,66,20,100,100,20";
int colWidths[S_MAX];
String fieldWidthsString = "5,8,2,12,12";
int fieldWidths[5];
String colHdrs[S_MAX] = {"STD","ETD","PL","From","To"};
int rotation = 1;

void ICACHE_RAM_ATTR  delaymSec(unsigned long mSec) {
	unsigned long ms = mSec;
	while(ms > 100) {
		delay(100);
		ms -= 100;
		ESP.wdtFeed();
	}
	delay(ms);
	ESP.wdtFeed();
	yield();
}

void ICACHE_RAM_ATTR  delayuSec(unsigned long uSec) {
	unsigned long us = uSec;
	while(us > 100000) {
		delay(100);
		us -= 100000;
		ESP.wdtFeed();
	}
	delayMicroseconds(us);
	ESP.wdtFeed();
	yield();
}

void unusedIO() {
	int i;
	
	for(i=0;i<11;i++) {
		if(unusedPins[i] < 0) {
			break;
		} else if(unusedPins[i] != 16) {
			pinMode(unusedPins[i],INPUT_PULLUP);
		} else {
			pinMode(16,INPUT_PULLDOWN_16);
		}
	}
}

/*
  Connect to local wifi with retries
  If check is set then test the connection and re-establish if timed out
*/
int wifiConnect(int check) {
	if(check) {
		if((elapsedTime - wifiCheckTime) * timeInterval > WIFI_CHECK_TIMEOUT) {
			if(WiFi.status() != WL_CONNECTED) {
				Serial.println(F("Wifi connection timed out. Try to relink"));
			} else {
				wifiCheckTime = elapsedTime;
				return 1;
			}
		} else {
			return 1;
		}
	}
	wifiCheckTime = elapsedTime;
#ifdef WM_NAME
	Serial.println(F("Set up managed Web"));
#ifdef WM_STATIC_IP
	wifiManager.setSTAStaticIPConfig(IPAddress(WM_STATIC_IP), IPAddress(WM_STATIC_GATEWAY), IPAddress(255,255,255,0));
#endif
	wifiManager.setConfigPortalTimeout(180);
	//Revert to STA if wifimanager times out as otherwise APA is left on.
	strcpy(wmName, WM_NAME);
	strcat(wmName, macAddr.c_str());
	wifiManager.autoConnect(wmName, WM_PASSWORD);
	WiFi.mode(WIFI_STA);
#else
	Serial.println(F("Set up manual Web"));
	int retries = 0;
	Serial.print(F("Connecting to AP"));
	#ifdef AP_IP
		IPAddress addr1(AP_IP);
		IPAddress addr2(AP_DNS);
		IPAddress addr3(AP_GATEWAY);
		IPAddress addr4(AP_SUBNET);
		WiFi.config(addr1, addr2, addr3, addr4);
	#endif
	WiFi.begin(AP_SSID, AP_PASSWORD);
	while (WiFi.status() != WL_CONNECTED && retries < AP_MAX_WAIT) {
		delaymSec(1000);
		Serial.print(".");
		retries++;
	}
	Serial.println("");
	if(retries < AP_MAX_WAIT) {
		Serial.print("WiFi connected ip ");
		Serial.print(WiFi.localIP());
		Serial.printf(":%d mac %s\r\n", AP_PORT, WiFi.macAddress().c_str());
		return 1;
	} else {
		Serial.println(F("WiFi connection attempt failed")); 
		return 0;
	} 
#endif
	//wifi_set_sleep_type(LIGHT_SLEEP_T);
}


void initFS() {
	if(!SPIFFS.begin()) {
		Serial.println(F("No SIFFS found. Format it"));
		if(SPIFFS.format()) {
			SPIFFS.begin();
		} else {
			Serial.println(F("No SIFFS found. Format it"));
		}
	} else {
		Serial.println(F("SPIFFS file list"));
		Dir dir = SPIFFS.openDir("/");
		while (dir.next()) {
			Serial.print(dir.fileName());
			Serial.print(F(" - "));
			Serial.println(dir.fileSize());
		}
	}
}

String getContentType(String filename){
  if(server.hasArg("download")) return "application/octet-stream";
  else if(filename.endsWith(".htm")) return "text/html";
  else if(filename.endsWith(".html")) return "text/html";
  else if(filename.endsWith(".css")) return "text/css";
  else if(filename.endsWith(".js")) return "application/javascript";
  else if(filename.endsWith(".png")) return "image/png";
  else if(filename.endsWith(".gif")) return "image/gif";
  else if(filename.endsWith(".jpg")) return "image/jpeg";
  else if(filename.endsWith(".ico")) return "image/x-icon";
  else if(filename.endsWith(".xml")) return "text/xml";
  else if(filename.endsWith(".pdf")) return "application/x-pdf";
  else if(filename.endsWith(".zip")) return "application/x-zip";
  else if(filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}

bool handleFileRead(String path){
  Serial.printf_P(PSTR("handleFileRead: %s\r\n"), path.c_str());
  if(path.endsWith("/")) path += "index.htm";
  String contentType = getContentType(path);
  String pathWithGz = path + ".gz";
  if(SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)){
    if(SPIFFS.exists(pathWithGz))
      path += ".gz";
    File file = SPIFFS.open(path, "r");
    size_t sent = server.streamFile(file, contentType);
    file.close();
    return true;
  }
  return false;
}

void handleFileUpload(){
  if(server.uri() != "/edit") return;
  HTTPUpload& upload = server.upload();
  if(upload.status == UPLOAD_FILE_START){
    String filename = upload.filename;
    if(!filename.startsWith("/")) filename = "/"+filename;
    Serial.printf_P(PSTR("handleFileUpload Name: %s\r\n"), filename.c_str());
    fsUploadFile = SPIFFS.open(filename, "w");
    filename = String();
  } else if(upload.status == UPLOAD_FILE_WRITE){
    Serial.printf_P(PSTR("handleFileUpload Data: %d\r\n"), upload.currentSize);
    if(fsUploadFile)
      fsUploadFile.write(upload.buf, upload.currentSize);
  } else if(upload.status == UPLOAD_FILE_END){
    if(fsUploadFile)
      fsUploadFile.close();
    Serial.printf_P(PSTR("handleFileUpload Size: %d\r\n"), upload.totalSize);
  }
}

void handleFileDelete(){
  if(server.args() == 0) return server.send(500, "text/plain", "BAD ARGS");
  String path = server.arg(0);
  Serial.printf_P(PSTR("handleFileDelete: %s\r\n"),path.c_str());
  if(path == "/")
    return server.send(500, "text/plain", "BAD PATH");
  if(!SPIFFS.exists(path))
    return server.send(404, "text/plain", "FileNotFound");
  SPIFFS.remove(path);
  server.send(200, "text/plain", "");
  path = String();
}

void handleFileCreate(){
  if(server.args() == 0)
    return server.send(500, "text/plain", "BAD ARGS");
  String path = server.arg(0);
  Serial.printf_P(PSTR("handleFileCreate: %s\r\n"),path.c_str());
  if(path == "/")
    return server.send(500, "text/plain", "BAD PATH");
  if(SPIFFS.exists(path))
    return server.send(500, "text/plain", "FILE EXISTS");
  File file = SPIFFS.open(path, "w");
  if(file)
    file.close();
  else
    return server.send(500, "text/plain", "CREATE FAILED");
  server.send(200, "text/plain", "");
  path = String();
}

void handleFileList() {
  if(!server.hasArg("dir")) {server.send(500, "text/plain", "BAD ARGS"); return;}
  
  String path = server.arg("dir");
  Serial.printf_P(PSTR("handleFileList: %s\r\n"),path.c_str());
  Dir dir = SPIFFS.openDir(path);
  path = String();

  String output = "[";
  while(dir.next()){
    File entry = dir.openFile("r");
    if (output != "[") output += ',';
    bool isDir = false;
    output += "{\"type\":\"";
    output += (isDir)?"dir":"file";
    output += "\",\"name\":\"";
    output += String(entry.name()).substring(1);
    output += "\"}";
    entry.close();
  }
  output += "]";
  server.send(200, "text/json", output);
}

void handleMinimalUpload() {
  char temp[700];

  snprintf ( temp, 700,
    "<!DOCTYPE html>\
    <html>\
      <head>\
        <title>ESP8266 Upload</title>\
        <meta charset=\"utf-8\">\
        <meta http-equiv=\"X-UA-Compatible\" content=\"IE=edge\">\
        <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\
      </head>\
      <body>\
        <form action=\"/edit\" method=\"post\" enctype=\"multipart/form-data\">\
          <input type=\"file\" name=\"data\">\
          <input type=\"text\" name=\"path\" value=\"/\">\
          <button>Upload</button>\
         </form>\
      </body>\
    </html>"
  );
  server.send ( 200, "text/html", temp );
}

void handleSpiffsFormat() {
	SPIFFS.format();
	server.send(200, "text/json", "format complete");
}

void parseCSV(String csv, int fMax, int fType) {
	int i,j,k;
	i=0;
	k = 0;
	while(k < fMax) {
		j = csv.indexOf(',', i);
		if(j<0) j = 255;
		switch(fType) {
			case 0: 
				colWidths[k] = csv.substring(i,j).toInt();
				break;
			case 1:
				fieldWidths[k] = csv.substring(i,j).toInt();
				break;
			case 2:
				trainsStations[k] = csv.substring(i,j);
				break;
			case 3:
				trainsDestinations[k] = csv.substring(i,j);
				break;
		}
		k++;
		if(j == 255) break;
		i = j + 1;
	}
	//Fill up any unspecified array elements
	switch(fType) {
		case 0: 
			for(i = k; i < fMax; i++) colWidths[k] = 20;
			break;
		case 1:
			for(i = k; i < fMax; i++) fieldWidths[k] = 8;
			break;
		case 2:
			for(i = k; i < fMax; i++) trainsStations[i] = trainsStations[i-1];
			break;
		case 3:
			for(i = k; i < fMax; i++) trainsDestinations[i] = trainsDestinations[i-1];
			break;
	}
}

/*
  Get config
*/
void getConfig() {
	String line = "";
	int config = 0;
	File f = SPIFFS.open(CONFIG_FILE, "r");
	if(f) {
		while(f.available()) {
			line =f.readStringUntil('\n');
			line.replace("\r","");
			if(line.length() > 0 && line.charAt(0) != '#') {
				switch(config) {
					case 0: host = line;break;
					case 1: trainsAccessToken = line;break;
					case 2: trainsURL = line;break;
					case 3: trainsStationQuery = line;break;
					case 4: trainsFingerprint = line;break;
					case 5: trainsStationsString = line;break;
					case 6: trainsDestinationsString = line;break;
					case 7: trainsRows = line;break;
					case 8: sleepMode = line.toInt();break;
					case 9: updateInterval = line.toInt();break;
					case 10: noChangeTimeout = line.toInt();break;
					case 11: colWidthsHtString = line; break;
					case 12: fieldWidthsString = line; break;
					case 13: rotation = line.toInt();break;
					case 14: displayRows = line.toInt();break;
					case 15:
						ADC_CAL =line.toFloat();
						Serial.println(F("Config loaded from file OK"));
						break;
				}
				config++;
			}
		}
		f.close();
		if(updateInterval < 15) updateInterval = 15;
		if(trainsRows.toInt() > MAX_ROWS) trainsRows = String(MAX_ROWS);
		if(displayRows < 4) displayRows = 4;
		Serial.println("Config loaded");
		Serial.print(F("host:"));Serial.println(host);
		Serial.print(F("trainsAccessToken:"));Serial.println(trainsAccessToken);
		Serial.print(F("trainsURL:"));Serial.println(trainsURL);
		Serial.print(F("trainsStationQuery:"));Serial.println(trainsStationQuery);
		Serial.print(F("trainsFingerprint:"));Serial.println(trainsFingerprint);
		Serial.print(F("trainsStationsString:"));Serial.println(trainsStationsString);
		Serial.print(F("trainsDestinationsString:"));Serial.println(trainsDestinationsString);
		Serial.print(F("trainsRows:"));Serial.println(trainsRows);
		Serial.print(F("sleepMode:"));Serial.println(sleepMode);
		Serial.print(F("updateInterval:"));Serial.println(updateInterval);
		Serial.print(F("noChangeTimeout:"));Serial.println(noChangeTimeout);
		Serial.print(F("colWidthsHtString:"));Serial.println(colWidthsHtString);
		Serial.print(F("fieldWidthsString:"));Serial.println(fieldWidthsString);
		Serial.print(F("rotation:"));Serial.println(rotation);
		Serial.print(F("displayRows:"));Serial.println(displayRows);
		Serial.print(F("ADC_CAL:"));Serial.println(ADC_CAL);
	} else {
		Serial.println(String(CONFIG_FILE) + " not found");
	}
	parseCSV(colWidthsHtString, S_MAX, 0);
	parseCSV(fieldWidthsString, S_MAX, 1);
	parseCSV(trainsStationsString, STATIONS_MAX, 2);
	parseCSV(trainsDestinationsString, STATIONS_MAX, 3);
}

/*
   process xml tag, val pair
*/
void processTag(String tagName, String tagVal) {
	String tag;
	int index;
	int iTag;
	
	index = tagName.indexOf(':');
	if(index >=0) {
		tag = tagName.substring(index+1);
	} else {
		tag = tagName;
	}
	if(tag == SERVICE_START) {
		serviceCount++;
	} else if(serviceCount) {
		iTag = -1;
		if(tag == SERVICE_TIME) {
			iTag = S_STD;
		} else if(tag == SERVICE_EXPECTED) {
			iTag = S_ETD;
		} else if(tag == SERVICE_PLATFORM) {
			iTag = S_PLATFORM;
		} else if(tag == SERVICE_ORIGIN) {
			responseLocation = 0;
		} else if(tag == SERVICE_DESTINATION) {
			responseLocation = 1;
		} else if(tag == SERVICE_LOCATION) {
			if(responseLocation) {
				iTag = S_DESTINATION;
			} else {
				iTag = S_ORIGIN;
			}
		}
		if(iTag >= 0) {
			tagVal = tagVal.substring(0,fieldWidths[iTag]);
			if(services[serviceCount - 1][iTag] != tagVal) {
				services[serviceCount - 1][iTag] = 	tagVal;
				servicesChanged = 1;
			}
		}
	}
}

/*
   process responseBuff
*/
void processResponseBuff() {
	int responseState = RESPONSE_FINDTAG;
	char *p1;
	char *p2;
	String tagName, tagVal;
	
	p1 = responseBuff;
	while(p1 && strlen(p1)) {
		switch(responseState) {
			case RESPONSE_FINDTAG :
				p1 = strchr(p1, '<');
				if(p1) {
					p2 = strchr(p1, '>');
					if(p2) {
						p2[0] = 0;
						tagName = String(p1+1);
						p2[0] = '>';
						if(tagName.indexOf('/') < 0) {
							responseState = RESPONSE_FINDVAL;
							p1 = p2 + 1;
						} else {
							memmove(responseBuff, p2 + 1, strlen(p2) + 1);
							p1 = responseBuff;
						}
					} else {
						p1 = NULL;
					}
				}
				break;
			case RESPONSE_FINDVAL :
				p2 = strchr(p1, '<');
				if(p2) {
					p2[0] = 0;
					tagVal = String(p1);
					p2[0] = '<';
					processTag(tagName, tagVal);
					memmove(responseBuff, p2, strlen(p2) + 1);
					p1 = responseBuff;
					responseState = RESPONSE_FINDTAG;
				} else {
					p1 = NULL;
				}
				break;
		}
	}
}

/*
 Make trains query and process response
*/
void queryTrainsDB(String msg) {
	int len,c;
	size_t size, space;
	
	if(msg.length()) {
		Serial.print("[HTTPS] begin...\r\n");
		std::unique_ptr<BearSSL::WiFiClientSecure>client(new BearSSL::WiFiClientSecure);
		if(trainsFingerprint.length() > 40) {
			client->setFingerprint(trainsFingerprint.c_str());
		} else {
			client->setInsecure();
		}
		http.begin(*client, trainsURL);
		http.addHeader("Content-Type", "text/xml");
		int httpCode = http.POST(msg);
		if (httpCode > 0) {
			len = http.getSize();
			Serial.printf("[HTTP] code: %d  size:%d\r\n", httpCode, len);
			// file found at server
			if(httpCode >= 200 && httpCode <= 299) {
				// read all data from server
				char *p;
				responseBuff[0] = 0;
				while(http.connected() && (len > 0 || len == -1)) {
					p = responseBuff + strlen(responseBuff);
					size = client->available();
					space = responseBuff - p + RESPONSE_BUFFSZ - 2;
					if (size && (responseBuff - p + RESPONSE_BUFFSZ) > (RESPONSE_BUFFRD - 1)) {
						c = client->readBytes((uint8_t*)p, ((size > space) ? space : size));
						if (!c) {
							Serial.println("read timeout");
						}
						p[c] = 0;
						processResponseBuff();
						if (len > 0) {
							len -= c;
						}
					} else {
						break;
					}
					delay(1);
				}
			} 
		} else {
			Serial.print("[HTTP] ... failed, error:" + http.errorToString(httpCode) + "\n");
		}
		Serial.println("http ended");
		http.end();
	}
}

String makeQuery(String query) {
	query.replace("%t", trainsAccessToken);
	query.replace("%s", trainsStations[stationIndex]);
	query.replace("%d", trainsDestinations[stationIndex]);
	query.replace("%r", trainsRows);
	return query;
}

void getStationBoard() {
	int i;
	servicesChanged = 0;
	serviceCount = 0;
	responseCurrent = "";
	queryTrainsDB(makeQuery(trainsStationQuery));
	if(serviceCount != oldServiceCount) {
		servicesChanged = 1;
		oldServiceCount = serviceCount;
	}
}

void initDisplay(int first) {
	int i, j;
	int x, x1;
	int y;
	String hdr;
	tft.fillRect(0, 0, TFT_HEIGHT - 1, colWidths[S_HEIGHT] - 1, TFT_GREEN);
	tft.setTextColor(TFT_BLACK, TFT_GREEN);
	x = 0;
	x1 = 0;
	y = 0;
	for(i = 0; i < S_MAX; i++) {
		x1 = x + colWidths[i] / 2;
		x+= colWidths[i];
		hdr = colHdrs[i];
		if(i == 3) hdr+= " - " + trainsStations[stationIndex];
		if(i == 4) hdr+= " - " + trainsDestinations[stationIndex];
		tft.drawCentreString(hdr, x1,y,2);
	}
	if(first) {
		y += colWidths[S_HEIGHT];
		for(i=0; i < displayRows; i++) {
			tft.fillRect(0, y, TFT_HEIGHT - 1, colWidths[S_HEIGHT] - 1,  (i & 1)? TFT_BLUE : TFT_RED);
			y+= colWidths[S_HEIGHT];
		}
		tft.setTextColor(TFT_WHITE, TFT_RED);
		tft.drawCentreString("Wait", colWidths[0] / 2, colWidths[S_HEIGHT],2);
	}
}

void displayServices(int start) {
	int i, j;
	int x, x1;
	int y;
	
	if(servicesChanged) {
		Serial.println("display Services" + String(serviceCount) + " from " + String(start));
		y = colWidths[S_HEIGHT];
		for(i=0; i < displayRows; i++) {
			tft.fillRect(0, y, TFT_HEIGHT - 1, colWidths[S_HEIGHT] - 1,  (i & 1)? TFT_BLUE : TFT_RED);
			if((i + start) < serviceCount) {
				tft.setTextColor(TFT_WHITE, (i & 1)? TFT_BLUE : TFT_RED);
				x = 0;
				x1 = 0;
				for(j = 0; j < S_MAX; j++) {
					x1 = x + colWidths[j] / 2;
					x+= colWidths[j];
					tft.drawCentreString(services[i + start][j],x1,y,2);
					//Serial.print(services[i + start][j] + ",");
				}
			}
			//Serial.println();
			y+= colWidths[S_HEIGHT];
		}
		x = 0;
		for (i = 0; i < S_MAX; i++) {
			x+= colWidths[i];
			tft.drawLine(x - 1, 0, x - 1, TFT_WIDTH - 1, TFT_WHITE);
		}
	}
}

/*
	check and time button events
*/
int checkButtons() {
	int i;
	int pin;
	unsigned long period;
	int changed = 0;
	
	for(i=0; i<3;i++) {
		pin = digitalRead(pinInputs[i]);
		if(pin == 0 && pinStates[i] == 1){
			pinTimes[i] = elapsedTime;
		} else if(pin == 1 && (pinStates[i] == 0)) {
			changed = 1;
			period = (elapsedTime - pinTimes[i]) * timeInterval;
			pinChanges[i] = (period>LONG_PRESS) ? 2:1;
		}
		pinStates[i] = pin;
	}
	return changed;
}

/*
	action button events
*/
int processButtons() {
	int changed = 0;
	if(pinChanges[KEY1] == 2) {
		//Long press
		stationIndex++;
		if(stationIndex >= STATIONS_MAX) stationIndex = 0;
		initDisplay(0);
		changed = 1;
		pinChanges[KEY1] = 0;
	} else if (pinChanges[KEY1] == 1) {
		//Short press
		serviceOffset -= displayRows;
		if(serviceOffset < 0) serviceOffset = 0;
		changed = 1;
		servicesChanged = 1;
		servicesRefresh = 0;
		pinChanges[KEY1] = 0;
	} else if (pinChanges[KEY2] == 2) {
		//Long press
		sleepForce = 1;
		changed = 2;
		servicesRefresh = 0;
		pinChanges[KEY2] = 0;
	} else if (pinChanges[KEY2] == 1) {
		//Short press
		pinChanges[KEY2] = 0;
	} else if (pinChanges[KEY3] == 2) {
		//Long press
		tft.fillRect(0, colWidths[S_HEIGHT], TFT_HEIGHT - 1, colWidths[S_HEIGHT] - 1, TFT_RED);
		tft.drawString("Battery:" + String(battery_volts) + " ip:" + WiFi.localIP().toString(),0,colWidths[S_HEIGHT],2);
		changed = 1;
		servicesRefresh = 0;
		//prevent overwrite of display
		servicesChanged = 0;
		pinChanges[KEY3] = 0;
	} else if (pinChanges[KEY3] == 1) {
		//Short press
		serviceOffset += displayRows;
		if(serviceOffset > (serviceCount - displayRows)) serviceOffset = serviceCount - displayRows;
		changed = 1;
		servicesChanged = 1;
		servicesRefresh = 0;
		pinChanges[KEY3] = 0;
	}
	return changed;
}

/*
  Set up basic wifi, collect config from flash/server, initiate update server
*/
void setup() {
	int i;
	startUpTime = millis();
	unusedIO();
	if(POWER_HOLD_PIN >= 0) {
		digitalWrite(POWER_HOLD_PIN, 0);
		pinMode(POWER_HOLD_PIN, OUTPUT);
	}
	Serial.begin(115200);
	Serial.println(F("Set up filing system"));
	initFS();
	sleepMode = SLEEP_MODE_OFF;
	getConfig();
	tft.init();
	tft.setRotation(rotation);
	initDisplay(1);
	for(i=0; i<3;i++) {
		pinMode(pinInputs[i], INPUT_PULLUP);
		pinStates[i] = 1;
		pinTimers[i] = elapsedTime;
	}
	if(digitalRead(pinInputs[KEY3]) == 0) {
		sleepMode = SLEEP_MODE_OFF;
		Serial.println(F("Sleep override active"));
	}
	Serial.println(F("Set up Wifi services"));
	macAddr = WiFi.macAddress();
	macAddr.replace(":","");
	Serial.println(macAddr);
	wifiConnect(0);
	//Update service
	MDNS.begin(host.c_str());
	httpUpdater.setup(&server, update_path, update_username, update_password);
	Serial.println(F("Set up web server"));
	//Simple upload
	server.on("/upload", handleMinimalUpload);
	server.on("/format", handleSpiffsFormat);
	server.on("/list", HTTP_GET, handleFileList);
	//load editor
	server.on("/edit", HTTP_GET, [](){
	if(!handleFileRead("/edit.htm")) server.send(404, "text/plain", "FileNotFound");});
	//create file
	server.on("/edit", HTTP_PUT, handleFileCreate);
	//delete file
	server.on("/edit", HTTP_DELETE, handleFileDelete);
	//first callback is called after the request has ended with all parsed arguments
	//second callback handles file uploads at that location
	server.on("/edit", HTTP_POST, [](){ server.send(200, "text/plain", ""); }, handleFileUpload);
	//called when the url is not defined here
	//use it to load content from SPIFFS
	server.onNotFound([](){if(!handleFileRead(server.uri())) server.send(404, "text/plain", "FileNotFound");});
	server.begin();
	MDNS.addService("http", "tcp", 80);
	http.begin(trainsURL, trainsFingerprint);
	http.addHeader("Content-Type", "text/xml");
	lastChangeTime = millis();
	Serial.println(F("Set up complete"));
}

/*
  Main loop to read temperature and publish as required
*/
void loop() {
	int i,c;
	battery_volts = battery_mult * ADC_CAL * analogRead(A0);
	if(servicesRefresh) getStationBoard();
	servicesRefresh = 1;
	displayServices(serviceOffset);
	if((sleepMode == SLEEP_MODE_DEEP) && (millis() > (lastChangeTime  + noChangeTimeout)) || sleepForce) {
		WiFi.mode(WIFI_OFF);
		delaymSec(10);
		WiFi.forceSleepBegin();
		delaymSec(1000);
		if(POWER_HOLD_PIN >=0) pinMode(POWER_HOLD_PIN, INPUT);
		ESP.deepSleep(0);
	}
	for(i = 0;i < updateInterval*1000/timeInterval; i++) {
		server.handleClient();
		wifiConnect(1);
		delaymSec(timeInterval);
		elapsedTime++;
		if(checkButtons()) {
			c = processButtons();
			if(c) {
				if(c == 1) {
					lastChangeTime = millis();
				}
				break;
			}
		}
	}
}
