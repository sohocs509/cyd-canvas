// SPDX-License-Identifier: Apache-2.0
#include "tls.h"
#include <mbedtls/base64.h>
#include <mbedtls/ecp.h>
#include <mbedtls/md.h>
#include <mbedtls/pk.h>
#include <vector>

typedef std::vector<uint8_t> Bytes;

static int rng(void*, unsigned char* out, size_t len) {
  esp_fill_random(out, len);
  return 0;
}

static Bytes tlv(uint8_t tag, const Bytes& v) {
  Bytes o{tag};
  size_t n = v.size();
  if (n < 128) o.push_back(n);
  else if (n < 256) { o.push_back(0x81); o.push_back(n); }
  else { o.push_back(0x82); o.push_back(n >> 8); o.push_back(n & 0xFF); }
  o.insert(o.end(), v.begin(), v.end());
  return o;
}

static Bytes cat(std::initializer_list<Bytes> parts) {
  Bytes o;
  for (const Bytes& p : parts) o.insert(o.end(), p.begin(), p.end());
  return o;
}

static Bytes seq(std::initializer_list<Bytes> parts) { return tlv(0x30, cat(parts)); }
static Bytes str(uint8_t tag, const String& s) { return tlv(tag, Bytes(s.begin(), s.end())); }
static Bytes raw(std::initializer_list<uint8_t> b) { return Bytes(b); }

static String pem(const char* label, const uint8_t* der, size_t len) {
  size_t olen = 0;
  mbedtls_base64_encode(nullptr, 0, &olen, der, len);
  std::vector<unsigned char> b64(olen + 1);
  mbedtls_base64_encode(b64.data(), b64.size(), &olen, der, len);
  String out = String("-----BEGIN ") + label + "-----\n";
  for (size_t i = 0; i < olen; i += 64) {
    out += String((const char*)b64.data()).substring(i, min(i + 64, olen));
    out += "\n";
  }
  out += String("-----END ") + label + "-----\n";
  return out;
}

static bool pemToDer(const String& p, Bytes& der) {
  int a = p.indexOf("-----BEGIN CERTIFICATE-----");
  int b = p.indexOf("-----END CERTIFICATE-----");
  if (a < 0 || b < 0) return false;
  String body = p.substring(a + 27, b);
  body.replace("\n", ""); body.replace("\r", "");
  der.assign(body.length(), 0);
  size_t olen = 0;
  if (mbedtls_base64_decode(der.data(), der.size(), &olen, (const unsigned char*)body.c_str(), body.length())) return false;
  der.resize(olen);
  return true;
}

String tls::fingerprint(const String& certPem) {
  Bytes der;
  if (!pemToDer(certPem, der)) return "";
  uint8_t h[32];
  mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), der.data(), der.size(), h);
  String out;
  for (int i = 0; i < 32; i++) {
    char b[4];
    snprintf(b, sizeof(b), i ? ":%02X" : "%02X", h[i]);
    out += b;
  }
  return out;
}

bool tls::generate(const String& host, String& certPem, String& keyPem, String& pubkeyPin) {
  mbedtls_pk_context pk;
  mbedtls_pk_init(&pk);
  bool ok = false;
  do {
    if (mbedtls_pk_setup(&pk, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY))) break;
    if (mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(pk), rng, nullptr)) break;

    unsigned char buf[1024];
    if (mbedtls_pk_write_key_pem(&pk, buf, sizeof(buf))) break;
    keyPem = (const char*)buf;

    int n = mbedtls_pk_write_pubkey_der(&pk, buf, sizeof(buf));  // written at the end of buf
    if (n <= 0) break;
    Bytes spki(buf + sizeof(buf) - n, buf + sizeof(buf));
    {
      uint8_t ph[32];
      mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), spki.data(), spki.size(), ph);
      unsigned char b64[64];
      size_t bl = 0;
      mbedtls_base64_encode(b64, sizeof(b64), &bl, ph, 32);
      pubkeyPin = String("sha256//") + String((const char*)b64).substring(0, bl);
    }

    Bytes sigAlg = seq({raw({0x06, 0x08, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x04, 0x03, 0x02})});
    Bytes name = seq({tlv(0x31, seq({raw({0x06, 0x03, 0x55, 0x04, 0x03}), str(0x0C, host)}))});
    Bytes validity = seq({str(0x17, "250101000000Z"), str(0x17, "450101000000Z")});

    Bytes serial(8);
    esp_fill_random(serial.data(), serial.size());
    serial[0] = (serial[0] & 0x3F) | 0x40;

    Bytes san = seq({str(0x82, host), str(0x82, host + ".local"), str(0x82, host + ".lan")});
    Bytes exts = tlv(0xA3, seq({
        seq({raw({0x06, 0x03, 0x55, 0x1D, 0x13, 0x01, 0x01, 0xFF}), tlv(0x04, seq({}))}),
        seq({raw({0x06, 0x03, 0x55, 0x1D, 0x11}), tlv(0x04, san)}),
        seq({raw({0x06, 0x03, 0x55, 0x1D, 0x25}),
             tlv(0x04, seq({raw({0x06, 0x08, 0x2B, 0x06, 0x01, 0x05, 0x05, 0x07, 0x03, 0x01})}))}),
    }));

    Bytes tbs = seq({raw({0xA0, 0x03, 0x02, 0x01, 0x02}), tlv(0x02, serial), sigAlg, name, validity, name, spki, exts});

    uint8_t h[32];
    mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), tbs.data(), tbs.size(), h);
    unsigned char sig[128];
    size_t siglen = 0;
    if (mbedtls_pk_sign(&pk, MBEDTLS_MD_SHA256, h, 32, sig, &siglen, rng, nullptr)) break;
    Bytes sigBits{0x00};
    sigBits.insert(sigBits.end(), sig, sig + siglen);

    Bytes cert = seq({tbs, sigAlg, tlv(0x03, sigBits)});
    certPem = pem("CERTIFICATE", cert.data(), cert.size());
    ok = true;
  } while (0);
  mbedtls_pk_free(&pk);
  return ok;
}
