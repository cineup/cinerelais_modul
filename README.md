# CineRelais Modul — ESP32-S3 Relay Controller

**[English](README_EN.md)** | Deutsch

Firmware für das Waveshare ESP32-S3-ETH-8DI-8RO / ESP32-S3-POE-ETH-8DI-8RO Modul mit Web-Interface, TCP-Steuerung, WiFi-Support und OTA-Updates.

## Features

- **Ethernet (W5500)** mit DHCP oder statischer IP
- **WiFi** — AP-Modus für Erstkonfiguration, optionaler STA-Modus
- **Web-Interface** zur Konfiguration und Steuerung
- **TCP-Befehlsschnittstelle** für Relaissteuerung
- **OTA-Firmware-Updates** über das Web-Interface
- **8 Digitale Eingänge** (optokoppler-isoliert)
- **8 Relaisausgänge** via TCA9554 I2C-Expander
- **RGB Status-LED** (WS2812) mit Netzwerk- und Aktivitätsanzeige
- **NTP-Zeitsynchronisation** mit konfigurierbarem Server
- **Befehlsprotokoll** (letzte 50 Befehle mit Zeitstempel)

## Installation

### Voraussetzungen

- [PlatformIO](https://platformio.org/) (empfohlen) oder Arduino IDE
- USB-C-Kabel für die erste Programmierung

### Mit PlatformIO

```bash
# Kompilieren
pio run

# Firmware hochladen
pio run -t upload

# Filesystem (Web-Interface) hochladen
pio run -t uploadfs

# Serial Monitor (115200 Baud)
pio device monitor
```

### Mit Arduino IDE

1. ESP32 Board Support (v3.x) installieren
2. Benötigte Libraries installieren:
   - ArduinoJson (^7.0.0)
   - ElegantOTA (^3.1.0)
   - AsyncTCP (^1.1.1)
   - ESPAsyncWebServer (^1.2.3)
3. Board auswählen: "ESP32S3 Dev Module"
4. `src/main.cpp` als `.ino` Datei öffnen
5. Hochladen

## Hardware

### Pin-Belegung

#### Digitale Eingänge (DI1-DI8)
Optokoppler-isoliert, Active LOW mit internem Pull-up.

| Eingang | GPIO |
|---------|------|
| DI1     | 4    |
| DI2     | 5    |
| DI3     | 6    |
| DI4     | 7    |
| DI5     | 8    |
| DI6     | 9    |
| DI7     | 10   |
| DI8     | 11   |

#### Relaisausgänge (RO1-RO8)
Gesteuert via **TCA9554 I2C I/O-Expander** (Adresse 0x20).

| I2C | GPIO |
|-----|------|
| SDA | 42   |
| SCL | 41   |

Die 8 Relais (RO1-RO8) werden über die TCA9554-Pins P0-P7 gesteuert.

#### Ethernet (W5500 SPI)
| Signal | GPIO |
|--------|------|
| SCLK   | 15   |
| MOSI   | 13   |
| MISO   | 14   |
| CS     | 16   |
| INT    | 12   |

#### Weitere Peripherie
| Funktion   | GPIO |
|------------|------|
| RS485 TX   | 17   |
| RS485 RX   | 18   |
| RGB LED    | 38   |
| Buzzer     | 46   |

## Verwendung

### Erstinbetriebnahme

1. Firmware und Filesystem hochladen
2. Das Modul startet einen WiFi Access Point:
   - SSID: `cinerelais1` (= Hostname)
   - Passwort: offen (kein Passwort)
3. Mit dem AP verbinden und `http://192.168.4.1` aufrufen
4. Im Web-Interface Ethernet/WiFi konfigurieren
5. Gerät neustarten

### Web-Interface

Nach dem Start ist das Web-Interface erreichbar:
- **Ethernet**: IP aus DHCP oder konfigurierte statische IP
- **WiFi AP**: `http://192.168.4.1`
- **WiFi STA**: IP aus DHCP oder konfigurierte statische IP

**Funktionen:**
- Relais ein-/ausschalten (Klick)
- Relais pulsen (Rechtsklick)
- Alle Relais gleichzeitig steuern
- Ethernet-Konfiguration (DHCP/statische IP)
- WiFi-Konfiguration (AP/STA, DHCP/statische IP)
- TCP-Port und Impulsdauer einstellen
- System-Informationen (Ethernet/WiFi Status, TCA9554)
- OTA-Firmware-Update

### TCP-Befehle

Verbinden Sie sich mit einem TCP-Client (z.B. `nc`, `telnet`) zum konfigurierten Port (Standard: 5000).

| Befehl | Beschreibung |
|--------|--------------|
| `r1_on` ... `r8_on` | Relais einschalten |
| `r1_off` ... `r8_off` | Relais ausschalten |
| `r1_pulse` | Relais pulsen (Standard-Dauer) |
| `r1_pulse_1000` | Relais für 1000ms pulsen |
| `all_on` | Alle Relais einschalten |
| `all_off` | Alle Relais ausschalten |
| `all_pulse` | Alle Relais pulsen |
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
4. Gerät startet automatisch neu

### Status-LED

Die RGB-LED (WS2812 auf GPIO 38) zeigt verschiedene Zustände an:

| Farbe | Muster | Bedeutung |
|-------|--------|-----------|
| Grün | Dauerlicht | Ethernet verbunden |
| Cyan | Dauerlicht | WiFi STA verbunden |
| Blau | Pulsierend | Nur WiFi AP aktiv |
| Rot | Schnell blinkend | Keine Netzwerkverbindung |
| Orange | Kurzer Blitz | Befehl empfangen |
| Gelb | Kurzer Blitz | Relais-Aktivität |
| Lila | Pulsierend | OTA-Update läuft |

Die LED-Helligkeit ist im Web-Interface einstellbar (Standard: 20%).

## Konfiguration

### Standard-Werte

| Parameter | Standard-Wert |
|-----------|---------------|
| Hostname | cinerelais1 |
| DHCP (Ethernet) | Aktiviert |
| WiFi | Aktiviert |
| WiFi AP | Aktiviert |
| WiFi AP Passwort | (offen) |
| TCP-Port | 5000 |
| Impulsdauer | 500 ms |
| Statische IP | 192.168.1.100 |
| NTP | Aktiviert |
| NTP-Server | pool.ntp.org |
| Zeitzone | CET-1CEST (Europa/Berlin) |
| LED | Aktiviert |
| LED-Helligkeit | 20% |

### Konfiguration ändern

Alle Einstellungen können über das Web-Interface geändert werden. Die Konfiguration wird im Flash-Speicher (LittleFS) als `/config.json` gespeichert.

**Wichtig:** Nach Änderung der Netzwerk-Einstellungen ist ein Neustart erforderlich!

## API-Endpunkte

| Endpunkt | Methode | Beschreibung |
|----------|---------|--------------|
| `/api/status` | GET | Aktueller Status (JSON) |
| `/api/config` | GET | Aktuelle Konfiguration (JSON) |
| `/api/config` | POST | Konfiguration speichern |
| `/api/relay` | POST | Einzelnes Relais steuern |
| `/api/relays` | POST | Alle Relais steuern |
| `/api/log` | GET | Befehlsprotokoll (letzte 50 Einträge) |
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

# WiFi SSID konfigurieren
curl -X POST -d "wifiSSID=MeinWLAN&wifiPassword=geheim" http://192.168.1.100/api/config
```

## Troubleshooting

### TCA9554 nicht gefunden
- I2C-Verbindung prüfen (SDA=GPIO42, SCL=GPIO41)
- Serial Monitor auf "TCA9554 found at 0x20" prüfen
- Falls nicht gefunden: Board-Hardware prüfen

### Keine Ethernet-Verbindung
- Ethernet-Kabel prüfen
- PoE-Stromversorgung prüfen (falls POE-Version)
- Serial Monitor auf "ETH Got IP" prüfen

### WiFi AP nicht sichtbar
- Prüfen ob WiFi in Konfiguration aktiviert ist
- Prüfen ob WiFi AP aktiviert ist
- Hostname = SSID des Access Points

### Web-Interface nicht erreichbar
- IP-Adresse im Serial Monitor prüfen
- Bei WiFi AP: `192.168.4.1` verwenden
- Browser-Cache leeren

## Lizenz

MIT License
