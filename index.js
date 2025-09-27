const express = require("express");
const axios = require("axios");
const crypto = require("crypto");
const { ethers } = require("ethers");
const entropyAbi = require("./abi/entropy-abi.json");
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

// Helper: cryptographically strong random float in [min, max], with decimals
function randomFloat(min, max, decimals = 2) {
  const buf = crypto.randomBytes(4);
  const uint = buf.readUInt32BE(0);
  const ratio = uint / 0xffffffff; // [0,1]
  const raw = min + (max - min) * ratio;
  return parseFloat(raw.toFixed(decimals));
}

// Helper: call on-chain randomFloat on Base
async function getOnChainRandomFloat(min = 1n, max = 5n, decimals = 3) {
  const rpc = process.env.BASE_RPC_URL || process.env.RPC_URL;
  if (!rpc) throw new Error("BASE_RPC_URL (or RPC_URL) not set");
  const addr = "0x6984e9c7d9aF12ef679fc78add955de2b1bA69cD";
  const provider = new ethers.JsonRpcProvider(rpc);
  const c = new ethers.Contract(addr, entropyAbi, provider);
  const out = await c.randomFloat(min, max, decimals);
  // out is [value, scaleDecimals]
  const value = out[0];
  const scaleDecimals = Number(out[1]);
  const scale = 10 ** scaleDecimals;
  const asFloat = Number(value.toString()) / scale;
  return { value: asFloat, raw: value.toString(), decimals: scaleDecimals };
}

// POST /intent/execute  { intentID, chainID, tokenAddress, toAddress, amount? }
app.post("/intent/execute", async (req, res) => {
  try {
    const payloadBody = req.body || {};

    console.log("body");
    console.log(req.body);

    const info = await isQrcodeIntent(payloadBody.intentID);
    const label = info.intentLabel;

    switch (label) {
      case "DOPE_PAY": {
        const base = process.env.VAULT_EXECUTE_URL;
        const url = `${base}/api/v1/transactions/execute-intent-with-intent-id`;

        console.log("Payload: " + JSON.stringify(payloadBody));
        const r = await axios.post(url, payloadBody, {
          headers: { "Content-Type": "application/json" },
        });
        return res.json({ ok: true, label, response: r.data });
      }
      case "DONATE": {
        // Generate amount (1..5) and call execute API with that amount
        let source = "fallback";
        let amountNum = 1.0;
        try {
          const onchain = await getOnChainRandomFloat(1n, 5n, 3);
          amountNum = onchain.value;
          source = "onchain";
        } catch (_) {
          amountNum = randomFloat(1, 5, 3);
          source = "fallback";
        }

        const amountStr = Number.isFinite(amountNum)
          ? (Math.round(amountNum * 1000) / 1000).toFixed(3)
          : "1.000";

        const base = process.env.VAULT_EXECUTE_URL;
        const url = `${base}/api/v1/transactions/execute-intent-with-intent-id`;
        const payload = {
          intentID: payloadBody.intentID,
          withdrawAction: {
            chainID: payloadBody.chainID,
            toAddress: payloadBody.toAddress,
            tokenAddress: payloadBody.tokenAddress,
            amount: amountStr,
          },
        };
        const r = await axios.post(url, payload, {
          headers: { "Content-Type": "application/json" },
        });
        return res.json({
          ok: true,
          label,
          source,
          amount: amountStr,
          response: r.data,
        });
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
