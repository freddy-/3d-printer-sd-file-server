# ESP8266 SD Card File Server For 3D Printer

This project mimics the file upload endpoint of OctoPrint allowing to send gcode from prusa slicer and saving direct to SD card.

This project is based on ardyesp/ESPWebDAV and FYSETC/ESPWebDAV.
 
The SD card is shared between ESP8266 and 3d printer running marlin firmware with a custom change.

This change in Marlin allow the SDIO lines from the SD card to be shared with the ESP8266. When the card is removed from the 3D Printer Marlin detects and disable the SDIO hardware and set these pins to input.

### 3D Printer
The tests were made using a Kingroon KP3S. This printer have a MakerBase Robin Nano clone board with a connector for a external SD board.

### Firmware update
This project allow firmware update via browser throught url `<esp-ip>/update`

### Dependencies
- ESP8266 version 3.1.2