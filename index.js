const express = require("express");
const axios = require("axios");
const crypto = require("crypto");
const app = express();
const PORT = process.env.PORT || 3000;

// Middleware
app.use(express.json());
app.use(express.urlencoded({ extended: true }));

// Routes
app.get("/", (req, res) => {
  res.json({
    message: "Welcome to Express.js!",
    status: "Server is running successfully",
  });
});

app.get("/health", (req, res) => {
  res.json({
    status: "OK",
    timestamp: new Date().toISOString(),
  });
});

// Helper: get intent label
async function isQrcodeIntent(intentID) {
  const base = process.env.VAULT_EXECUTE_URL;
  if (!base) throw new Error("VAULT_EXECUTE_URL not set");
  const url = `${base}/api/v1/intent/check-if-qr-intent?intentID=${encodeURIComponent(
    intentID
  )}`;
  const response = await axios.get(url);
  return {
    intentLabel: response?.data?.label || "DOPE_PAY",
  };
}

// POST /intent/execute  { intentID, chainID, tokenAddress, toAddress, amount? }
app.post("/intent/execute", async (req, res) => {
  try {
    const { intentID, chainID, tokenAddress, toAddress, amount } =
      req.body || {};
    if (!intentID) return res.status(400).json({ error: "intentID required" });

    const info = await isQrcodeIntent(intentID);
    const label = info.intentLabel;

    switch (label) {
      case "DOPE_PAY": {
        const base = process.env.VAULT_EXECUTE_URL;
        const url = `${base}/api/v1/transactions/execute-intent-with-intent-id`;
        const payload = {
          intentID,
          withdrawAction: {
            chainID,
            toAddress,
            tokenAddress,
            ...(amount ? { amount } : {}),
          },
        };
        const r = await axios.post(url, payload, {
          headers: { "Content-Type": "application/json" },
        });
        return res.json({ ok: true, label, response: r.data });
      }
      case "DONATE": {
        // Pyth entropy system via relay -> fallback to crypto RNG
      }
      default: {
        return res
          .status(400)
          .json({ ok: false, label, error: "Unsupported intent label" });
      }
    }
  } catch (err) {
    console.error(err);
    const msg = err?.response?.data || err.message || "unknown_error";
    res.status(500).json({ ok: false, error: msg });
  }
});

// Start server
app.listen(PORT, () => {
  console.log(`Server is running on http://localhost:${PORT}`);
});
