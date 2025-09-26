//this script can read URL and ssend the intent to the vault-api for preocessing


#include <SPI.h>
#include <MFRC522.h>

#define RST_PIN 22
#define SS_PIN  21

MFRC522 mfrc522(SS_PIN, RST_PIN);

void printHexAndAscii(byte *buffer, byte length) {
  Serial.print("HEX: ");
  for (byte i = 0; i < length; i++) {
    if (buffer[i] < 0x10) Serial.print("0");
    Serial.print(buffer[i], HEX);
    Serial.print(" ");
  }
  Serial.print(" | ASCII: ");
  for (byte i = 0; i < length; i++) {
    if (buffer[i] >= 32 && buffer[i] <= 126) {
      Serial.print((char)buffer[i]);  // printable characters
    } else {
      Serial.print(".");
    }
  }
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  SPI.begin();
  mfrc522.PCD_Init();
  
  Serial.println("=== MIFARE Ultralight Reader ===");
  Serial.println("Place a MIFARE Ultralight card to read data...");
  Serial.println("----------------------------------------");
}

void loop() {
  if (!mfrc522.PICC_IsNewCardPresent()) return;
  if (!mfrc522.PICC_ReadCardSerial()) return;

  Serial.print("\nCard UID: ");
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    if (mfrc522.uid.uidByte[i] < 0x10) Serial.print("0");
    Serial.print(mfrc522.uid.uidByte[i], HEX);
    Serial.print(" ");
  }
  Serial.println();

  MFRC522::PICC_Type type = mfrc522.PICC_GetType(mfrc522.uid.sak);
  Serial.print("Card Type: ");
  Serial.println(mfrc522.PICC_GetTypeName(type));

  if (type == MFRC522::PICC_TYPE_MIFARE_UL) {
    readMifareUltralight();
  } else {
    Serial.println("This is not a MIFARE Ultralight card. Please use the appropriate reader.");
  }

  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
  
  // Wait for card to be removed
  Serial.println("Remove card to continue...");
  while (mfrc522.PICC_IsNewCardPresent()) {
    delay(100);
  }
  delay(1000);
  Serial.println("Ready for next card.");
  Serial.println("----------------------------------------");
}

void readMifareUltralight() {
  Serial.println("MIFARE Ultralight detected. Reading all pages...");
  
  // Read pages 0-12 only
  String allTextData = "";
  bool foundTextData = false;
  
  for (byte page = 0; page <= 12; page++) {
    byte buffer[18];
    byte size = sizeof(buffer);
    MFRC522::StatusCode status = mfrc522.MIFARE_Read(page, buffer, &size);
    
    if (status == MFRC522::STATUS_OK) {
      Serial.print("Page "); 
      if (page < 10) Serial.print(" ");
      Serial.print(page); 
      Serial.print(": ");
      printHexAndAscii(buffer, 4); // 4 bytes per page
      
      // Extract printable text from this page
      for (byte i = 0; i < 4; i++) {
        if (buffer[i] >= 32 && buffer[i] <= 126) {
          allTextData += (char)buffer[i];
          foundTextData = true;
        }
      }
    } else {
      Serial.print("Page "); 
      if (page < 10) Serial.print(" ");
      Serial.print(page);
      Serial.print(" failed: ");
      Serial.println(mfrc522.GetStatusCodeName(status));
    }
  }
  
  // Show extracted text data
  if (foundTextData) {
    Serial.println("\n✓ Text data found on card:");
    Serial.println(allTextData);

    // Print substring starting from "dope.cards" to the end if present
    int dopeIdx = allTextData.indexOf("dope.cards");
    if (dopeIdx >= 0) {
      Serial.println("\nSubstring from dope.cards:");
      String dopeTail = allTextData.substring(dopeIdx);
      Serial.println(dopeTail);

      // Extract intentID = last non-empty path segment
      // Remove query string if present
      int qpos = dopeTail.indexOf('?');
      String pathOnly = (qpos >= 0) ? dopeTail.substring(0, qpos) : dopeTail;
      // Trim trailing slashes
      while (pathOnly.length() > 0 && pathOnly[pathOnly.length()-1] == '/') {
        pathOnly.remove(pathOnly.length()-1);
      }
      int lastSlash = pathOnly.lastIndexOf('/');
      String intentID = (lastSlash >= 0 && lastSlash + 1 < (int)pathOnly.length()) ? pathOnly.substring(lastSlash + 1) : String("");

      // Build POST URL and JSON payload (print only, do not send)
      const char* VAULT_EXECUTE_URL = "https://vault-api.example.com"; // TODO: replace with real base URL
      String postUrl = String(VAULT_EXECUTE_URL) + "/api/v1/transactions/execute-intent-with-intent-id";

      // Construct payload JSON
      String payload = "{";
      payload += "\"intentID\":\"" + intentID + "\",";
      payload += "\"withdrawAction\":{";
      payload += "\"chainID\":2,";
      payload += "\"toAddress\":\"0x123\",";
      payload += "\"tokenAddress\":\"0x0\"";
      payload += "}}";

      Serial.println("\nPOST URL:");
      Serial.println(postUrl);
      Serial.println("Payload:");
      Serial.println(payload);
    } else {
      Serial.println("\nNote: 'dope.cards' not found in text data.");
    }
  } else {
    Serial.println("\n✗ No readable text data found on card.");
  }
  
  // Try to parse as NDEF if it looks like NDEF data
  if (allTextData.length() > 10) {
    Serial.println("\nAttempting to parse as NDEF...");
    String ndefText = parseNDEFFromText(allTextData);
    if (ndefText.length() > 0) {
      Serial.println("✓ NDEF Text Record found:");
      Serial.println(ndefText);
    } else {
      Serial.println("✗ No NDEF text record found in data.");
    }
  }
}

