# eHaJo Tennis ESP32-C3 Binaries

Diese Dateien sind fertig kompilierte Firmware-Images fuer ESP32-C3-Module.
Sie koennen ohne Arduino-Kompilierung direkt geflasht werden.

## Dateien

- `controller-tennis/controller-tennis-v1.0-esp32c3-merged.bin`
  - Firmware fuer den Tennis-Controller
- `machine-tennis/machine-tennis-v1.0-esp32c3-merged.bin`
  - Firmware fuer die Arcade Machine mit Tennis-Spiel

## Flashen unter Windows

1. ESP32-C3 per USB anschliessen.
2. Den COM-Port im Windows-Geraetemanager oder mit Arduino CLI ermitteln.
3. Einen der Batch-Helper starten:

```bat
upload-controller.bat COM20
upload-machine.bat COM22
```

Die Images sind als `merged.bin` gebaut und werden ab Adresse `0x0`
geschrieben. Dadurch sind Bootloader, Partitionstabelle und Firmware in
einer Datei enthalten.

## Voraussetzungen

Die Batch-Dateien verwenden `esptool.exe` aus einer installierten
ESP32-Arduino-Core-Installation:

```text
%LOCALAPPDATA%\Arduino15\packages\esp32\tools\esptool_py\5.1.0\esptool.exe
```

Falls dieser Pfad auf einem Kundenrechner nicht existiert, kann dieselbe
`merged.bin` auch mit einem anderen ESP32-C3-kompatiblen Flash-Tool ab
Adresse `0x0` geschrieben werden.
