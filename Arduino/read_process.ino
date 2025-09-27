#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <SPI.h>
#include <MFRC522.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// RFID wiring
#define MFRC_SS   21
#define MFRC_RST  22

// Wi‑Fi AP credentials (change if you like)
const char* AP_SSID = "ESP32-Scanner";
const char* AP_PASS = "scan12345"; // min 8 chars required by softAP

// STA (internet) credentials - set to your router/hotspot for API calls
const char* STA_SSID = "Airtel_Third"; // e.g., "MyPhoneHotspot"
const char* STA_PASS = "Stay@123"; // e.g., "password123"


// Vault API endpoint to execute by intentID
const char* VAULT_API_BASE = "https://dope-dot-pay-eth-global-production.up.railway.app"; // TODO: set real base
const char* VAULT_EXECUTE_PATH = "/intent/execute";

WebServer server(80);
MFRC522 mfrc522(MFRC_SS, MFRC_RST);

enum UiState {
  STATE_IDLE,
  STATE_SCANNING,
  STATE_DONE
};

volatile bool startRequested = false;
UiState state = STATE_IDLE;
unsigned long scannedAtMs = 0;


// Request context and API status
String reqType;      // "pay" | "donate"
String reqNetwork;   // e.g., "2"
String reqToken;     // e.g., "USDC"
String reqAmount;    // optional for donate
String lastCardUid;  // hex string
String lastApiStatus; // e.g., "API OK (200)" or error
String reqTokenAddress;