String parseNDEFFromText(String textData) {
  // Convert string to byte array for NDEF parsing
  byte data[128];
  int length = (textData.length() < (unsigned int)128) ? (int)textData.length() : 128;
  
  for (int i = 0; i < length; i++) {
    data[i] = textData.charAt(i);
  }
  
  return parseNDEFTextRecord(data, length);
}

String parseNDEFTextRecord(byte* data, int length) {
  String result = "";
  
  // Look for NDEF TLV (Type-Length-Value) structure
  for (int i = 0; i < length - 1; i++) {
    // Look for NDEF message TLV (Type = 0x03)
    if (data[i] == 0x03) {
      int ndefLength = data[i + 1];
      if (ndefLength > 0 && (i + 2 + ndefLength) <= length) {
        // Found NDEF message, parse it
        byte* ndefMessage = &data[i + 2];
        result = parseNDEFMessage(ndefMessage, ndefLength);
        if (result.length() > 0) {
          return result;
        }
      }
    }
  }
  
  return result;
}

String parseNDEFMessage(byte* message, int length) {
  String result = "";
  
  if (length < 3) return result;
  
  // NDEF Record Header
  byte flags = message[0];
  byte typeLength = message[1];
  int payloadLength = message[2];
  
  // Handle short record (payload length in 1 byte)
  int headerLength = 3;
  int payloadOffset = 3;
  
  // Handle long record (payload length in 3 bytes)
  if (payloadLength == 0xFF) {
    if (length < 6) return result;
    payloadLength = (message[3] << 8) | message[4];
    headerLength = 5;
    payloadOffset = 5;
  }
  
  // Check if this is a text record (Type Name Format = 0x01)
  if ((flags & 0x40) == 0x40) { // TNF = 0x01 (Well Known)
    if (typeLength > 0 && message[payloadOffset] == 'T') { // Text record type
      if (payloadLength > 0 && (payloadOffset + payloadLength) <= length) {
        // Skip language code (first 2 bytes of payload)
        int textStart = payloadOffset + 1;
        int textLength = payloadLength - 1;
        
        // Extract text
        for (int i = 0; i < textLength && (textStart + i) < length; i++) {
          result += (char)message[textStart + i];
        }
      }
    }
  }
  
  return result;
}