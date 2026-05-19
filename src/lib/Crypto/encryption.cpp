#include "encryption.h"
#include "FHSS.h"

#include <cstring>

#if defined(PLATFORM_ESP32)
#include <mbedtls/aes.h>
#include <mbedtls/md.h>
#include <mbedtls/pkcs5.h>
#include <nvs_flash.h>
#include <nvs.h>
#endif

bool OtaEncryptionEnabled = false;

static uint8_t aesKey[16] = {};
static bool aesKeyValid = false;

#if defined(PLATFORM_ESP32)
static mbedtls_aes_context aesCtx;

// Salt prefix - constant string mixed with UID so a given phrase yields different keys per binding.
// Length includes terminating null on purpose (acts as separator before UID).
static constexpr uint8_t KDF_SALT_PREFIX[] = "ELRS-AES-v1";
static constexpr uint32_t KDF_ITERATIONS = 100000; // ~150ms on ESP32 240MHz

static constexpr char NVS_NAMESPACE[] = "ELRS";
static constexpr char NVS_KEY_AESKEY[] = "aeskey";
static constexpr char NVS_KEY_ENCMODE[] = "encmode";

static bool nvs_open_handle(nvs_handle_t *out)
{
    return nvs_open(NVS_NAMESPACE, NVS_READWRITE, out) == ESP_OK;
}

static void persist_key(const uint8_t key[16])
{
    nvs_handle_t h;
    if (!nvs_open_handle(&h)) return;
    nvs_set_blob(h, NVS_KEY_AESKEY, key, 16);
    nvs_commit(h);
    nvs_close(h);
}

static bool load_key(uint8_t key[16])
{
    nvs_handle_t h;
    if (!nvs_open_handle(&h)) return false;
    size_t len = 16;
    esp_err_t err = nvs_get_blob(h, NVS_KEY_AESKEY, key, &len);
    nvs_close(h);
    return err == ESP_OK && len == 16;
}

static void persist_mode(uint8_t mode)
{
    nvs_handle_t h;
    if (!nvs_open_handle(&h)) return;
    nvs_set_u8(h, NVS_KEY_ENCMODE, mode);
    nvs_commit(h);
    nvs_close(h);
}

static bool load_mode(uint8_t *mode)
{
    nvs_handle_t h;
    if (!nvs_open_handle(&h)) return false;
    esp_err_t err = nvs_get_u8(h, NVS_KEY_ENCMODE, mode);
    nvs_close(h);
    return err == ESP_OK;
}

static void install_key(const uint8_t key[16])
{
    memcpy(aesKey, key, 16);
    mbedtls_aes_free(&aesCtx);
    mbedtls_aes_init(&aesCtx);
    mbedtls_aes_setkey_enc(&aesCtx, aesKey, 128);
    aesKeyValid = true;
}
#endif // PLATFORM_ESP32

bool OtaEncryptionKeyIsReady(void)
{
    return aesKeyValid;
}

void OtaEncryptionInit(const uint8_t *phrase, size_t phraseLen)
{
    if (phraseLen == 0)
    {
        memset(aesKey, 0, sizeof(aesKey));
        aesKeyValid = false;
        OtaEncryptionEnabled = false;
        return;
    }

#if defined(PLATFORM_ESP32)
    // Build salt = KDF_SALT_PREFIX || UID
    uint8_t salt[sizeof(KDF_SALT_PREFIX) + UID_LEN];
    memcpy(salt, KDF_SALT_PREFIX, sizeof(KDF_SALT_PREFIX));
    memcpy(salt + sizeof(KDF_SALT_PREFIX), UID, UID_LEN);

    uint8_t derived[16];
    const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    // mbedtls 2.x API used by ESP-IDF v4 retains the deprecated entry-point
    mbedtls_md_context_t md_ctx;
    mbedtls_md_init(&md_ctx);
    if (mbedtls_md_setup(&md_ctx, md_info, 1) == 0)
    {
        mbedtls_pkcs5_pbkdf2_hmac(&md_ctx, phrase, phraseLen,
                                   salt, sizeof(salt),
                                   KDF_ITERATIONS,
                                   sizeof(derived), derived);
    }
    mbedtls_md_free(&md_ctx);

    install_key(derived);
    persist_key(derived);
    memset(derived, 0, sizeof(derived)); // wipe stack copy
#else
    aesKeyValid = false;
#endif
}

void OtaEncryptionLoadFromStorage(void)
{
#if defined(PLATFORM_ESP32)
    uint8_t key[16];
    if (load_key(key))
    {
        install_key(key);
        memset(key, 0, sizeof(key));
    }
    uint8_t mode = 0;
    if (load_mode(&mode))
    {
        OtaEncryptionEnabled = (mode != 0) && aesKeyValid;
    }
#endif
}

bool OtaEncryptionSetMode(bool enabled)
{
#if defined(PLATFORM_ESP32)
    if (enabled && !aesKeyValid)
    {
        // Refuse to enable encryption without a valid key - would brick the link.
        return false;
    }
    OtaEncryptionEnabled = enabled;
    persist_mode(enabled ? 1 : 0);
    return true;
#else
    (void)enabled;
    return false;
#endif
}

static inline void buildIV(uint8_t iv[16], uint8_t nonce, uint8_t fhssIdx)
{
    // IV layout: [nonce(1) | fhssIdx(1) | UID(6) | zeros(8)]
    iv[0] = nonce;
    iv[1] = fhssIdx;
    memcpy(&iv[2], UID, UID_LEN);
    memset(&iv[2 + UID_LEN], 0, 16 - 2 - UID_LEN);
}

void ICACHE_RAM_ATTR OtaCryptBody(OTA_Packet_s *pkt)
{
    if (!OtaEncryptionEnabled || !aesKeyValid)
        return;

    // Skip SYNC packets - RX needs them plain to bind/lock and we want the negotiation flag readable.
    uint8_t type;
    if (OtaIsFullRes)
        type = pkt->full.rc.packetType;
    else
        type = pkt->std.type;
    if (type == PACKET_TYPE_SYNC)
        return;

#if defined(PLATFORM_ESP32)
    uint8_t iv[16];
    buildIV(iv, OtaNonce, FHSSgetCurrIndex());

    uint8_t *bodyStart;
    size_t bodyLen;
    if (OtaIsFullRes)
    {
        bodyStart = ((uint8_t*)pkt) + 1;
        bodyLen = 10; // bytes [1..10], CRC is at [11..12]
    }
    else
    {
        bodyStart = ((uint8_t*)pkt) + 1;
        bodyLen = 6;  // bytes [1..6], crcLow at [7], crcHigh in byte 0 (untouched)
    }

    size_t nc_off = 0;
    uint8_t stream_block[16] = {};
    mbedtls_aes_crypt_ctr(&aesCtx, bodyLen, &nc_off, iv, stream_block, bodyStart, bodyStart);
#else
    (void)pkt;
#endif
}
