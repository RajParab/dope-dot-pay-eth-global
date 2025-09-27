# Dope.Pay Contracts

This folder contains sample smart contracts used by the Dope.Pay POS server for randomness (donation flow) via Pyth Entropy.

## Files

- `entropy-pyth.sol` – Example contract integrating Pyth Entropy V2 to request and consume randomness. Adapt as needed for your network and provider.

## Deploy (Remix)

1. Open Remix (remix.ethereum.org), create a new file and paste the contract code.
2. Ensure imports resolve (Pyth Entropy SDK):
   - `@pythnetwork/entropy-sdk-solidity/IEntropyV2.sol`
   - `@pythnetwork/entropy-sdk-solidity/IEntropyConsumer.sol`
   - `@pythnetwork/entropy-sdk-solidity/EntropyStructsV2.sol`
3. Compiler:
   - Solidity 0.8.x (match pragma), optimizer on (200 runs).
4. Deploy (Injected Provider – MetaMask):
   - Constructor args:
     - `entropyV2`: Deployed Entropy V2 contract address for your chain.
     - `provider`: Default provider address published by Pyth for your chain.
5. After deploy, save the deployed address for server use.

## Usage

- Funding: Users (or your app) must pay the fee for each randomness request.
- Request:
  - Call `getFee()` to fetch the current fee.
  - Call `requestRandom()` with `value = getFee()`.
  - Wait for `RandomFulfilled(sequenceNumber, random)` event (provider fulfills on‑chain).
- Read a fixed‑point "random float":
  - Expose a function like `randomFloat(min, max, decimals)` that scales `lastRandom` into `[min, max]` by 10^`decimals`.
  - Off‑chain, divide the returned value by 10^decimals to get the human‑readable float.

## Server Integration

- In `project-repo/index.js`, the server can call your contract’s `randomFloat(min, max, decimals)` using ethers:
  - Set `BASE_RPC_URL` (or `RPC_URL`).
  - Set `ENTROPY_RANDOM_ADDRESS` (or use the default in code).
  - On success, the Donate flow uses the returned float as the amount; the Pay flow continues to use a user‑supplied amount.

## Notes

- Get Pyth Entropy V2 and default provider addresses from Pyth’s documentation for your network.
- The randomness request will revert if you don’t pay at least the current fee.
- For production, add access controls and robust error handling.

## License

Apache‑2.0
