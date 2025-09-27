# Dope.Pay POS – Arduino Firmware

This folder contains reference Arduino sketches for the NFC card scan flow that powers the Dope.Pay cross‑chain POS.

## Files

- `cardScan.ino` – Minimal reader that scans a MIFARE Ultralight NFC card, prints the URL or text, and shows how to build a payload for the server’s intent execute API.
- `read_process.ino` – Helper/process flow example for reading blocks/pages and post‑processing.

## Hardware

- ESP32 DevKit (3.3V logic)
- MFRC522 RFID (SPI)
- Jumper wires, breadboard, 5V power bank

Pins (default in sketches):

- RST_PIN: 22
- SS_PIN: 21

## Quick Start

1. Open `cardScan.ino` in Arduino IDE.
2. Board: ESP32 Dev Module; Upload speed 921600; Flash freq default.
3. Wire MFRC522 to ESP32 (SS=21, RST=22) + 3.3V/GND.
4. Open Serial Monitor at 115200 baud.
5. Tap a MIFARE Ultralight/NTAG card to read.

On success you’ll see:

- Card UID
- Card type
- Pages (hex + ASCII)
- Extracted substring or NDEF text
- Example JSON payload and target URL (to send to your server)

## Intent Extraction

- URL mode: if the card stores a URL like `https://dope.cards/claim/CmfDdFr5hx`, the last path segment `CmfDdFr5hx` is treated as the intentID.
- Text mode: if the card stores a plain alphanumeric token like `CmfDdFr5hx`, the sketch extracts it as intentID directly.

## Send to Server

The sketch prints a sample payload and endpoint to call:

- Endpoint: `${VAULT_EXECUTE_URL}/api/v1/transactions/execute-intent-with-intent-id`
- Payload:
  ```json
  {
    "intentID": "CmfDdFr5hx",
    "withdrawAction": {
      "chainID": 2,
      "toAddress": "0x...",
      "tokenAddress": "0x..."
    }
  }
  ```
  You should replace `VAULT_EXECUTE_URL`, `toAddress`, and `tokenAddress` in your firmware or on the server side.

## Notes

- Use NTAG213/Ultralight tags for better compatibility.
- Keep SPI wires short; power MFRC522 with 3.3V.
- For production, validate and sign requests on the server.

## License

Apache‑2.0
