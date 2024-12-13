/*
 *  This project mimics the file upload endpoint of OctoPrint
 *  allowing sending gcode from prusa slicer and saving direct to SD card.
 * 
 *  The SD card is shared between ESP8266 and 3d printer.
 *  
 *  This project is based on ardyesp/ESPWebDAV and FYSETC/ESPWebDAV.
 * 
 * Use ESP8266 version 2.4.X!!!
 * USe SdFat version 1.1.4
 */

// testar updater ota
// usar como exemplo:
// ESP8266HTTPUpdate
// Updater.h e 
// ArduinoOTAClass::_runUpdate()
// WebUpdate.ino

// armazenar string na flash sendHeader(String(F("Connection")), String(F("close")));


// TODO antes de iniciar qualquer aceso ao cartão SD
// simular uma desconexão do cartao SD e fazer um init dele, pois ele já vai estar operando em SDIO

#include <ESP8266WiFi.h>
#include <SPI.h>
#include <SdFat.h>

// Pins
#define SD_POWER    16 // D0
#define CS_PIN      5  // D1
#define MISO_PIN		12 // D6
#define MOSI_PIN		13 // D7
#define CLK_PIN     14 // D5
#define CD_PIN      4  // D2 - used to control sd card detection in marlin, LOW = SD inserted

// TODO get this data from sd card config.ini file
// ssid;passwd

const char* ssid = "xxxxxxxxx";
const char* password = "xxxxxxxxxx";

// Create an instance of the server
// specify the port to listen on as an argument
WiFiServer server(80);
SdFat sd;
uint32_t maxSketchSpace;

void setup() {

  Update.begin(10, U_FLASH);

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
  Serial.println();
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
  Serial.println(WiFi.localIP());

}

void loop() {
  // Check if a client has connected
  WiFiClient client = server.available();
  if (!client) {
    return;
  }
  
  // Wait until the client sends some data
  Serial.println("\nnew client");
  while(!client.available()){
    delay(1);
  }
  
  // Read the first line of the request
  String req = client.readStringUntil('\r');
  client.readStringUntil('\n');
  Serial.println(req);
  
  // Match the request
  if (req.indexOf("/api/version") != -1) {
    client.print("HTTP/1.1 200 OK\r\n\r\n{\"api\":\"0.1\",\"text\":\"OctoPrint 1.10.1\"}");
    return;
    
  } else if (req.indexOf("POST /api/files/local") != -1) {
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


    // TODO fazer a inicialização para escrita no arquivo!
    // igual ao ESPWebDAV::handlePut


    // remove card from marlin and wait a little bit
    digitalWrite(CD_PIN, HIGH);
	  delay(1000);

    takeSdBus();

    // SD init
    if (sd.begin(CS_PIN, SPI_FULL_SPEED)) { //SPI_DIV3_SPEED SPI_FULL_SPEED SPI_HALF_SPEED
      Serial.println("SD init ok");
    } else {
      Serial.println("SD init fail!!!!!");
    }  

    // receber o restante do request
    SdFile nFile;
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

    // create a contiguous file
		if (!nFile.createContiguous(sd.vwd(), fileName.c_str(), contBlocks * WRITE_BLOCK_CONST))
			return handleWriteError(client, "File create contiguous sections failed", &nFile);

		// get the location of the file's blocks
		if (!nFile.contiguousRange(&bgnBlock, &endBlock))
			return handleWriteError(client, "Unable to get contiguous range", &nFile);

		if (!sd.card()->writeStart(bgnBlock, contBlocks))
			return handleWriteError(client, "Unable to start writing contiguous range", &nFile);

    while(numRemaining > 0)	{
			size_t numToRead = (numRemaining > WRITE_BLOCK_CONST) ? WRITE_BLOCK_CONST : numRemaining;
			size_t numRead = readBytesWithTimeout(client, buf, sizeof(buf), numToRead);

			if(numRead == 0)
				break;

			if (!sd.card()->writeData(buf))
				return handleWriteError(client, "Write data failed", &nFile);

			numRemaining -= numRead;
    }

		// stop writing operation
		if (!sd.card()->writeStop())
			return handleWriteError(client, "Unable to stop writing contiguous range", &nFile);

    // truncar o arquivo no sd pra ignorar o restante do request, ex:
    // --------------------------c5ee0ac6eeaf75ad--
		if(!nFile.truncate(fileSize - numRemaining - boundaryLength - 6)) // 47 is the --------------------------c5ee0ac6eeaf75ad--
			return handleWriteError(client, "Unable to truncate the file", &nFile);

    nFile.close();

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

    return;

  } else if (req.indexOf("GET /update") != -1) {
    // retornar uma pagina html com um input de upload
    const char* serverIndex = "<form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='update'><input type='submit' value='Update'></form>";

    client.print("HTTP/1.1 200 OK\r\n");
    client.print("Content-Type:text/html\r\n\r\n");
    client.print(serverIndex);

    return;

  } else if (req.indexOf("POST /update") != -1) {
    // primeiro teste, como o upload vai se comportar?
    // printar tudo que receber pra ver como vai ser...

    // curl -F "image=@esp8266-http-to-sd.ino.generic.bin" 192.168.0.70/update

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
      Update.printError(Serial);
    }

    size_t numRemaining = fileSize;
		uint8_t buffer[2048];
    while(numRemaining > 0)	{
			size_t numToRead = (numRemaining > 2048) ? 2048 : numRemaining;
			size_t numRead = readBytesWithTimeout(client, buffer, numToRead, numToRead);

			if(numRead == 0) break;

      Serial.printf("size: %d", numRead);
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
    client.print("Location: http://192.168.0.70/update\r\n\r\n");

    ESP.restart();
    return;

  } else {
    Serial.println("invalid request");
    client.stop();
    return;
  }
  
  client.flush();

  // Prepare the response
  String s = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<!DOCTYPE HTML>\r\n<html>\r\n...</html>\n";

  // Send the response to the client
  client.print(s);
  delay(1);
  Serial.println("Client disonnected");

  // The client will actually be disconnected 
  // when the function returns and 'client' object is detroyed
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

void handleWriteError(WiFiClient client, String message, FatFile *wFile)	{
// ------------------------
	// close this file
	wFile->close();
	// delete the wrile being written
	//sd.remove(uri.c_str());
  Serial.print("Write Error: ");
  Serial.println(message);
  client.print("HTTP/1.1 400 Bad Request\r\n\r\n");
  client.flush();

  releaseSdBus();
  digitalWrite(CD_PIN, LOW);
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