// Home page
const char INDEX_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html>
  <head>
    <meta name="viewport" content="width=device-width, initial-scale=1" />
    <title>Dope Cards</title>
    <style>
      :root { --bg:#0c0c0f; --card:#15151b; --text:#eaeaf0; --muted:#9aa0aa; --accent:#6ae3ff; --accent2:#9bff8f; --primary:#0a84ff; }
      * { box-sizing: border-box; }
      body { margin:0; background:linear-gradient(180deg, #0b0b10, #111117); color:var(--text); font-family:-apple-system, BlinkMacSystemFont, Segoe UI, Roboto, Arial; }
      .wrap { min-height:100dvh; display:flex; align-items:center; justify-content:center; padding:24px; }
      .card { width:min(480px, 92vw); background:var(--card); border:1px solid #22242c; border-radius:18px; padding:20px 18px; box-shadow:0 10px 30px rgba(0,0,0,.35); }
      .brand { display:flex; align-items:center; gap:10px; margin-bottom:14px; }
      .logo { width:34px; height:34px; border-radius:10px; background:linear-gradient(135deg, var(--accent), var(--accent2)); }
      h1 { font-size:22px; margin:0; letter-spacing:.2px; }
      p { margin:0; color:var(--muted); font-size:14px; }
      .btns { display:grid; gap:12px; margin-top:18px; }
      .btn { appearance:none; display:flex; align-items:center; justify-content:center; gap:10px; width:100%; padding:16px 14px; font-size:16px; border-radius:12px; border:1px solid #2a2e39; color:var(--text); text-decoration:none; background:#1b1f2a; }
      .btn.primary { background:var(--primary); border-color:#0a6dd6; color:#fff; }
      .btn:hover { filter:brightness(1.05); }
    </style>
  </head>
  <body>
    <div class="wrap">
      <div class="card">
        <div class="brand">
          <div class="logo"></div>
          <div>
            <h1>Dope Cards</h1>
            <p>Choose an action</p>
          </div>
        </div>
        <div class="btns">
          <a class="btn primary" href="/pay">Pay with Dope Cards</a>
          <a class="btn" href="/donate">Donate with Dope Cards</a>
        </div>
      </div>
    </div>
  </body>
</html>
)HTML";

// Pay page
const char PAY_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html>
  <head>
    <meta name="viewport" content="width=device-width, initial-scale=1" />
    <title>Pay • Dope Cards</title>
    <style>
      :root { --bg:#0c0c0f; --card:#15151b; --text:#eaeaf0; --muted:#9aa0aa; --success:#27d17f; --danger:#ff5e7a; --primary:#0a84ff; }
      *{box-sizing:border-box}
      body{margin:0;background:linear-gradient(180deg,#0b0b10,#111117);color:var(--text);font-family:-apple-system,BlinkMacSystemFont,Segoe UI,Roboto,Arial}
      .wrap{min-height:100dvh;display:flex;align-items:center;justify-content:center;padding:18px}
      .card{width:min(520px,94vw);background:var(--card);border:1px solid #22242c;border-radius:18px;padding:18px 16px 20px;box-shadow:0 10px 30px rgba(0,0,0,.35)}
      .top{display:flex;align-items:center;gap:10px;margin-bottom:8px}
      .back{appearance:none;border:none;background:#1b1f2a;color:#cfd3dc;border:1px solid #2a2e39;border-radius:10px;padding:10px 12px;text-decoration:none}
      h2{margin:0;font-size:19px}
      .row{display:grid;gap:8px;margin-top:14px}
      label{font-size:13px;color:#c6cbd6}
      select,input{width:100%;padding:14px 12px;border-radius:12px;border:1px solid #2a2e39;background:#0f121a;color:#e9edf5;font-size:16px}
      .hint{font-size:12px;color:#9aa0aa}
      .grid{display:grid;grid-template-columns:1fr 1fr;gap:10px}
      .btn{appearance:none;display:flex;align-items:center;justify-content:center;width:100%;padding:14px 12px;margin-top:16px;border-radius:12px;border:1px solid #0a6dd6;background:var(--primary);color:#fff;font-size:16px}
      .btn[disabled]{opacity:.7}
      .center{display:flex;align-items:center;justify-content:center}
      .loader{width:36px;height:36px;border-radius:50%;border:3px solid #2a2e39;border-top-color:#56c2ff;animation:spin 1s linear infinite}
      @keyframes spin{to{transform:rotate(360deg)}}
      .success{width:80px;height:80px;border-radius:50%;background:rgba(39,209,127,.1);border:2px solid var(--success);display:flex;align-items:center;justify-content:center;margin:10px auto}
      .success svg{width:42px;height:42px;stroke:var(--success)}
      .hidden{display:none}
    </style>
  </head>
  <body>
    <div class="wrap">
      <div class="card" id="card">
        <div class="top">
          <a class="back" href="/">← Back</a>
          <h2>Pay with Dope Cards</h2>
        </div>
        <div class="row grid">
          <div>
            <label for="network">Network</label>
            <select id="network">
              <option value="2">Base (2)</option>
              <option value="3">Binance (3)</option>
              <option value="4">Arbitrum (4)</option>
              <option value="12">Flow (12)</option>
            </select>
            <div class="hint" id="netHint"></div>
          </div>
          <div>
            <label for="token">Token</label>
            <select id="token"></select>
            <div class="hint" id="tokenAddr"></div>
          </div>
        </div>
        <div class="row">
          <label for="amount">Amount</label>
          <input id="amount" type="number" inputmode="decimal" min="0" step="0.01" placeholder="0.00" />
        </div>
        <button class="btn" id="accept">Accept Payment</button>

        <div class="row center hidden" id="loading">
          <div class="loader"></div>
        </div>
        <div class="row hidden" id="done">
          <div class="success">
            <svg viewBox="0 0 24 24" fill="none" stroke-width="2">
              <path d="M20 6L9 17l-5-5"/>
            </svg>
          </div>
          <div class="center" style="color:#b8bec9">Payment accepted</div>
        </div>
        <div class="row center" id="status" style="color:#b8bec9"></div>
      </div>
    </div>
    <script>
      // networks and token mapping (symbol -> address)
      const NETWORKS = { 2: { name: 'Base' }, 3: { name: 'Binance' }, 4: { name: 'Arbitrum' }, 12: { name: 'Flow' } };
      const TOKENS = {
        2: { USDC: '0x833589fCD6eDb6E08f4c7C32D4f71b54bdA02913' },
        3: { USDC: '0x55d398326f99059fF775485246999027B3197955' },
        4: { USDC: '0xaf88d065e77c8cC2239327C5EDb3A432268e5831' },
        12: { USDC: '' }
      };
      const networkEl = document.getElementById('network');
      const tokenEl = document.getElementById('token');
      const tokenAddr = document.getElementById('tokenAddr');
      const netHint = document.getElementById('netHint');
      const amountEl = document.getElementById('amount');
      const acceptBtn = document.getElementById('accept');
      const loading = document.getElementById('loading');
      const done = document.getElementById('done');
      const statusEl = document.getElementById('status');

      function populateTokens() {
        const n = networkEl.value;
        const map = TOKENS[n] || {};
        tokenEl.innerHTML = '';
        Object.keys(map).forEach(sym => {
          const opt = document.createElement('option');
          opt.value = sym; opt.textContent = sym; tokenEl.appendChild(opt);
        });
        updateTokenAddr();
        netHint.textContent = NETWORKS[n] ? (NETWORKS[n].name + ' network') : '';
      }
      function updateTokenAddr(){
        const n = networkEl.value; const sym = tokenEl.value; const addr = (TOKENS[n]||{})[sym] || '';
        tokenAddr.textContent = addr ? `Address: ${addr}` : '';
      }
      networkEl.addEventListener('change', populateTokens);
      tokenEl.addEventListener('change', updateTokenAddr);
      populateTokens();

      async function poll() {
        const r = await fetch('/status');
        const t = await r.text();
        statusEl.textContent = t;
        if (t.includes('Card scanned')) {
          loading.classList.add('hidden');
          done.classList.remove('hidden');
        } else if (!t.includes('Idle')) {
          setTimeout(poll, 700);
        } else {
          // back to idle without scanning
          loading.classList.add('hidden');
        }
      }

      acceptBtn.addEventListener('click', async () => {
        const network = networkEl.value;
        const token = tokenEl.value;
        const tokenAddress = (TOKENS[network]||{})[token] || '';
        const amount = amountEl.value;
        if (!amount || Number(amount) <= 0) { statusEl.textContent = 'Enter a valid amount'; return; }

        acceptBtn.disabled = true; statusEl.textContent = '';
        done.classList.add('hidden'); loading.classList.remove('hidden');
        // Start RFID scan on device with params
        const q = new URLSearchParams({ type: 'pay', network, token, tokenAddress, amount });
        try { await fetch('/start?' + q.toString()); } catch {}
        poll();
      });
    </script>
  </body>
</html>
)HTML";

// Donate page (same selectors, no amount)
const char DONATE_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html>
  <head>
    <meta name="viewport" content="width=device-width, initial-scale=1" />
    <title>Donate • Dope Cards</title>
    <style>
      :root { --bg:#0c0c0f; --card:#15151b; --text:#eaeaf0; --muted:#9aa0aa; --success:#27d17f; --primary:#0a84ff; }
      *{box-sizing:border-box}
      body{margin:0;background:linear-gradient(180deg,#0b0b10,#111117);color:var(--text);font-family:-apple-system,BlinkMacSystemFont,Segoe UI,Roboto,Arial}
      .wrap{min-height:100dvh;display:flex;align-items:center;justify-content:center;padding:18px}
      .card{width:min(520px,94vw);background:var(--card);border:1px solid #22242c;border-radius:18px;padding:18px 16px 20px;box-shadow:0 10px 30px rgba(0,0,0,.35)}
      .top{display:flex;align-items:center;gap:10px;margin-bottom:8px}
      .back{appearance:none;border:none;background:#1b1f2a;color:#cfd3dc;border:1px solid #2a2e39;border-radius:10px;padding:10px 12px;text-decoration:none}
      h2{margin:0;font-size:19px}
      .row{display:grid;gap:8px;margin-top:14px}
      label{font-size:13px;color:#c6cbd6}
      select{width:100%;padding:14px 12px;border-radius:12px;border:1px solid #2a2e39;background:#0f121a;color:#e9edf5;font-size:16px}
      .hint{font-size:12px;color:#9aa0aa}
      .grid{display:grid;grid-template-columns:1fr 1fr;gap:10px}
      .btn{appearance:none;display:flex;align-items:center;justify-content:center;width:100%;padding:14px 12px;margin-top:16px;border-radius:12px;border:1px solid #0a6dd6;background:var(--primary);color:#fff;font-size:16px}
      .center{display:flex;align-items:center;justify-content:center}
      .loader{width:36px;height:36px;border-radius:50%;border:3px solid #2a2e39;border-top-color:#56c2ff;animation:spin 1s linear infinite}
      @keyframes spin{to{transform:rotate(360deg)}}
      .success{width:80px;height:80px;border-radius:50%;background:rgba(39,209,127,.1);border:2px solid var(--success);display:flex;align-items:center;justify-content:center;margin:10px auto}
      .success svg{width:42px;height:42px;stroke:var(--success)}
      .hidden{display:none}
    </style>
  </head>
  <body>
    <div class="wrap">
      <div class="card" id="card">
        <div class="top">
          <a class="back" href="/">← Back</a>
          <h2>Donate with Dope Cards</h2>
        </div>
        <div class="row grid">
          <div>
            <label for="network">Network</label>
            <select id="network">
              <option value="2">Base (2)</option>
              <option value="2">Binance (3)</option>
              <option value="4">Arbitrum (4)</option>
              <option value="12">Flow (12)</option>
            </select>
            <div class="hint" id="netHint"></div>
          </div>
          <div>
            <label for="token">Token</label>
            <select id="token"></select>
            <div class="hint" id="tokenAddr"></div>
          </div>
        </div>
        <button class="btn" id="donate">Donate</button>

        <div class="row center hidden" id="loading">
          <div class="loader"></div>
        </div>
        <div class="row hidden" id="done">
          <div class="success">
            <svg viewBox="0 0 24 24" fill="none" stroke-width="2">
              <path d="M20 6L9 17l-5-5"/>
            </svg>
          </div>
          <div class="center" style="color:#b8bec9">Donation accepted</div>
        </div>
        <div class="row center" id="status" style="color:#b8bec9"></div>
      </div>
    </div>
    <script>
      // network -> token symbol -> address
      const NETWORKS = { 2: { name: 'Base' }, 3: { name: 'Binance' }, 4: { name: 'Arbitrum' }, 12: { name: 'Flow' } };
      const TOKENS = {
        2: { USDC: '0x833589fCD6eDb6E08f4c7C32D4f71b54bdA02913' },
        3: { USDC: '0x55d398326f99059fF775485246999027B3197955' },
        4: { USDC: '0xaf88d065e77c8cC2239327C5EDb3A432268e5831' },
        12: { USDC: '' }
      };
      
  
      const networkEl = document.getElementById('network');
      const tokenEl = document.getElementById('token');
      const tokenAddr = document.getElementById('tokenAddr');
      const netHint = document.getElementById('netHint');
      const donateBtn = document.getElementById('donate');
      const loading = document.getElementById('loading');
      const done = document.getElementById('done');
      const statusEl = document.getElementById('status');

      function populateTokens() {
        const n = networkEl.value;
        const map = TOKENS[n] || {};
        tokenEl.innerHTML = '';
        Object.keys(map).forEach(sym => {
          const opt = document.createElement('option');
          opt.value = sym; opt.textContent = sym; tokenEl.appendChild(opt);
        });
        updateTokenAddr();
        netHint.textContent = n === '2' ? 'Base network' : (n === '10' ? 'Optimism network' : '');
      }
      function updateTokenAddr(){
        const n = networkEl.value; const sym = tokenEl.value; const addr = (TOKENS[n]||{})[sym] || '';
        tokenAddr.textContent = addr ? `Address: ${addr}` : '';
      }
      networkEl.addEventListener('change', populateTokens);
      tokenEl.addEventListener('change', updateTokenAddr);
      populateTokens();

      async function poll() {
        const r = await fetch('/status');
        const t = await r.text();
        statusEl.textContent = t;
        if (t.includes('Card scanned')) {
          loading.classList.add('hidden');
          done.classList.remove('hidden');
        } else if (!t.includes('Idle')) {
          setTimeout(poll, 700);
        } else {
          loading.classList.add('hidden');
        }
      }

      donateBtn.addEventListener('click', async () => {
        const network = networkEl.value;
        const token = tokenEl.value;
        const tokenAddress = (TOKENS[network]||{})[token] || '';
        const q = new URLSearchParams({ type: 'donate', network, token, tokenAddress });
        statusEl.textContent = '';
        done.classList.add('hidden');
        loading.classList.remove('hidden');
        try { await fetch('/start?' + q.toString()); } catch {}
        poll();
      });
    </script>
  </body>
</html>
)HTML";

void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleStart() {
  // Capture params from query string
  reqType = server.hasArg("type") ? server.arg("type") : String();
  reqNetwork = server.hasArg("network") ? server.arg("network") : String();
  reqToken = server.hasArg("token") ? server.arg("token") : String();
  reqTokenAddress = server.hasArg("tokenAddress") ? server.arg("tokenAddress") : String();
  reqAmount = server.hasArg("amount") ? server.arg("amount") : String();
  startRequested = true;
  lastApiStatus = "";
  server.send(200, "text/plain", "OK");
}

String statusString() {
  switch (state) {
    case STATE_IDLE: return "Idle: Press Start to scan";
    case STATE_SCANNING: return "Scanning... Tap card on reader";
    case STATE_DONE: return "Card scanned. Returning to idle...";
  }
  return "Unknown";
}

void handleStatus() {
  String s = statusString();
  if (lastApiStatus.length() > 0) {
    s += String(" | ") + lastApiStatus;
  }
  server.send(200, "text/plain", s);
}

void handlePay() {
  server.send_P(200, "text/html", PAY_HTML);
}

void handleDonate() {
  server.send_P(200, "text/html", DONATE_HTML);
}

void setup() {
  Serial.begin(115200);
  delay(200);

  // Start Wi‑Fi AP + try STA for internet
  WiFi.mode(WIFI_AP_STA);
  bool ok = WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("AP start "); Serial.println(ok ? "OK" : "FAIL");
  Serial.print("Connect to SSID: "); Serial.println(AP_SSID);
  Serial.print("Password: "); Serial.println(AP_PASS);
  Serial.print("Open http://"); Serial.println(WiFi.softAPIP());

  if (strlen(STA_SSID) > 0) {
    WiFi.begin(STA_SSID, STA_PASS);
    Serial.print("Connecting STA to "); Serial.println(STA_SSID);
    // Wait up to 10s for connection
    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 10000) {
      delay(250);
      Serial.print('.');
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("STA connected. IP: "); Serial.println(WiFi.localIP());
      Serial.print("Gateway: "); Serial.println(WiFi.gatewayIP());
      Serial.print("DNS: "); Serial.println(WiFi.dnsIP());
    } else {
      Serial.println("STA not connected (timeout). API calls will be skipped.");
    }
  }

  // Web routes
  server.on("/", handleRoot);
  server.on("/pay", HTTP_GET, handlePay);
  server.on("/donate", HTTP_GET, handleDonate);
  server.on("/start", HTTP_GET, handleStart);
  server.on("/status", HTTP_GET, handleStatus);
  server.begin();

  // RFID init
  SPI.begin();
  mfrc522.PCD_Init();
  Serial.println("RFID ready");
}

// Read first pages of MIFARE Ultralight and extract printable text
static String readUltralightText() {
  String text = "";
  for (byte page = 0; page <= 12; page++) {
    byte buffer[18];
    byte size = sizeof(buffer);
    MFRC522::StatusCode status = mfrc522.MIFARE_Read(page, buffer, &size);
    if (status == MFRC522::STATUS_OK) {
      for (byte i = 0; i < 4; i++) {
        if (buffer[i] >= 32 && buffer[i] <= 126) text += (char)buffer[i];
      }
    }
  }
  return text;
}

// Extract last path segment from a URL present in the text
static String extractIntentIdFromText(const String &text) {
  Serial.println("URL");
  Serial.println(text);
  int start = text.indexOf("http");
  if (start < 0) start = text.indexOf("dope.cards");
  if (start < 0) return String("");
  // take substring until first whitespace
  int end = start;
  while (end < (int)text.length() && !isspace((unsigned char)text[end])) end++;
  String url = text.substring(start, end);
  // strip query
  int q = url.indexOf('?');
  if (q >= 0) url = url.substring(0, q);
  // trim trailing slashes
  while (url.length() > 0 && url[url.length()-1] == '/') url.remove(url.length()-1);
  int lastSlash = url.lastIndexOf('/');
  Serial.print("string: "); Serial.println(url);

  if (lastSlash >= 0 && lastSlash + 1 < (int)url.length()) return url.substring(lastSlash + 1);
  return String("");
}

// Extract plain alphanumeric token (no URL) from text, e.g., "CmfDdFr5hx"
static String extractPlainIntentIdFromText(const String &text) {
  String best = "";
  String curr = "";
  for (int i = 0; i < (int)text.length(); i++) {
    char c = text[i];
    bool ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    if (ok) {
      curr += c;
    } else {
      if (curr.length() > best.length()) best = curr;
      curr = "";
    }
  }
  if (curr.length() > best.length()) best = curr;
  return best;
}

// Read user area bytes (pages 4..N) into buffer and return length
static int readUserAreaBytes(uint8_t *out, int maxLen) {
  int written = 0;
  // Read in 16-byte chunks (4 pages) starting at page 4
  for (byte page = 4; page <= 44; page += 4) {
    if (written + 16 > maxLen) break;
    byte buf[18];
    byte sz = sizeof(buf);
    MFRC522::StatusCode st = mfrc522.MIFARE_Read(page, buf, &sz);
    if (st != MFRC522::STATUS_OK) break;
    // First 16 bytes are data
    for (int i = 0; i < 16; i++) out[written++] = buf[i];
  }
  return written;
}

// Parse NDEF Text record (TNF Well-known, Type 'T') from a byte buffer
static String parseNdefTextFromBytes(const uint8_t *data, int len) {
  // TLV scan for 0x03
  int i = 0;
  while (i + 2 < len) {
    uint8_t t = data[i++];
    if (t == 0x00) continue; // NULL TLV
    if (t == 0xFE) break;    // Terminator
    uint32_t L = data[i++];
    if (L == 0xFF) { // long length
      if (i + 2 > len) return String("");
      L = (data[i] << 8) | data[i + 1];
      i += 2;
    }
    if (t != 0x03) { i += L; continue; }
    // NDEF message at data[i .. i+L)
    int p = i;
    if (p + 3 > len) return String("");
    uint8_t hdr = data[p++];
    uint8_t typeLen = data[p++];
    uint32_t payloadLen = 0;
    bool sr = (hdr & 0x10) != 0;
    if (sr) {
      if (p >= len) return String("");
      payloadLen = data[p++];
    } else {
      if (p + 4 > len) return String("");
      payloadLen = (data[p] << 24) | (data[p + 1] << 16) | (data[p + 2] << 8) | data[p + 3];
      p += 4;
    }
    bool il = (hdr & 0x08) != 0;
    if (il) { if (p >= len) return String(""); p += 1; }
    // Type
    if (p + typeLen > len) return String("");
    // 'T' = 0x54
    bool isText = (typeLen == 1 && data[p] == 0x54);
    p += typeLen;
    if (!isText) return String("");
    // Payload
    if (p + (int)payloadLen > len) return String("");
    const uint8_t *pl = &data[p];
    if (payloadLen == 0) return String("");
    uint8_t status = pl[0];
    uint8_t langLen = status & 0x3F;
    int txtStart = 1 + langLen;
    if (txtStart > (int)payloadLen) return String("");
    int txtLen = payloadLen - txtStart;
    String out;
    out.reserve(txtLen);
    for (int j = 0; j < txtLen; j++) out += (char)pl[txtStart + j];
    return out;
  }
  return String("");
}

static void sendFinalApiIfPossible() {
  lastApiStatus = "";
  if (WiFi.status() != WL_CONNECTED) {
    lastApiStatus = "API skipped (no internet)";
    return;
  }

  // Build Vault URL
  String url = String(VAULT_API_BASE) + String(VAULT_EXECUTE_PATH);
  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();
  if (!http.begin(client, url)) { lastApiStatus = "API begin fail"; return; }
  http.addHeader("Content-Type", "application/json");

  // API fields mapping per requirement:
  // intentID = last path segment from URL stored on card (not UID)
  // chainID = network (from UI)
  // tokenAddress = tokenAddress (from UI)
  // toAddress -> user will set on server; we omit or set placeholder
  int chainId = reqNetwork.length() ? reqNetwork.toInt() : 0;
  String tokenAddress = reqTokenAddress;

  // Build intentID from card URL text (last path segment)
  // Prefer NDEF Text record exact contents if present
  uint8_t ubytes[512];
  int ulen = readUserAreaBytes(ubytes, sizeof(ubytes));
  String fullIntentId = parseNdefTextFromBytes(ubytes, ulen);
  if (fullIntentId.length() == 0) {
    // Fallback to previous heuristics
    String text = readUltralightText();
    // Try URL-based extraction first
    fullIntentId = extractIntentIdFromText(text);
    if (fullIntentId.length() == 0) {
      fullIntentId = extractPlainIntentIdFromText(text);
    }
  }
  if (fullIntentId.length() == 0) {
    lastApiStatus = "No intentID on card";
    http.end();
    return;
  }
  // Use the complete last path segment (e.g., "CmfDdFr5hx")
  String intentId = fullIntentId;

  // Build withdrawAction, include amount only for Pay flow
  String withdraw = String("\"withdrawAction\":{") +
      "\"chainID\":" + String(chainId) + "," +
      "\"tokenAddress\":\"" + tokenAddress + "\",";
  if (reqType == String("pay") && reqAmount.length() > 0) {
    withdraw += "\"amount\":\"" + reqAmount + "\",";
  }
  withdraw += "\"toAddress\":\"0xEe20A8fCBFB86BA1D8C9B28cafFC94fC43056b36\"}";

  String payload = String("{") +
    "\"intentID\":\"" + intentId + "\"," +
    withdraw +
  "}";

  Serial.println("Payload:");
  Serial.println(payload);
  int code = http.POST(payload);
  if (code > 0) lastApiStatus = String("API OK (") + code + ")"; else lastApiStatus = String("API FAIL: ") + http.errorToString(code);
  http.end();
}

void loop() {
  server.handleClient();

  if (state == STATE_IDLE) {
    if (startRequested) {
      startRequested = false;
      state = STATE_SCANNING;
      Serial.println("Starting scan...");
    }
  } else if (state == STATE_SCANNING) {
    if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
      Serial.print("Card UID: ");
      lastCardUid = "";
      for (byte i = 0; i < mfrc522.uid.size; i++) {
        if (mfrc522.uid.uidByte[i] < 0x10) Serial.print("0");
        Serial.print(mfrc522.uid.uidByte[i], HEX);
        Serial.print(" ");
        if (mfrc522.uid.uidByte[i] < 0x10) lastCardUid += "0";
        lastCardUid += String(mfrc522.uid.uidByte[i], HEX);
      }
      Serial.println();
      Serial.println("Card scanned");
      scannedAtMs = millis();
      // Fire API (if STA internet is available)
      sendFinalApiIfPossible();
      state = STATE_DONE;
      mfrc522.PICC_HaltA();
      mfrc522.PCD_StopCrypto1();
    }
  } else if (state == STATE_DONE) {
    if (millis() - scannedAtMs > 2000) {
      state = STATE_IDLE;
    }
  }

  delay(10);
}
