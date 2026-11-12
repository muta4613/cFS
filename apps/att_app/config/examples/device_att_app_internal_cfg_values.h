/************************************************************************
 * ATT_APP - GNSS signal-health monitor for NASA cFS
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License. You may obtain
 * a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
 ************************************************************************/

/**
 * @file
 *   Example platform configuration: read a live GNSS receiver over a serial
 *   device instead of replaying the recorded log.
 *
 * To use it, copy this file into the mission definitions directory under the
 * name the build looks for, then rebuild from scratch:
 *
 *     cp apps/att_app/config/examples/device_att_app_internal_cfg_values.h \
 *        sample_defs/cpu1_att_app_internal_cfg_values.h
 *     rm -rf build-native_std && make native_std.prep && make native_std.install
 *
 * `make native_std.prep` prints
 * "Using file: .../sample_defs/cpu1_att_app_internal_cfg_values.h" when the
 * override is picked up. Delete the copy and rebuild to go back to log replay.
 *
 * As written, the app opens /tmp/gnss_pty, the pseudo-terminal that
 * apps/att_app/ground/pty_feed.sh creates for testing without hardware. For a
 * real receiver, set HOST_DIR to "/dev" and DEVICE_NAME to the port, usually
 * "ttyUSB0" (USB-serial adapter) or "ttyACM0" (u-blox USB).
 *
 * OSAL has no termios API, so the app cannot set the baud rate. Set it before
 * starting cFS, for example: stty -F /dev/ttyUSB0 9600 raw -echo
 *
 * The receiver must output GGA and GSV sentences. An override replaces the
 * whole default file, so every item has to be defined here, not only the
 * changed ones.
 */
#ifndef ATT_APP_INTERNAL_CFG_VALUES_H
#define ATT_APP_INTERNAL_CFG_VALUES_H

#define ATT_APP_PLATFORM_CFGVAL(x) ATT_APP_DEVICE_CFG_##x

#define ATT_APP_DEVICE_CFG_PIPE_DEPTH              32
#define ATT_APP_DEVICE_CFG_PIPE_NAME               "ATT_APP_CMD_PIPE"
#define ATT_APP_DEVICE_CFG_GNSS_INPUT_MODE         ATT_APP_GNSS_INPUT_MODE_DEVICE   /* changed */
#define ATT_APP_DEVICE_CFG_GNSS_LOG_FILE           "/cf/gnss_sample.nmea"
#define ATT_APP_DEVICE_CFG_GNSS_DEVICE_HOST_DIR    "/tmp"   /* changed */
#define ATT_APP_DEVICE_CFG_GNSS_DEVICE_MOUNT       "/gnssdev"
#define ATT_APP_DEVICE_CFG_GNSS_DEVICE_NAME        "gnss_pty"   /* changed */
#define ATT_APP_DEVICE_CFG_WAKEUPS_PER_EPOCH       50
#define ATT_APP_DEVICE_CFG_GNSS_READ_BUF_SIZE      256
#define ATT_APP_DEVICE_CFG_GNSS_READ_TIMEOUT_MS    0
#define ATT_APP_DEVICE_CFG_GNSS_MAX_LINES_PER_TICK 64
#define ATT_APP_DEVICE_CFG_CNO_THRESHOLD_DEFAULT   30
#define ATT_APP_DEVICE_CFG_CNO_THRESHOLD_MIN       10
#define ATT_APP_DEVICE_CFG_CNO_THRESHOLD_MAX       55
#define ATT_APP_DEVICE_CFG_LOW_CNO_HYSTERESIS_DB   2
#define ATT_APP_DEVICE_CFG_LOW_CNO_SET_COUNT       1
#define ATT_APP_DEVICE_CFG_LOW_CNO_CLEAR_COUNT     1
#define ATT_APP_DEVICE_CFG_NO_FIX_SET_COUNT        1
#define ATT_APP_DEVICE_CFG_NO_FIX_CLEAR_COUNT      2
#define ATT_APP_DEVICE_CFG_DROP_MEAN_DB            8.0f
#define ATT_APP_DEVICE_CFG_DROP_CLEAR_DB           4.0f
#define ATT_APP_DEVICE_CFG_DROP_SAT_DB             6.0f
#define ATT_APP_DEVICE_CFG_DROP_SAT_PERCENT        75
#define ATT_APP_DEVICE_CFG_DROP_MIN_SATS           4
#define ATT_APP_DEVICE_CFG_DROP_SET_COUNT          2
#define ATT_APP_DEVICE_CFG_DROP_CLEAR_COUNT        3
#define ATT_APP_DEVICE_CFG_BASELINE_WINDOW         30
#define ATT_APP_DEVICE_CFG_BASELINE_MIN_SAMPLES    10
#define ATT_APP_DEVICE_CFG_SAT_EXPIRE_EPOCHS       30

#endif
