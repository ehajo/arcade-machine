# AGENTS.md – eHaJo Arcade Machine und Controller-Tennis

## Projektkontext

Dieses Repository enthält Hard- und Software für die eHaJo Arcade Machine sowie den zugehörigen Tennis-Controller.

Wichtige Bereiche:

- `Machine/Software` – Software für die Arcade Machine
- `Controller-Tennis/Software` – Software für den separaten Tennis-Controller
- `Controller-Tennis/CAD` – mechanische/CAD-Daten für den Controller
- `Controller-Tennis/PCB` – Platinen-/PCB-Daten für den Controller

Die Machine und der Controller gehören zusammen. Änderungen an Protokoll, Kommunikation, Pinbelegung oder Spielsteuerung müssen immer auf beiden Seiten betrachtet werden.

## Aktuelle Software-Struktur

### Machine/Software

Enthält mehrere Arduino-Projekte:

- `Machine/Software/alien`
- `Machine/Software/elektronik-attack`
- `Machine/Software/tennis`

Der Tennis-Sketch der Machine enthält unter anderem:

- `tennis.ino`
- `ehajo_master.h`
- `ehajo_protocol.h`
- `ehajo_bw.bmp`

### Controller-Tennis/Software

Enthält die Controller-Software:

- `Software.ino`
- `ehajo_protocol.h`

Der Controller kommuniziert mit der Machine. Änderungen am Protokoll müssen mit der Machine-Seite kompatibel bleiben.

## Zielhardware

Die genaue Hardware kann je nach Unterprojekt leicht abweichen. Typische Komponenten:

- ESP32-C3 SuperMini oder kompatibler ESP32-C3
- OLED-Display, häufig SPI
- WS2812 / NeoPixel LEDs
- Buttons / Joystick / Eingabeelemente
- passiver Buzzer
- ggf. ESP-NOW oder anderes proprietäres Kommunikationsprotokoll
- eHaJo-spezifisches Protokoll in `ehajo_protocol.h`

## Sehr wichtige Regeln

1. Bestehende Funktionen nicht entfernen.
2. Bestehende Pinbelegungen nicht ungefragt ändern.
3. Bestehende Sounds, Animationen, Spielmechaniken und Display-Ausgaben erhalten.
4. Bei Änderungen am Controller immer prüfen, ob die Machine-Seite ebenfalls angepasst werden muss.
5. Bei Änderungen an der Machine immer prüfen, ob der Controller weiterhin kompatibel ist.
6. Änderungen an `ehajo_protocol.h` müssen auf beiden Seiten synchron betrachtet werden.
7. Keine neuen Bibliotheken einbauen, außer es ist wirklich nötig.
8. Wenn neue Bibliotheken nötig sind, den genauen Namen für den Arduino Library Manager nennen.
9. Code soll mit der Arduino IDE kompilierbar bleiben.
10. Keine unnötig komplexe C++-Architektur einführen.
11. Nur die ausdrücklich gewünschte Änderung vornehmen.
12. Keine generierten Build-Dateien verändern oder einchecken.

## Kommunikations- und Protokollregeln

Die Dateien mit dem Namen `ehajo_protocol.h` sind besonders wichtig.

Vor Änderungen an Protokoll, Paketstruktur, Message-Typen, Pairing, MAC-Adressen, Checksummen oder Button-Kommandos:

1. Beide Seiten prüfen:
   - `Machine/Software/tennis/ehajo_protocol.h`
   - `Controller-Tennis/Software/ehajo_protocol.h`

2. Prüfen, ob beide Dateien identisch sein sollten.

3. Falls eine Änderung nötig ist, beide Seiten konsistent ändern.

4. In der Zusammenfassung ausdrücklich erwähnen:
   - welche Protokolldateien geändert wurden
   - ob Machine und Controller kompatibel bleiben
   - ob ein gemeinsamer Header sinnvoll wäre

## Coding-Stil

- Arduino/C++
- Deutsche Kommentare sind erwünscht.
- Konstanten und Pinbelegungen sollen oben im Sketch gut sichtbar bleiben.
- Keine unnötige dynamische Speicherverwaltung.
- Keine großen Umbauten ohne ausdrücklichen Auftrag.
- Lieber verständlich als besonders clever.
- Vollständige, kopierbare Dateien sind bevorzugt.
- Bestehende Includes und Header-Struktur nicht ohne Grund ändern.

## Projektziel

Die Software ist für echte Hardware und für einen eHaJo-Bausatz gedacht.

Wichtig sind:

- einfache Wartbarkeit
- robuste Funktion
- nachvollziehbarer Code
- Retro-Arcade-Feeling
- eHaJo-Branding
- zuverlässige Kommunikation zwischen Controller und Machine
- kindertauglicher / workshop-tauglicher Aufbau

## Typische Aufgaben

Typische Änderungswünsche:

- Compilerfehler beheben
- Display um 180 Grad drehen
- eHaJo-Bootscreen ergänzen
- WS2812-Helligkeit ändern
- Soundeffekte ergänzen oder reparieren
- Ballphysik im Tennis verbessern
- Controller-Eingaben zuverlässiger machen
- Pairing / Verbindung zwischen Controller und Machine verbessern
- Protokoll zwischen Controller und Machine anpassen
- Alien-Schüsse sichtbarer machen
- Level-100-Endanimation verbessern
- OLED-Ausgabe optimieren
- Code für Arduino IDE kompilierbar machen

## Build- und Testregeln

Wenn möglich, nach Änderungen einen Compile-Test durchführen.

Falls kein Compile-Test möglich ist:

- Code statisch prüfen
- offensichtliche fehlende Includes, Typdefinitionen und Funktionsprototypen korrigieren
- am Ende klar sagen, dass kein echter Compile-Test durchgeführt wurde

Bei Änderungen an der Controller-Kommunikation müssen beide Seiten mindestens statisch geprüft werden:

- Machine-Seite
- Controller-Seite

## Umgang mit Compilerfehlern

Wenn ein Compilerfehler gegeben wird:

1. Fehlerstelle suchen
2. Ursache erklären
3. nur nötige Stellen korrigieren
4. keine vorhandenen Funktionen entfernen
5. prüfen, ob dieselbe Änderung auch im anderen Softwareteil nötig ist
6. am Ende kurz erklären, welche Datei geändert wurde

## Git-Regeln

- Keine automatischen Commits ohne ausdrücklichen Auftrag.
- Keine generierten Build-Dateien einchecken.
- Keine `.bin`, `.elf`, `.map`, `.hex`, `.o`, `.a` oder temporären Build-Dateien einchecken.
- Ordner namens `build` sollen normalerweise ignoriert werden.
- Vor größeren Umbauten erst die betroffenen Dateien nennen.