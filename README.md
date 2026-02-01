# ESP32-S3-POE-ETH-8DI-8RO Relay Controller

**[English](README_EN.md)** | Deutsch

Firmware für das Waveshare ESP32-S3-POE-ETH-8DI-8RO Modul mit Web-Interface, TCP-Steuerung und OTA-Updates.

## Features

- **DHCP (Standard)** oder statische IP-Konfiguration
- **Web-Interface** zur Konfiguration und Steuerung
- **TCP-Befehlsschnittstelle** für Relaissteuerung
- **OTA-Firmware-Updates** über das Web-Interface
- **8 Digitale Eingänge** (optokoppler-isoliert)
- **8 Relaisausgänge** mit on/off/pulse-Befehlen

## Installation

### Voraussetzungen

- [PlatformIO](https://platformio.org/) (empfohlen) oder Arduino IDE
- USB-Kabel für die erste Programmierung

### Mit PlatformIO

```bash
# Projekt klonen/kopieren
cd esp32-relay-controller

# Kompilieren
pio run

# Firmware hochladen
pio run -t upload

# Filesystem (Web-Interface) hochladen
pio run -t uploadfs

# Serial Monitor
pio device monitor
```

### Mit Arduino IDE

1. ESP32 Board Support installieren
2. Benötigte Libraries installieren:
   - ArduinoJson
   - ElegantOTA
   - AsyncTCP
   - ESPAsyncWebServer
3. Board auswählen: "ESP32S3 Dev Module"
4. `src/main.cpp` als `.ino` Datei öffnen
5. Hochladen

## Hardware Pin-Belegung

### Digitale Eingänge (DI1-DI8)
| Eingang | GPIO |
|---------|------|
| DI1     | 4    |
| DI2     | 5    |
| DI3     | 6    |
| DI4     | 7    |
| DI5     | 15   |
| DI6     | 16   |
| DI7     | 17   |
| DI8     | 18   |

### Relaisausgänge (RO1-RO8)
| Relais | GPIO |
|--------|------|
| RO1    | 33   |
| RO2    | 34   |
| RO3    | 35   |
| RO4    | 36   |
| RO5    | 37   |
| RO6    | 38   |
| RO7    | 39   |
| RO8    | 40   |

### Ethernet (W5500)
| Signal | GPIO |
|--------|------|
| MISO   | 11   |
| MOSI   | 13   |
| SCLK   | 12   |
| CS     | 10   |
| INT    | 14   |

> **Hinweis:** Die Pin-Belegung kann je nach Board-Revision variieren. Bitte überprüfen Sie die Dokumentation Ihres spezifischen Boards.

## Verwendung

### Web-Interface

Nach dem Start ist das Web-Interface unter der IP-Adresse des Geräts erreichbar:
- Bei DHCP: IP aus Router/DHCP-Server auslesen oder Serial Monitor prüfen
- Standard: `http://[IP-ADRESSE]/`

**Funktionen:**
- Relais ein-/ausschalten (Klick)
- Relais pulsen (Rechtsklick)
- Alle Relais gleichzeitig steuern
- Netzwerk-Konfiguration ändern
- TCP-Port und Impulsdauer einstellen
- System-Informationen anzeigen
- OTA-Firmware-Update

### TCP-Befehle

Verbinden Sie sich mit einem TCP-Client (z.B. `nc`, `telnet`, oder eigene Software) zum konfigurierten Port (Standard: 5000).

| Befehl | Beschreibung |
|--------|--------------|
| `r1_on` | Relais 1 einschalten (r1-r8) |
| `r1_off` | Relais 1 ausschalten (r1-r8) |
| `r1_pulse` | Relais 1 pulsen mit Standard-Dauer (r1-r8) |
| `r1_pulse_1000` | Relais 1 für 1000ms pulsen (r1-r8) |
| `all_on` | Alle Relais einschalten |
| `all_off` | Alle Relais ausschalten |
| `all_pulse` | Alle Relais pulsen mit Standard-Dauer |
| `all_pulse_500` | Alle Relais für 500ms pulsen |
| `status` | JSON-Status aller Ein-/Ausgänge |
| `help` | Hilfe anzeigen |

**Beispiele:**

```bash
# Mit netcat verbinden
nc 192.168.1.100 5000

# Relais 1 einschalten
r1_on

# Relais 3 für 1000ms pulsen
r3_pulse_1000

# Alle Relais ausschalten
all_off

# Status abfragen
status
```

### OTA-Updates

1. Web-Interface öffnen
2. "OTA Firmware Update öffnen" klicken
3. `.bin` Datei auswählen und hochladen
4. Warten bis der Upload abgeschlossen ist
5. Gerät startet automatisch neu

## Konfiguration

### Standard-Werte

| Parameter | Standard-Wert |
|-----------|---------------|
| DHCP | Aktiviert |
| Hostname | esp32-relay |
| TCP-Port | 5000 |
| Impulsdauer | 500 ms |
| Statische IP | 192.168.1.100 |
| Gateway | 192.168.1.1 |
| Subnetz | 255.255.255.0 |
| DNS | 8.8.8.8 |

### Konfiguration ändern

Alle Einstellungen können über das Web-Interface geändert werden. Die Konfiguration wird im Flash-Speicher (LittleFS) gespeichert und bleibt nach einem Neustart erhalten.

**Wichtig:** Nach Änderung der Netzwerk-Einstellungen ist ein Neustart erforderlich!

## API-Endpunkte

| Endpunkt | Methode | Beschreibung |
|----------|---------|--------------|
| `/api/status` | GET | Aktueller Status (JSON) |
| `/api/config` | GET | Aktuelle Konfiguration (JSON) |
| `/api/config` | POST | Konfiguration speichern |
| `/api/relay` | POST | Einzelnes Relais steuern |
| `/api/relays` | POST | Alle Relais steuern |
| `/api/restart` | POST | Gerät neustarten |
| `/update` | GET | OTA-Update Seite |

### API-Beispiele

```bash
# Status abfragen
curl http://192.168.1.100/api/status

# Relais 1 einschalten
curl -X POST -d "relay=1&state=on" http://192.168.1.100/api/relay

# Relais 2 für 2000ms pulsen
curl -X POST -d "relay=2&state=pulse&duration=2000" http://192.168.1.100/api/relay

# Alle Relais ausschalten
curl -X POST -d "state=off" http://192.168.1.100/api/relays

# TCP-Port ändern
curl -X POST -d "tcpPort=5001" http://192.168.1.100/api/config
```

## Troubleshooting

### Keine Ethernet-Verbindung
- Prüfen Sie das Ethernet-Kabel
- Prüfen Sie die PoE-Stromversorgung
- Serial Monitor auf Fehlermeldungen prüfen

### Web-Interface nicht erreichbar
- IP-Adresse im Serial Monitor prüfen
- Firewall-Einstellungen prüfen
- Browser-Cache leeren

### Relais reagieren nicht
- Pin-Belegung überprüfen (kann je nach Board-Version variieren)
- Spannungsversorgung der Relais prüfen

## Lizenz

MIT License

## Anpassung der Pin-Belegung

Falls Ihr Board eine andere Pin-Belegung hat, passen Sie die Arrays in `src/config.h` an:

```cpp
// Digital Inputs
const int DI_PINS[8] = { 4, 5, 6, 7, 15, 16, 17, 18 };

// Relay Outputs  
const int RO_PINS[8] = { 33, 34, 35, 36, 37, 38, 39, 40 };
```
