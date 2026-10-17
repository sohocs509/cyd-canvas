// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <Arduino.h>

// A private memory pool for mbedTLS. A TLS handshake needs ~34 KB in two contiguous chunks, which a
// 72 KB fragmented heap cannot reliably give. Reserving the pool at boot, while the heap is
// pristine, makes handshakes independent of whatever else (menus, JSON, Strings) is allocating.
namespace tlsmem {
bool begin(size_t bytes);       // call first thing in setup(); falls back to the normal heap if it fails
size_t freeBytes();             // free in the pool
size_t largestFree();
}
