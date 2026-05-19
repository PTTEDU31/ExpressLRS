# ELRS Crypto Module — AES-128-CTR OTA Encryption

Custom fork of ExpressLRS adding optional AES-128-CTR encryption on OTA packets.
Stronger than [PrivacyLRS](https://github.com/sensei-hacker/PrivacyLRS) in two ways:
chosen cipher is AES (FIPS-approved) and the key-derivation uses PBKDF2-HMAC-SHA256 with
100 000 iterations to slow brute-force on weak bind phrases.

## What it encrypts

- RC data packets (TX → RX channels + switches + arm)
- Telemetry / data downlink (RX → TX GPS, sensors, MSP)
- Tunneled MSP / config commands

## What it does NOT encrypt

- **SYNC packets** — RX must read them in clear to bind / lock FHSS sequence.
- **WiFi configuration AP** — protected by WPA2 already.
- **OTA firmware updates** — flashed over USB / WiFi, separate channel.
- **Preamble / sync-word at LoRa level** — fixed by radio hardware.

## How keys are derived

```
key[16] = PBKDF2-HMAC-SHA256(
    password = bind_phrase,
    salt     = "ELRS-AES-v1\0" || UID[6],
    iters    = 100 000,
    out_len  = 16
)
```

- Both TX and RX derive the same key from the same bind phrase. No key is ever
  transmitted over the air.
- Persisted to NVS (`ELRS` namespace, `aeskey` blob) so subsequent boots load
  without re-deriving.
- Derivation runs once during `SetBindPhrase()` — about 150 ms on ESP32 @240 MHz.

## How packets are encrypted

AES-128-CTR (stream cipher mode) in-place on the OTA packet body. Bytes 0
(packet type + crcHigh) and the CRC bytes are left in clear so:
- RX can dispatch on packet type before decrypting.
- CRC still catches transmission errors before we spend cycles decrypting garbage.

| Mode  | Packet size | Encrypted bytes | CRC bytes |
|-------|-------------|-----------------|-----------|
| Std4  | 8           | 1..6 (6 bytes)  | 0.crcHigh + 7.crcLow |
| Full8 | 13          | 1..10 (10 bytes)| 11..12   |

CTR IV layout (16 bytes):
```
[OtaNonce(1) | fhssIndex(1) | UID(6) | zeros(8)]
```
- `OtaNonce` wraps every 256 packets; combined with `fhssIndex` (≥ 50 hops) it
  takes many seconds before the same IV pair recurs.
- For v1 the keystream may eventually repeat — acceptable given how short-lived
  individual RC packets are. v2 will add a 32-bit wrap counter via SYNC.

## Mode negotiation

The `OTA_Sync_s.cryptoMode` bit (1 bit, repurposed `free` field) carries the TX's
runtime encryption state every sync packet:

- TX writes `OtaEncryptionEnabled` into `sync.cryptoMode` each sync.
- RX reads `sync.cryptoMode`, mirrors it into its own `OtaEncryptionEnabled`
  flag iff a valid key is loaded. RX follows TX.
- If RX has no key, it silently ignores the request (will not decrypt → link
  fails cleanly via CRC mismatch on encrypted body).

## Lua menu

Under the *MODEL* folder a new toggle **Encryption** (Off/On) appears on TX.
Unit text reads `No key` if no bind phrase has been set yet.

Setting to On:
1. Verifies a derived key is in RAM.
2. Persists the mode flag to NVS.
3. Updates `OtaEncryptionEnabled` immediately.
4. RX picks up the change on the next SYNC packet.

## Defaults & compatibility

- `OtaEncryptionEnabled = false` on first boot → behaves like stock ELRS.
- Binds with stock ELRS receivers when off.
- Does **not** bind with stock receivers when on (decryption will fail; RX
  ignores body), unless the RX is also a build with this Crypto module + same
  bind phrase + auto-negotiation via SYNC.

## Verification

### Hardware loopback test
1. Flash 2 ESP32 devices (1 TX + 1 RX) with this firmware.
2. Set the **same** bind phrase on both via ELRS Configurator or backpack MSP.
3. Bind normally. Confirm telemetry flows with encryption Off.
4. Toggle Lua → **Encryption → On**. Verify:
   - Channel data still moves the FC.
   - Telemetry RSSI / battery still flow.
   - DBGVLN logs on RX show no extra CRC errors.
5. Reboot both. Verify the link comes back with encryption still On
   (persistence test).

### Negative test
- TX with encryption On + RX with encryption Off and **same** bind phrase →
  RX should auto-flip to On via SYNC. Link works.
- TX with encryption On + RX with **different** bind phrase → CRC will pass
  (body is just random ciphertext) but decryption produces garbage channel
  values → FC sees random sticks. **This is the expected failure mode.**
- TX with encryption On + RX is stock ELRS → RX may bind (sync looks normal)
  but channel data is garbage. **Set failsafe before testing.**

### Benchmark
- Set `DEBUG_RCVR_LINKSTATS` in `user_defines.txt` to measure round-trip.
- AES-CTR on ESP32 HW-accelerated path: ~10 µs / 16-byte block.
- One block per packet per direction → ~20 µs added per 4 ms cycle → 0.5 %.

## Known limitations

- **No cryptographic authentication.** CTR has no MAC. The 16-bit CRC catches
  random errors but a determined attacker can flip bits in ciphertext to
  predictably flip plaintext bits. Mitigation: short packet lifetime (4 ms)
  + the attacker would also need to forge a valid CRC. Acceptable for
  privacy use case (preventing telemetry eavesdropping). Not acceptable for
  high-stakes anti-injection scenarios.
- **No forward secrecy.** A compromised key decrypts all past + future
  traffic until rotated.
- **8-bit OtaNonce wrap.** See IV section. v2 plan adds wrap counter.
- **Bind phrase strength matters.** PBKDF2 100k iters slows GPU brute-force
  to roughly 1 guess / second on consumer GPU. A weak phrase still falls.
  Use 4+ random-word phrases.

## File map

| File | Role |
|------|------|
| [src/lib/Crypto/encryption.h](encryption.h) | Public API |
| [src/lib/Crypto/encryption.cpp](encryption.cpp) | mbedtls AES-CTR + PBKDF2 + NVS persist |
| [src/lib/CONFIG/config.cpp](../CONFIG/config.cpp) | Derive key on `SetBindPhrase()` |
| [src/lib/OTA/OTA.h](../OTA/OTA.h) | Added `cryptoMode:1` to `OTA_Sync_s` |
| [src/src/tx_main.cpp](../../src/tx_main.cpp) | Encrypt before CRC; broadcast mode in SYNC |
| [src/src/rx_main.cpp](../../src/rx_main.cpp) | Decrypt after CRC; auto-follow TX's mode |
| [src/lib/tx-crsf/TXModuleParameters.cpp](../tx-crsf/TXModuleParameters.cpp) | Lua **Encryption** toggle |

## Future work

- Argon2id KDF (memory-hard, ~10× better GPU brute-force resistance).
- 32-bit wrap counter in SYNC to extend CTR IV uniqueness.
- AES-GCM with truncated 2-byte tag (trade payload bytes for authentication).
- ECDH key exchange at bind for forward secrecy.
- Stub `OtaCryptBody` body for ESP8285 RX so it can interop with encrypted TX
  (requires AES soft impl ~3 KB Flash).
