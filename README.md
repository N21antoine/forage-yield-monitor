# Forage Harvester Yield Monitor

A low-cost DIY yield monitor for a forage harvester, built on an Arduino Uno. It logs GPS position, ground speed and feedroll spacing to an SD card while the header is down, so you can build **relative yield maps** after harvest.

Feedroll spacing is used as a proxy for crop flow. The raw sensor value is logged and no conversion to mm or tonnes is done, so maps show relative yield (higher or lower than the rest of the field), not absolute yield.

## Hardware

| Part | Purpose |
|------|---------|
| Arduino Uno | Controller |
| u-blox NEO-6M GPS module (GY-GPS6MV2) + antenna | Position, speed, date/time |
| MicroSD SPI module (3V3) | Data logging |
| Hall effect rotary position sensor (3-wire, analog) | Feedroll spacing |
| LJ12A3-4-Z/BX NPN NO proximity switch, 4 mm | Header up/down detection |
| DC-DC converter, 12 V to 5 V | Powers everything from the machine |
| Weatherproof enclosure, screw-terminal proto shield | Mounting and wiring |

## Wiring

### Power
| From | To |
|------|----|
| Machine 12 V+ | DC-DC IN+ |
| Machine GND | DC-DC IN- |
| DC-DC OUT+ (set to exactly 5 V) | Arduino 5V |
| DC-DC OUT- | Arduino GND |

Set and verify the DC-DC output with a multimeter **before** connecting the Arduino.

### NEO-6M GPS
| GPS pin | Arduino |
|---------|---------|
| VCC | 5V |
| GND | GND |
| TX | D2 |
| RX | D3 |

### MicroSD module
| SD pin | Arduino |
|--------|---------|
| 3V3 | 3.3V |
| GND | GND |
| MOSI | D11 |
| MISO | D12 |
| CLK | D13 |
| CS | D10 |

### Hall effect position sensor
The wires are all black, so identify them with a multimeter. Power it with 5 V and GND, and the signal wire should swing between roughly 0.03 V and 4.9 V as the arm moves.

| Sensor wire | Arduino |
|-------------|---------|
| Power | 5V |
| Ground | GND |
| Signal | A0 |

### Proximity switch (NPN, NO, 5 V)
| Wire | Arduino |
|------|---------|
| Brown | 5V |
| Blue | GND |
| Black | D4 |

An NPN switch pulls the signal to GND when metal is detected, so it works directly with `INPUT_PULLUP`. No resistors are needed.

### Pin summary
| Pin | Use |
|-----|-----|
| D2 / D3 | GPS (SoftwareSerial RX / TX) |
| D4 | Proximity switch |
| D10-D13 | SD card (SPI) |
| A0 | Hall sensor |

## Software

1. Install the **TinyGPSPlus** library (Mikal Hart) from the Arduino Library Manager. `SoftwareSerial`, `SD` and `SPI` are built in.
2. Select **Arduino Uno** under Tools > Board.
3. Open `yield_monitor/yield_monitor.ino` and upload it.
4. Open Serial Monitor at 9600 baud to watch live output.

### Settings (top of the sketch)
| Setting | Default | Notes |
|---------|---------|-------|
| `UTC_OFFSET_HOURS` | `-4` | EDT. Change to `-5` for EST in winter. |
| `LOG_INTERVAL_MS` | `1000` | 1 Hz. At 5-10 km/h that is about one sample every 1.4-2.8 m. |

## How it works

- On startup the SD card is initialized and a new file is created (`LOG001.CSV`, `LOG002.CSV`, ...). Each power cycle gets its own file.
- The GPS then starts and searches for satellites. The first fix can take a few minutes outdoors.
- Every second the sketch checks the proximity switch. If the header is down, it writes one CSV row. If the header is up, it only prints status to Serial.
- The file is closed after every write, so abrupt power loss does not corrupt the log.

## CSV format

```
date,time(EDT),latitude,longitude,speed_kmh,hall_raw,gps_valid
```

| Column | Description |
|--------|-------------|
| `date` | GPS date, `YYYY-MM-DD` |
| `time(EDT)` | Local time, `HH:MM:SS` (GPS UTC time plus the offset) |
| `latitude`, `longitude` | Decimal degrees, 6 decimals |
| `speed_kmh` | GPS ground speed |
| `hall_raw` | Raw ADC value (0-1023) from the feedroll sensor |
| `gps_valid` | `1` = valid fix, `0` = no fix (coordinates are `0.000000`, filter these rows out) |

## Lessons learned

- **Initialize the SD card before starting SoftwareSerial.** With the GPS running first, `SD.begin()` failed. Calling `SD.begin()` first and `gpsSerial.begin()` after fixed it.
- **Try a different SD card early.** Two cards were formatted and tested before one finally worked. The card was the problem, not the module.
- **Use SD cards of 32 GB or less, formatted FAT32.** The Arduino SD library is unreliable with larger cards.
- **Cheap GPS shields can ship with the wrong chip.** One shield sold as a NEO-6M had an ST1612U that output no NMEA data at any baud rate. A separate NEO-6M module plus an SD module worked immediately.
- **Industrial proximity switches are usually PNP and 6-36 V.** A PNP switch on 12 V needs a voltage divider before an Arduino pin. An NPN switch powered at 5 V connects directly.
- **The antenna goes ceramic side up.**

## Limitations

- GPS accuracy of the NEO-6M is about 2.5 m, and it updates at 1 Hz.
- There is no real-time clock, so file names are numbered rather than dated.
- Yield is relative only. Calibrating against actual harvested weight would be needed for absolute values.

## Ideas for later

- Sampling rate proportional to ground speed, for constant samples per distance
- Date in the filename once the GPS has a fix
- Script to turn the CSV into a yield map (QGIS or Python)

## License

Add your preferred license here (MIT is a common choice).
