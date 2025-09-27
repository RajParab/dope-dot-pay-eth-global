// SPDX-License-Identifier: Apache-2.0
    pragma solidity ^0.8.20;

    import "@pythnetwork/entropy-sdk-solidity/IEntropyV2.sol";
    import "@pythnetwork/entropy-sdk-solidity/IEntropyConsumer.sol";
    import "@pythnetwork/entropy-sdk-solidity/EntropyStructsV2.sol";

    /// @title EntropyRandom
    /// @notice Minimal example integrating Pyth Entropy V2 and exposing a fixed-point
    ///         "random float" generator between a caller-provided min and max.
    ///         Solidity has no floating point; we return a scaled integer and its decimals.
    contract EntropyRandom is IEntropyConsumer {
        event RandomRequested(uint64 sequenceNumber);
        event RandomFulfilled(uint64 sequenceNumber, bytes32 random);

        IEntropyV2 private immutable entropy;
        address private immutable defaultProvider;

        // Last fulfilled random and sequence number
        bytes32 public lastRandom;
        uint64 public lastSequence;

        error InsufficientFee();
        error NoRandomYet();

        constructor(address entropyV2, address provider) {
            entropy = IEntropyV2(entropyV2);
            defaultProvider = provider;
        }

        /// @notice Requests a new random value from Entropy using default provider + default gas limit
        /// @dev The caller must attach at least entropy.getFeeV2() wei
        function requestRandom() external payable returns (uint64 sequenceNumber) {
            uint256 fee = entropy.getFeeV2();
            if (msg.value < fee) revert InsufficientFee();
            sequenceNumber = entropy.requestV2{value: fee}();
            emit RandomRequested(sequenceNumber);
        }

        /// @notice Requests with a custom gas limit
        function requestRandomWithGas(uint32 gasLimit) external payable returns (uint64 sequenceNumber) {
            uint256 fee = entropy.getFeeV2(gasLimit);
            if (msg.value < fee) revert InsufficientFee();
            sequenceNumber = entropy.requestV2{value: fee}(gasLimit);
            emit RandomRequested(sequenceNumber);
        }

        /// @notice Helper to fetch the current fee for default provider/gas
        function getFee() external view returns (uint256) {
            return entropy.getFeeV2();
        }

        /// @notice Helper: default provider's default gas limit
        function getDefaultProviderGasLimit() external view returns (uint32) {
            EntropyStructsV2.ProviderInfo memory info = entropy.getProviderInfoV2(entropy.getDefaultProvider());
            return info.defaultGasLimit;
        }

        /// @notice Computes a random fixed-point number in [min, max] using the last fulfilled random
        /// @param min The minimum value (unscaled integer)
        /// @param max The maximum value (unscaled integer)
        /// @param decimals Number of decimal places to scale by (e.g. 2 => 1 = 1.00)
        /// @return value The random value scaled by 10^decimals
        /// @return scaleDecimals The decimals the value is scaled by
        /// @dev Example: to get a random number between 1.00 and 5.00 with 2 decimals,
        ///      call randomFloat(1, 5, 2) and divide the returned value by 10^2 off-chain.
        function randomFloat(uint256 min, uint256 max, uint8 decimals) external view returns (uint256 value, uint8 scaleDecimals) {
            if (lastRandom == bytes32(0)) revert NoRandomYet();
            require(max >= min, "max<min");
            uint256 scale = 10 ** uint256(decimals);
            uint256 minScaled = min * scale;
            uint256 maxScaled = max * scale;
            if (maxScaled == minScaled) return (minScaled, decimals);
            uint256 span = maxScaled - minScaled + 1;
            // Use the full 256-bit entropy as modulus source
            uint256 r = uint256(lastRandom);
            value = minScaled + (r % span);
            scaleDecimals = decimals;
        }

        /// @inheritdoc IEntropyConsumer
        function entropyCallback(
            uint64 sequenceNumber,
            address,
            bytes32 randomNumber
        ) internal override {
            lastRandom = randomNumber;
            lastSequence = sequenceNumber;
            emit RandomFulfilled(sequenceNumber, randomNumber);
        }

        /// @inheritdoc IEntropyConsumer
        function getEntropy() internal view override returns (address) {
            return address(entropy);
        }

        receive() external payable {}
    }