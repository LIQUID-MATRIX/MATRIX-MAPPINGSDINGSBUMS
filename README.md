MatrixChat ESP32 – Mini Chat Display

Ein eigenständiges Chat-Display auf Basis eines ESP32, das Nachrichten aus Matrix-Räumen in Echtzeit visualisiert. Es verbindet sich via WLAN direkt mit einem Matrix-Homeserver und benötigt keine zusätzliche App oder Cloud-Infrastruktur.
🛠 Hardware & Komponenten

    Mikrocontroller: ESP32 Dev Module (240 MHz, 520 KB SRAM).

    Display: SSD1306 OLED (128x64 Pixel, I2C).

    Eingabe: Rotary Encoder mit Taster (z. B. KY-040).

    Wichtig: Ein 10 kΩ Widerstand als externer Pull-up für den Encoder-Taster (GPIO34) ist zwingend erforderlich.

Pin-Belegung (ESP32)
Signal	GPIO	Beschreibung
OLED SDA	21	

I2C Datenleitung
OLED SCL	22	

I2C Taktleitung
Encoder CLK	32	

Takt-Signal (interner Pull-up)
Encoder DT	33	

Daten-Signal (interner Pull-up)
Encoder SW	34	

Taster (Externer 10 kΩ Pull-up nach 3,3V nötig!)
💻 Software-Installation
1. Arduino IDE vorbereiten

    Boardverwalter-URL für ESP32 hinzufügen.

    Espressif Systems über den Boardverwalter installieren.

    Board auswählen: ESP32 Dev Module.

2. Benötigte Bibliotheken

Installiere folgende Bibliotheken über den Library Manager:

    Adafruit SSD1306 (installiert Adafruit GFX automatisch mit).

    ArduinoJson (Version 7.x verwenden).

⚙️ Konfiguration

Passe die Konstanten am Anfang der MatrixChat.ino an:
C++

// WLAN
const char* WIFI_SSID = "Dein-WLAN-Name"; [cite: 75]
const char* WIFI_PASS = "Dein-WLAN-Passwort"; [cite: 76]

// Matrix
const char* MATRIX_HOST = "matrix.org"; // Ohne https:// [cite: 78]
const char* MATRIX_USER = "@benutzername:matrix.org"; // Vollständige MXID [cite: 81]
const char* MATRIX_PASS = "dein_passwort"; [cite: 82]
const char* MATRIX_ROOM = "!AbCdEfGhIj:matrix.org"; // Interne Raum-ID [cite: 84]

    Hinweis zur Raum-ID: Nutze in Element die Raum-Einstellungen -> Erweitert, um die interne ID (beginnend mit !) zu kopieren.

🕹 Bedienung

    Drehen (CW/Rechts): Scrollt rückwärts zu älteren Nachrichten.

    Drehen (CCW/Links): Scrollt vorwärts zu neueren Nachrichten.

    Taster drücken: Springt sofort zur aktuellsten Nachricht.

🔬 Technische Details & Inversion
Wie das System scheitern könnte (Inversion)

    GPIO34 Fehler: Ohne den 10 kΩ Pull-up-Widerstand wird der Taster aufgrund fehlender interner Pull-ups am ESP32 nicht zuverlässig auslösen.

    Speichermangel: Da Matrix-Sync-Antworten mehrere Megabyte groß sein können, nutzt das Projekt einen leichtgewichtigen String-Parser statt ArduinoJson für den /sync, um einen Stack-Overflow (TooDeep-Fehler) zu verhindern.

Folgen der Architektur (Second-Order Thinking)

Durch die Nutzung von HTTP Long-Polling (10s Intervall) wird eine nahezu Echtzeit-Erfahrung bei minimalem Ressourcenverbrauch erreicht. Die Reduktion der Datenlast erfolgt durch einen spezifischen Sync-Filter, der nur notwendige m.room.message-Events anfordert.
🛠 Fehlerbehebung

    Keine Nachrichten? Stelle sicher, dass der Account Mitglied im Raum ist und sende eine Testnachricht via Element.

    Login Fehler? Prüfe, ob die MATRIX_USER ID das Format @user:server.org hat.

    OLED bleibt schwarz? I2C-Adresse (0x3C oder 0x3D) im Code prüfen.
