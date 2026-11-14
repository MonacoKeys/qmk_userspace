// Copyright 2026 MonacoKeys
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Matches "firmwareVersion" in the MK60MX VIA definition published before this
// repository's via/mk60mx.json, so customers still using that file can connect.
#define VIA_FIRMWARE_VERSION 1

// Underglow settings saved by underglow.c (underglow_config_t).
#define EECONFIG_USER_DATA_SIZE 7
