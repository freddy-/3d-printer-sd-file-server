
// armazenar string na flash sendHeader(String(F("Connection")), String(F("close")));

#include <ESP8266WiFi.h>
#include <SPI.h>
#include <SdFat.h>
#include "html/index_htm.h"
#include "html/update_htm.h"
#include "./wifi-credentials.h"
#include "custom_types.h"

// TODO usar mdns pra nao precisar de ip fixo

// TODO
// 1- renomear o index_html pra update_html
// 2- refatorar os ifs que verificam qual URL deverá ser tratada, separar em funções
// 3- criar um index_html com links para update e listagem de arquivos
// 4- criar a listagem de arquivo:
// 4.1- ao abrir a tela devera ter um botão pra consultar os arquivos
// 4.2- ao executar esta consulta, devemos pegar o sd, inicializar, pegar o tamanho total, tamanho
//      usado e listar os arquivos, em seguida retornar resetar o SD e devolver ele pro marlin


// Pins
#define SD_POWER    16 // D0
#define CS_PIN      5  // D1
#define MISO_PIN		12 // D6
#define MOSI_PIN		13 // D7
#define CLK_PIN     14 // D5
#define CD_PIN      4  // D2 - used to control sd card detection in marlin, LOW = SD inserted

// TODO get this data from sd card config.ini file
// ssid;passwd

extern const char* ssid;
extern const char* password;

// Create an instance of the server
// specify the port to listen on as an argument
WiFiServer server(80);
SdFat32 sd;
String ipAddr;
uint32_t maxSketchSpace;
const int MAX_ROUTES = 10;
RouteItem routes[MAX_ROUTES];
int numRoutes = 0;

void setup() {
  // first of all, power the SD card (it's already high at esp boot)
  pinMode(SD_POWER, OUTPUT);
  digitalWrite(SD_POWER, HIGH);

  // set MISO, MOSI, CLK, CS as input
  pinMode(MISO_PIN, INPUT); // NAO PRECISA DE RESISTOR! REMOVER 3 RESISTORES DESNECESSARIOS E COLOCAR EM PARALELEO COM OS 3 QUE SOBRAREM
  pinMode(MOSI_PIN, INPUT);
  pinMode(CLK_PIN,  INPUT);
  pinMode(CS_PIN,   INPUT);

  pinMode(CD_PIN,   OUTPUT); 
  digitalWrite(CD_PIN, LOW); // insert card into marlin


  Serial.begin(115200);
  Serial.printf("Update %d\n", Update.size());
  delay(10);

  maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
  Serial.println(maxSketchSpace);
  
  // Connect to WiFi network
  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  
  WiFi.mode(WIFI_STA);
  WiFi.setPhyMode(WIFI_PHY_MODE_11N);
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
  
  // Start the server
  server.begin();
  Serial.println("Server started");

  // Print the IP address
  ipAddr = WiFi.localIP().toString();
  Serial.println(ipAddr);

  // Setup Web Server Routes
  addRoute("GET / HTTP",            handleIndex);
  addRoute("GET /api/version",      handleGetApiVersion);
  addRoute("POST /api/files/local", handlePostFileUploadToSdCard);
  addRoute("GET /update",           handleGetUpdateFirmware);
  addRoute("POST /update",          handlePostUpdateFirmware);
  addRoute("GET /reboot",           handleReboot);

}

void loop() {
  // Check if a client has connected
  WiFiClient client = server.available();
  if (!client) {
    return;
  }
  
  // Wait until the client sends some data
  Serial.println("\nnew client");
  int timeout_ms = 1000;
  while(!client.available() && --timeout_ms) {
    delay(1);
  }

  if (!timeout_ms) {
    Serial.println("Client timed out!");
    return;
  }
  
  // Route the request
  routeRequest(client);
}

size_t readBytesWithTimeout(WiFiClient client, uint8_t *buf, size_t bufSize, size_t numToRead) {
	int timeout_ms = 1000;
	size_t numAvailable = 0;
	
	while(((numAvailable = client.available()) < numToRead) && client.connected() && timeout_ms--) 
		delay(1);

	if(!numAvailable)
		return 0;

	return client.read(buf, bufSize);
}

