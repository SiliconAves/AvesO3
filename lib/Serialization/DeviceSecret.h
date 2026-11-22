#pragma once

#include <string>

// Encrypts small secrets kept on the SD card (saved Wi-Fi networks, the PIN)
// with AES-256-GCM under a random per-device key stored in internal flash (NVS),
// never on the card. A copied SD card cannot be decrypted on its own; if NVS is
// erased the secrets become unreadable and are treated as missing.
namespace device_secret {

// base64(nonce || ciphertext || tag), or an empty string on failure.
std::string seal(const std::string& plaintext);

// Decrypts seal() output. False (and an empty plaintext) if the data was
// altered, belongs to another device, or the key is unavailable.
bool open(const char* sealed, std::string& plaintext);

}  // namespace device_secret
