/*
  ============================================================
  Forage Harvester Yield Monitor
  ============================================================
  Hardware:
    - Arduino Uno
    - NEO-6M GPS module on D2/D3 (SoftwareSerial)
    - MicroSD module on SPI (D10 CS, D11 MOSI, D12 MISO, D13 CLK)
    - Hall effect position sensor on A0 (feedroll spacing)
    - NPN NO proximity switch on D4 (header up/down)

  How it works:
    - On startup, the SD card is initialized and a new CSV log
      file is created (LOG001.CSV, LOG002.CSV, etc.)
    - The GPS is then started and begins searching for satellites
    - Every second, the code checks if the header is down
      (proximity switch triggered)
    - If header is down: reads GPS and Hall sensor, writes a
      line to the CSV file
    - If header is up: pauses logging, prints status to Serial

  Log format (CSV):
    date, time(EDT), latitude, longitude, speed_kmh, hall_raw, gps_valid

  Libraries required (install via Arduino Library Manager):
    - TinyGPS++ by Mikal Hart
    - SoftwareSerial (built-in)
    - SD (built-in)
  ============================================================
*/

#include <SoftwareSerial.h>   // Allows serial communication on other pins than D0/D1
#include <TinyGPSPlus.h>      // Parses NMEA sentences from the GPS module
#include <SD.h>               // Reads and writes files on the SD card
#include <SPI.h>              // SPI bus communication (used by SD card)

// ---------- Pin definitions ----------
// These define which Arduino pin each component is connected to
#define GPS_RX_PIN      2     // Arduino receives GPS data on this pin (connect to GPS TX)
#define GPS_TX_PIN      3     // Arduino sends data to GPS on this pin (connect to GPS RX)
#define HEADER_PIN      4     // Proximity switch signal wire (NPN NO, pulled LOW when header down)
#define HALL_PIN        A0    // Hall effect sensor analog output (0–1023, higher = more crop)
#define SD_CS_PIN       10    // SD card chip select pin (SPI)

// ---------- Time zone ----------
// The GPS outputs UTC time. This offset converts it to local time.
// EDT = UTC-4 (summer), EST = UTC-5 (winter)
#define UTC_OFFSET_HOURS  -4

// ---------- Logging interval ----------
// How often a data point is written to the SD card (in milliseconds)
// 1000ms = 1 second = ~1 sample per 1.4–2.8m at harvesting speed
#define LOG_INTERVAL_MS  1000

// ---------- Objects ----------
// SoftwareSerial: creates a serial connection on D2/D3 for the GPS
// since D0/D1 are used by USB programming
SoftwareSerial gpsSerial(GPS_RX_PIN, GPS_TX_PIN);

// TinyGPSPlus: parses the raw NMEA sentences from the GPS into
// usable values like latitude, longitude, speed, date, time
TinyGPSPlus gps;

// File: represents the open log file on the SD card
File logFile;

// ---------- State variables ----------
// Tracks the last time a log entry was written (used for 1Hz timing)
unsigned long lastLogTime = 0;

// Stores the current log filename (e.g. "LOG001.CSV")
String fileName = "";

// -------------------------------------------------------
void setup() {
  // Start serial communication with the computer (for Serial Monitor debugging)
  Serial.begin(9600);

  // Configure the header pin as input with internal pull-up resistor
  // The NPN switch pulls this pin LOW when metal (header) is detected
  // When header is UP (no metal): pin reads HIGH
  // When header is DOWN (metal detected): pin reads LOW
  pinMode(HEADER_PIN, INPUT_PULLUP);

  Serial.println(F("Yield Monitor starting..."));

  // --- Initialize SD card FIRST ---
  // IMPORTANT: SD.begin() must be called before gpsSerial.begin()
  // SoftwareSerial disables interrupts while receiving data, which
  // can corrupt SPI communication with the SD card if started first
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println(F("ERROR: SD card init failed!"));
    // Halt the program — no point continuing without SD card
    while (true);
  }
  Serial.println(F("SD card OK."));

  // --- Generate a unique filename ---
  // Count up from LOG001.CSV until we find a name that doesn't exist yet
  // This ensures each power cycle creates a fresh log file
  for (int i = 1; i <= 999; i++) {
    fileName = "LOG";
    if (i < 100) fileName += "0";  // Pad to 3 digits (e.g. LOG001)
    if (i < 10)  fileName += "0";
    fileName += String(i) + ".CSV";
    if (!SD.exists(fileName)) break;  // Stop when we find an unused name
  }

  // --- Write CSV header row ---
  // Open the file, write the column names, then close it
  logFile = SD.open(fileName, FILE_WRITE);
  if (logFile) {
    logFile.println(F("date,time(EDT),latitude,longitude,speed_kmh,hall_raw,gps_valid"));
    logFile.close();
    Serial.print(F("Logging to: "));
    Serial.println(fileName);
  } else {
    Serial.println(F("ERROR: Could not create log file!"));
    while (true);
  }

  // --- Start GPS AFTER SD is initialized ---
  // See note above about SoftwareSerial/SPI conflict
  gpsSerial.begin(9600);
  Serial.println(F("Waiting for GPS fix..."));
}