void handleWriteError(WiFiClient client, String message, File32 *file)	{
// ------------------------
	// close this file
	file->close();
	// delete the wrile being written
	//sd.remove(uri.c_str());
  Serial.print("Write Error: ");
  Serial.println(message);
  client.print("HTTP/1.1 400 Bad Request\r\n\r\n");
  client.flush();

  releaseSdBus();
  digitalWrite(CD_PIN, LOW);
}

void handleIndex(WiFiClient client) {
  returnStaticHtmlPage(client, index_htm_gz, index_htm_gz_len);
}

void handleGetApiVersion(WiFiClient client) {
  client.print(String(F("HTTP/1.1 200 OK\r\n\r\n{\"api\":\"0.1\",\"text\":\"OctoPrint 1.10.1\"}")));
  client.flush();
}

void handlePostFileUploadToSdCard(WiFiClient client) {
  String req;
  String fileName = "";
  size_t fileSize = 0;
  uint8_t boundaryLength = 0;

  // extract length and file name from headers
  while(1) {
    req = client.readStringUntil('\r');
    client.readStringUntil('\n');

    if(req.indexOf("Content-Length") != -1) {
      int headerDiv = req.indexOf(':');
      fileSize = req.substring(headerDiv + 2).toInt();
      continue;
    }

    int idxBoundary = req.indexOf("boundary");
    if (idxBoundary != -1) {
      boundaryLength = req.substring(idxBoundary + 9, req.length()).length();
      continue;
    }
    
    int idxFileName = req.indexOf("filename");
    if (idxFileName != -1) {
      fileName = req.substring(idxFileName + 10, req.length() - 1);
      break;
    }
  }

  if (fileSize == 0 || fileName.length() == 0) {
    Serial.println("Invalid request");
    client.print("HTTP/1.1 400 Bad Request\r\n\r\n");
    client.flush();
    return;
  }

  Serial.print("File: ");
  Serial.println(fileName);
  Serial.print("Size: ");
  Serial.println(fileSize);

  // pula o content type que restou da request
  client.readStringUntil('\r');
  client.readStringUntil('\n');

  // remove card from marlin and wait a little bit
  digitalWrite(CD_PIN, HIGH);
  delay(1000);

  takeSdBus();

  // SD init
  if (sd.begin(SdSpiConfig(CS_PIN, DEDICATED_SPI, SPI_FULL_SPEED))) { //SPI_DIV3_SPEED SPI_FULL_SPEED SPI_HALF_SPEED
    Serial.println("SD init ok");
  } else {
    Serial.println("SD init fail!!!!!");
    return;
  }  

  // receber o restante do request
  File32 file;
  size_t numRemaining = fileSize;
  const size_t WRITE_BLOCK_CONST = 512;
  uint8_t buf[WRITE_BLOCK_CONST];

  size_t contBlocks = (fileSize/WRITE_BLOCK_CONST + 1);
  uint32_t bgnBlock, endBlock;

  // remove old file if exists
  if (sd.exists(fileName.c_str())) {
    Serial.println("File exists, removing...");
    sd.remove(fileName.c_str());
  }

  if (!file.open(fileName.c_str(), O_RDWR | O_CREAT)) {
    return handleWriteError(client, "open file failed", &file);
  }
  if (!file.preAllocate(fileSize)) {
    return handleWriteError(client, "preAllocate failed", &file);
  }

  uint32_t time = millis();
  while(numRemaining > 0)	{
    size_t numToRead = (numRemaining > WRITE_BLOCK_CONST) ? WRITE_BLOCK_CONST : numRemaining;
    size_t numRead = readBytesWithTimeout(client, buf, sizeof(buf), numToRead);

    if(numRead == 0)
      break;

    if (file.write(buf, numRead) != numRead) {
      return handleWriteError(client, "Write data failed", &file);
    }

    numRemaining -= numRead;
  }
  time = millis() - time;
  Serial.printf("Speed: %d, time %d \n", fileSize/time, time);

  if (!file.sync()) {
    return handleWriteError(client, "File Sync failed", &file);
  }

  if (!file.truncate(fileSize - numRemaining - boundaryLength - 6)) {
    return handleWriteError(client, "Unable to truncate the file", &file);
  }

  file.close();

  Serial.println("File created!");
  
  client.print("HTTP/1.1 200 OK\r\n\r\n");
  client.flush();

  releaseSdBus();
  
  // do a power off in the SD card
  digitalWrite(SD_POWER, LOW);
  delay(200);

  // then power it up again
  digitalWrite(SD_POWER, HIGH);
  delay(200);

  // and then insert into marlin
  digitalWrite(CD_PIN, LOW);
}

