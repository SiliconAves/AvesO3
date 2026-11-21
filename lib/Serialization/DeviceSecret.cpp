#include "DeviceSecret.h"

#include <Logging.h>
#include <Preferences.h>
#include <esp_random.h>
#include <mbedtls/base64.h>
#include <mbedtls/gcm.h>

#include <cstring>
#include <mutex>

namespace device_secret {
namespace {
constexpr size_t KEY_LEN = 32;
constexpr size_t NONCE_LEN = 12;
constexpr size_t TAG_LEN = 16;
constexpr size_t MAX_PLAINTEXT = 8 * 1024;
constexpr const char* KEY_NAMESPACE = "aveso3sec";
constexpr const char* KEY_NAME = "key";

std::mutex keyMutex;
uint8_t deviceKey[KEY_LEN];
bool keyLoaded = false;

// The key is created once, on first use, from the hardware random generator.
bool loadKey() {
  std::lock_guard<std::mutex> lock(keyMutex);
  if (keyLoaded) return true;
  Preferences prefs;
  if (!prefs.begin(KEY_NAMESPACE, false)) {
    LOG_ERR("SEC", "Cannot open device key storage");
    return false;
  }
  if (prefs.getBytesLength(KEY_NAME) == KEY_LEN && prefs.getBytes(KEY_NAME, deviceKey, KEY_LEN) == KEY_LEN) {
    keyLoaded = true;
  } else {
    esp_fill_random(deviceKey, KEY_LEN);
    keyLoaded = prefs.putBytes(KEY_NAME, deviceKey, KEY_LEN) == KEY_LEN;
    if (!keyLoaded) LOG_ERR("SEC", "Cannot store device key");
  }
  prefs.end();
  return keyLoaded;
}
}  // namespace

std::string seal(const std::string& plaintext) {
  if (plaintext.size() > MAX_PLAINTEXT || !loadKey()) return {};

  std::string raw(NONCE_LEN + plaintext.size() + TAG_LEN, '\0');
  auto* out = reinterpret_cast<unsigned char*>(&raw[0]);
  esp_fill_random(out, NONCE_LEN);

  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);
  int ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, deviceKey, KEY_LEN * 8);
  if (ret == 0) {
    ret = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, plaintext.size(), out, NONCE_LEN, nullptr, 0,
                                    reinterpret_cast<const unsigned char*>(plaintext.data()), out + NONCE_LEN, TAG_LEN,
                                    out + NONCE_LEN + plaintext.size());
  }
  mbedtls_gcm_free(&gcm);
  if (ret != 0) {
    LOG_ERR("SEC", "Encryption failed (%d)", ret);
    return {};
  }

  size_t encodedLen = 0;
  mbedtls_base64_encode(nullptr, 0, &encodedLen, out, raw.size());
  std::string encoded(encodedLen, '\0');
  if (mbedtls_base64_encode(reinterpret_cast<unsigned char*>(&encoded[0]), encoded.size(), &encodedLen, out,
                            raw.size()) != 0) {
    return {};
  }
  encoded.resize(encodedLen);
  return encoded;
}

bool open(const char* sealed, std::string& plaintext) {
  plaintext.clear();
  if (!sealed || !loadKey()) return false;

  const size_t sealedLen = strlen(sealed);
  size_t rawLen = 0;
  mbedtls_base64_decode(nullptr, 0, &rawLen, reinterpret_cast<const unsigned char*>(sealed), sealedLen);
  if (rawLen < NONCE_LEN + TAG_LEN || rawLen > NONCE_LEN + MAX_PLAINTEXT + TAG_LEN + 3) return false;
  std::string raw(rawLen, '\0');
  if (mbedtls_base64_decode(reinterpret_cast<unsigned char*>(&raw[0]), raw.size(), &rawLen,
                            reinterpret_cast<const unsigned char*>(sealed), sealedLen) != 0 ||
      rawLen < NONCE_LEN + TAG_LEN) {
    return false;
  }

  const auto* in = reinterpret_cast<const unsigned char*>(raw.data());
  const size_t cipherLen = rawLen - NONCE_LEN - TAG_LEN;
  std::string result(cipherLen, '\0');
  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);
  int ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, deviceKey, KEY_LEN * 8);
  if (ret == 0) {
    ret = mbedtls_gcm_auth_decrypt(&gcm, cipherLen, in, NONCE_LEN, nullptr, 0, in + NONCE_LEN + cipherLen, TAG_LEN,
                                   in + NONCE_LEN, reinterpret_cast<unsigned char*>(&result[0]));
  }
  mbedtls_gcm_free(&gcm);
  if (ret != 0) return false;
  plaintext = std::move(result);
  return true;
}

}  // namespace device_secret