// -------------------------------------------------------
void loop() {
  // --- Feed GPS data into TinyGPS++ ---
  // The GPS continuously sends NMEA sentences over serial
  // We read each character and pass it to gps.encode() which
  // parses it in the background. Values like gps.location.lat()
  // are updated automatically as complete sentences arrive.
  while (gpsSerial.available()) {
    gps.encode(gpsSerial.read());
  }

  // --- Check if it's time to log (every 1 second) ---
  unsigned long now = millis();  // millis() = milliseconds since Arduino powered on
  if (now - lastLogTime >= LOG_INTERVAL_MS) {
    lastLogTime = now;  // Reset the timer

    // --- Read the header proximity switch ---
    // NPN NO switch: LOW = metal detected = header is DOWN = we should log
    bool headerDown = (digitalRead(HEADER_PIN) == LOW);

    // --- Read the Hall effect sensor ---
    // analogRead() returns 0–1023 proportional to voltage (0–5V)
    // Higher value = feedrolls more open = more crop = higher yield
    int hallRaw = analogRead(HALL_PIN);

    // --- Only log when header is down ---
    if (headerDown) {
      // Open the log file in append mode (FILE_WRITE appends to existing file)
      logFile = SD.open(fileName, FILE_WRITE);
      if (logFile) {

        // --- Write date (YYYY-MM-DD) ---
        if (gps.date.isValid()) {
          char dateBuf[11];
          sprintf(dateBuf, "%04d-%02d-%02d", gps.date.year(), gps.date.month(), gps.date.day());
          logFile.print(dateBuf);
        } else {
          logFile.print(F("NO_DATE"));  // GPS hasn't received date yet
        }
        logFile.print(F(","));

        // --- Write time (HH:MM:SS) in local time (EDT) ---
        if (gps.time.isValid()) {
          // Apply UTC offset to convert from GPS time (UTC) to local time
          int hour = gps.time.hour() + UTC_OFFSET_HOURS;
          // Handle day boundary wraparound (e.g. UTC 01:00 - 4 = previous day 21:00)
          if (hour < 0) hour += 24;
          if (hour > 23) hour -= 24;
          char timeBuf[9];
          sprintf(timeBuf, "%02d:%02d:%02d", hour, gps.time.minute(), gps.time.second());
          logFile.print(timeBuf);
        } else {
          logFile.print(F("NO_TIME"));
        }
        logFile.print(F(","));

        // --- Write latitude (decimal degrees, 6 decimal places) ---
        if (gps.location.isValid()) {
          logFile.print(gps.location.lat(), 6);
        } else {
          logFile.print(F("0.000000"));  // No fix yet
        }
        logFile.print(F(","));

        // --- Write longitude (decimal degrees, 6 decimal places) ---
        if (gps.location.isValid()) {
          logFile.print(gps.location.lng(), 6);
        } else {
          logFile.print(F("0.000000"));
        }
        logFile.print(F(","));

        // --- Write ground speed (km/h) ---
        if (gps.speed.isValid()) {
          logFile.print(gps.speed.kmph(), 1);  // 1 decimal place
        } else {
          logFile.print(F("0.0"));
        }
        logFile.print(F(","));

        // --- Write Hall sensor raw value (0–1023) ---
        // This is the relative yield indicator — higher = more crop
        // No conversion needed since we're using relative yield
        logFile.print(hallRaw);
        logFile.print(F(","));

        // --- Write GPS valid flag ---
        // 1 = GPS had a valid fix, coordinates are trustworthy
        // 0 = no fix, coordinates are 0.000000 and should be ignored
        logFile.println(gps.location.isValid() ? F("1") : F("0"));

        // Close the file after each write to prevent data loss
        // if power is cut unexpectedly
        logFile.close();

        // --- Print log entry to Serial Monitor for debugging ---
        Serial.print(F("LOG | Hall: "));
        Serial.print(hallRaw);
        Serial.print(F(" | GPS: "));
        Serial.print(gps.location.isValid() ? F("OK") : F("NO FIX"));
        if (gps.location.isValid()) {
          Serial.print(F(" | "));
          Serial.print(gps.location.lat(), 6);
          Serial.print(F(","));
          Serial.print(gps.location.lng(), 6);
          Serial.print(F(" | "));
          Serial.print(gps.speed.kmph(), 1);
          Serial.print(F(" km/h"));
        }
        Serial.println();

      } else {
        // File failed to open — SD card may have been removed
        Serial.println(F("ERROR: Could not open log file!"));
      }

    } else {
      // --- Header is up — print status but don't log ---
      Serial.print(F("PAUSED (header up) | Hall: "));
      Serial.print(hallRaw);
      Serial.print(F(" | GPS sats: "));
      Serial.println(gps.satellites.value());  // Show satellite count while waiting
    }
  }
}