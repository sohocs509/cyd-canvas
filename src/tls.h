// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <Arduino.h>

namespace tls {
// Generate an ECDSA P-256 key and a self-signed server certificate for `host`
// (also valid for host.local / host.lan). The mbedTLS build in the Arduino core has no
// certificate writer, so the DER is assembled by hand.
bool generate(const String& host, String& certPem, String& keyPem, String& pubkeyPin);
String fingerprint(const String& certPem);  // SHA-256 of the DER, hex with colons
// Public-key pin in curl's format: sha256//<base64 of SHA-256(SubjectPublicKeyInfo DER)>
}