void handleGetUpdateFirmware(WiFiClient client) {
  returnStaticHtmlPage(client, update_htm_gz, update_htm_gz_len);
}

void handlePostUpdateFirmware(WiFiClient client) {
  // curl -F "image=@esp8266-http-to-sd.ino.generic.bin" 192.168.0.70/update

  String req;
  size_t fileSize = 0;
  while(1) {
    req = client.readStringUntil('\n');

    if(req.indexOf("Content-Disposition: form-data; name=\"size\"") != -1) {
      client.readStringUntil('\n');       // read empty line
      req = client.readStringUntil('\n'); // read the value
      fileSize = req.toInt();
      continue;
    }

    if(req.indexOf("Content-Disposition: form-data; name=\"update\"") != -1) {
      client.readStringUntil('\n');       // read content type line
      client.readStringUntil('\n');       // read empty
      break;
    }
  }
  
  Serial.setDebugOutput(true);  
  if (!Update.begin(maxSketchSpace)) {    // start firmware update
    Serial.println("Erro ao iniciar update");
    Update.printError(Serial);
  }

  size_t numRemaining = fileSize;
  uint8_t buffer[2048];
  while(numRemaining > 0)	{
    size_t numToRead = (numRemaining > 2048) ? 2048 : numRemaining;
    size_t numRead = readBytesWithTimeout(client, buffer, numToRead, numToRead);

    if(numRead == 0) break;

    if (Update.write(buffer, numRead) != numRead) {
      Update.printError(Serial);
    }

    numRemaining -= numRead;
  }

  if (Update.end(true)) { // true to set the size to the current progress
    Serial.printf("Update Success: %u\nRebooting...\n", fileSize);
  } else {
    Update.printError(Serial);
  }
  Serial.setDebugOutput(false);

  client.print("HTTP/1.1 302 Found\r\n");
  client.print("Location: http://");
  client.print(ipAddr);
  client.print("/update?reboot=true\r\n\r\n");
  client.flush();
}

void handleReboot(WiFiClient client) {
  client.print("HTTP/1.1 200 ok\r\n\r\n");
  client.flush();
  ESP.restart();
}

void handleNotFound(WiFiClient client) {
  Serial.println("invalid request");
  client.print("HTTP/1.1 404 Not Found\r\n\r\n");
  client.flush();
}

void returnStaticHtmlPage(WiFiClient client, unsigned char* content, unsigned int length) {
  client.print("HTTP/1.1 200 OK\r\n");
  client.print("Content-Encoding:gzip\r\n");
  client.print("Content-Type:text/html\r\n\r\n");
  client.write(content, length);
  client.flush();
}

void addRoute(const char* path, RouteHandlerFunction handler) {
  if (numRoutes < MAX_ROUTES) {
    routes[numRoutes].path = path;
    routes[numRoutes].handler = handler;
    numRoutes++;
  } else {
    Serial.println("Erro: Numero maximo de rotas atingido!");
  }
}

void routeRequest(WiFiClient client) {
  // Read the first line of the request
  String req = client.readStringUntil('\r');
  client.readStringUntil('\n');
  Serial.println(req);

  const char* requestStr = req.c_str();

  for (int i = 0; i < numRoutes; i++) {
    if (strstr(requestStr, routes[i].path) != NULL) {
      routes[i].handler(client);
      return;
    }
  }

  handleNotFound(client);
}

void takeSdBus() {
  pinMode(MISO_PIN, SPECIAL);
  pinMode(MOSI_PIN, SPECIAL);
  pinMode(CLK_PIN,  SPECIAL);
  pinMode(CS_PIN,   OUTPUT);
}

void releaseSdBus() {
  pinMode(MISO_PIN, INPUT);
  pinMode(MOSI_PIN, INPUT);
  pinMode(CLK_PIN,  INPUT);
  pinMode(CS_PIN,   INPUT);
}