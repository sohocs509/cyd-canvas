// SPDX-License-Identifier: Apache-2.0
#pragma once
// Lock/handler tracking used to diagnose stalls (printed by the network task).
extern volatile const char* g_sdHolder;
extern volatile const char* g_lvHolder;
extern char g_route[72];
extern volatile uint32_t g_routeSince;
