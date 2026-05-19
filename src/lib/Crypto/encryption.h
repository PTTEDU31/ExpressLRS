#pragma once

#include <cstdint>
#include <cstddef>

#include "OTA.h"

#ifdef __cplusplus
extern "C" {
#endif

// Runtime toggle - off by default to preserve stock ELRS compatibility.
// Set persistently via OtaEncryptionSetMode(); set in-memory via direct assignment after
// successful key derivation.
extern bool OtaEncryptionEnabled;

// Returns true if a key is loaded and encryption can be safely enabled.
bool OtaEncryptionKeyIsReady(void);

// Derive 128-bit AES key from bind phrase using PBKDF2-HMAC-SHA256 (100k iterations).
// Salt = constant "ELRS-AES-v1\0" || UID (so a given phrase produces different keys per binding).
// Persists derived key to NVS so subsequent boots can load without re-deriving.
// Takes ~50-200ms on ESP32 at 240MHz (acceptable bind-time delay).
//
// Must be called whenever the bind phrase changes.
void OtaEncryptionInit(const uint8_t *phrase, size_t phraseLen);

// Load persisted AES key + encryption mode from NVS at boot.
// Call once during setup() before the link runs. Safe to call when no key has ever been stored
// (silently sets mode to off).
void OtaEncryptionLoadFromStorage(void);

// Persist encryption-enabled flag to NVS and update the runtime flag.
// Refuses to enable if no key is loaded (returns false).
bool OtaEncryptionSetMode(bool enabled);

// AES-128-CTR encrypt/decrypt body region of OTA packet in-place.
// CTR is symmetric: encrypt == decrypt. Single function used both directions.
// IV is derived per-packet from OtaNonce + fhssIndex + UID so each packet has unique keystream.
// Skips SYNC packets (must stay readable for bind/lock).
// Skips byte 0 (packet type) and CRC bytes.
//
// Std packet (8B): encrypts bytes [1..6] (6 bytes)
// Full packet (13B): encrypts bytes [1..10] (10 bytes)
void OtaCryptBody(OTA_Packet_s *pkt);

#ifdef __cplusplus
}
#endif
