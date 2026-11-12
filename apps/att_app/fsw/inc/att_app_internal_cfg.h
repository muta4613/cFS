/************************************************************************
 * NASA Docket No. GSC-19,200-1, and identified as "cFS Draco"
 *
 * Copyright (c) 2023 United States Government as represented by the
 * Administrator of the National Aeronautics and Space Administration.
 * All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License. You may obtain
 * a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 ************************************************************************/

/**
 * @file
 *
 * ATT_APP Application Platform Configuration Header File
 *
 * This is a compatibility header for the "platform_cfg.h" file that has
 * traditionally provided both public and private config definitions
 * for each CFS app.
 *
 * These definitions are now provided in two separate files, one for
 * the public/mission scope and one for internal scope.
 *
 * @note This file may be overridden/superceded by mission-provided definitions
 * either by overriding this header or by generating definitions from a command/data
 * dictionary tool.
 */
#ifndef ATT_APP_INTERNAL_CFG_H
#define ATT_APP_INTERNAL_CFG_H

#include "att_app_internal_cfg_values.h"

/***********************************************************************/
#define ATT_APP_PLATFORM_PIPE_DEPTH         ATT_APP_PLATFORM_CFGVAL(PIPE_DEPTH)
#define DEFAULT_ATT_APP_PLATFORM_PIPE_DEPTH 32 /* Depth of the Command Pipe for Application */

#define ATT_APP_PLATFORM_PIPE_NAME         ATT_APP_PLATFORM_CFGVAL(PIPE_NAME)
#define DEFAULT_ATT_APP_PLATFORM_PIPE_NAME "ATT_APP_CMD_PIPE"

#define ATT_APP_PLATFORM_NUMBER_OF_TABLES         ATT_APP_PLATFORM_CFGVAL(NUMBER_OF_TABLES)
#define DEFAULT_ATT_APP_PLATFORM_NUMBER_OF_TABLES 1 /* Number of Example Table(s) */

#define ATT_APP_PLATFORM_TABLE_OUT_OF_RANGE_ERR_CODE         ATT_APP_PLATFORM_CFGVAL(TABLE_OUT_OF_RANGE_ERR_CODE)
#define DEFAULT_ATT_APP_PLATFORM_TABLE_OUT_OF_RANGE_ERR_CODE -1

#define ATT_APP_PLATFORM_TBL_ELEMENT_1_MAX         ATT_APP_PLATFORM_CFGVAL(TBL_ELEMENT_1_MAX)
#define DEFAULT_ATT_APP_PLATFORM_TBL_ELEMENT_1_MAX 10

#define ATT_APP_PLATFORM_TABLE_FILE         ATT_APP_PLATFORM_CFGVAL(TABLE_FILE)
#define DEFAULT_ATT_APP_PLATFORM_TABLE_FILE "/cf/att_app_tbl.tbl"

/***********************************************************************/
/*
** GNSS input
*/

/**
 * \name Choices for #ATT_APP_PLATFORM_GNSS_INPUT_MODE
 * \{
 */
#define ATT_APP_GNSS_INPUT_MODE_FILE   0 /**< \brief Replay a recorded NMEA log, wrapping at end of file */
#define ATT_APP_GNSS_INPUT_MODE_DEVICE 1 /**< \brief Read a live receiver from a serial device */
/** \} */

/**
 * \brief Where GNSS data comes from
 *
 * Both modes use the same OSAL read call and the same parser. They differ
 * only in how the input is opened, in what "no more data" means (end of
 * file wraps; an idle device just waits), and in how much is read per tick
 * (one epoch from a file; everything that has arrived from a device).
 */
#define ATT_APP_PLATFORM_GNSS_INPUT_MODE         ATT_APP_PLATFORM_CFGVAL(GNSS_INPUT_MODE)
#define DEFAULT_ATT_APP_PLATFORM_GNSS_INPUT_MODE ATT_APP_GNSS_INPUT_MODE_FILE

/** \brief OSAL virtual path of the recorded log (file mode) */
#define ATT_APP_PLATFORM_GNSS_LOG_FILE         ATT_APP_PLATFORM_CFGVAL(GNSS_LOG_FILE)
#define DEFAULT_ATT_APP_PLATFORM_GNSS_LOG_FILE "/cf/gnss_sample.nmea"

/**
 * \brief Host directory that holds the receiver's device node (device mode)
 *
 * OSAL only opens paths under a known virtual mount point, so in device mode
 * the app maps this host directory to #ATT_APP_PLATFORM_GNSS_DEVICE_MOUNT at
 * startup and opens #ATT_APP_PLATFORM_GNSS_DEVICE_NAME inside it.
 */
#define ATT_APP_PLATFORM_GNSS_DEVICE_HOST_DIR         ATT_APP_PLATFORM_CFGVAL(GNSS_DEVICE_HOST_DIR)
#define DEFAULT_ATT_APP_PLATFORM_GNSS_DEVICE_HOST_DIR "/dev"

/** \brief OSAL virtual mount point the device directory is mapped to (device mode) */
#define ATT_APP_PLATFORM_GNSS_DEVICE_MOUNT         ATT_APP_PLATFORM_CFGVAL(GNSS_DEVICE_MOUNT)
#define DEFAULT_ATT_APP_PLATFORM_GNSS_DEVICE_MOUNT "/gnssdev"

/** \brief Name of the device node inside the device directory (device mode) */
#define ATT_APP_PLATFORM_GNSS_DEVICE_NAME         ATT_APP_PLATFORM_CFGVAL(GNSS_DEVICE_NAME)
#define DEFAULT_ATT_APP_PLATFORM_GNSS_DEVICE_NAME "ttyUSB0"

/**
 * \brief Scheduler wakeups per GNSS epoch
 *
 * SCH_LAB sends the wakeup at 50 Hz; the receiver produces one epoch per
 * second, so the input is serviced on every 50th wakeup.
 */
#define ATT_APP_PLATFORM_WAKEUPS_PER_EPOCH         ATT_APP_PLATFORM_CFGVAL(WAKEUPS_PER_EPOCH)
#define DEFAULT_ATT_APP_PLATFORM_WAKEUPS_PER_EPOCH 50

/**
 * \brief Size of the buffer that input bytes are collected in
 *
 * Must hold at least two maximum-length sentences so a complete line can
 * always be found; a line that fills the buffer without a newline is discarded.
 */
#define ATT_APP_PLATFORM_GNSS_READ_BUF_SIZE         ATT_APP_PLATFORM_CFGVAL(GNSS_READ_BUF_SIZE)
#define DEFAULT_ATT_APP_PLATFORM_GNSS_READ_BUF_SIZE 256

/**
 * \brief How long one read may wait for data, in milliseconds
 *
 * 0 polls: the app never blocks on the receiver, because the same task
 * also has to answer commands and housekeeping requests.
 */
#define ATT_APP_PLATFORM_GNSS_READ_TIMEOUT_MS         ATT_APP_PLATFORM_CFGVAL(GNSS_READ_TIMEOUT_MS)
#define DEFAULT_ATT_APP_PLATFORM_GNSS_READ_TIMEOUT_MS 0

/**
 * \brief Most sentences processed in one tick
 *
 * Bounds the time spent in one tick if a device floods the input.
 */
#define ATT_APP_PLATFORM_GNSS_MAX_LINES_PER_TICK         ATT_APP_PLATFORM_CFGVAL(GNSS_MAX_LINES_PER_TICK)
#define DEFAULT_ATT_APP_PLATFORM_GNSS_MAX_LINES_PER_TICK 64

/***********************************************************************/
/*
** Signal-health checks
*/

/** \brief Power-on value of the single-satellite low C/N0 threshold, dB-Hz (SET_CNO_THRESHOLD changes it) */
#define ATT_APP_PLATFORM_CNO_THRESHOLD_DEFAULT         ATT_APP_PLATFORM_CFGVAL(CNO_THRESHOLD_DEFAULT)
#define DEFAULT_ATT_APP_PLATFORM_CNO_THRESHOLD_DEFAULT 30

/** \brief Lowest threshold SET_CNO_THRESHOLD accepts, dB-Hz (below this a receiver cannot track at all) */
#define ATT_APP_PLATFORM_CNO_THRESHOLD_MIN         ATT_APP_PLATFORM_CFGVAL(CNO_THRESHOLD_MIN)
#define DEFAULT_ATT_APP_PLATFORM_CNO_THRESHOLD_MIN 10

/** \brief Highest threshold SET_CNO_THRESHOLD accepts, dB-Hz (above this every healthy satellite would be "weak") */
#define ATT_APP_PLATFORM_CNO_THRESHOLD_MAX         ATT_APP_PLATFORM_CFGVAL(CNO_THRESHOLD_MAX)
#define DEFAULT_ATT_APP_PLATFORM_CNO_THRESHOLD_MAX 55

/** \brief LOW_CNO_SAT clears only when every tracked satellite is at least this far above the threshold, dB */
#define ATT_APP_PLATFORM_LOW_CNO_HYSTERESIS_DB         ATT_APP_PLATFORM_CFGVAL(LOW_CNO_HYSTERESIS_DB)
#define DEFAULT_ATT_APP_PLATFORM_LOW_CNO_HYSTERESIS_DB 2

/** \brief Consecutive epochs with a weak satellite before LOW_CNO_SAT sets */
#define ATT_APP_PLATFORM_LOW_CNO_SET_COUNT         ATT_APP_PLATFORM_CFGVAL(LOW_CNO_SET_COUNT)
#define DEFAULT_ATT_APP_PLATFORM_LOW_CNO_SET_COUNT 1

/** \brief Consecutive epochs without a weak satellite before LOW_CNO_SAT clears */
#define ATT_APP_PLATFORM_LOW_CNO_CLEAR_COUNT         ATT_APP_PLATFORM_CFGVAL(LOW_CNO_CLEAR_COUNT)
#define DEFAULT_ATT_APP_PLATFORM_LOW_CNO_CLEAR_COUNT 1

/** \brief Consecutive epochs without a fix before NO_FIX sets */
#define ATT_APP_PLATFORM_NO_FIX_SET_COUNT         ATT_APP_PLATFORM_CFGVAL(NO_FIX_SET_COUNT)
#define DEFAULT_ATT_APP_PLATFORM_NO_FIX_SET_COUNT 1

/** \brief Consecutive epochs with a fix before NO_FIX clears */
#define ATT_APP_PLATFORM_NO_FIX_CLEAR_COUNT         ATT_APP_PLATFORM_CFGVAL(NO_FIX_CLEAR_COUNT)
#define DEFAULT_ATT_APP_PLATFORM_NO_FIX_CLEAR_COUNT 2

/** \brief BROADBAND_DROP: mean C/N0 must be at least this far below its running average, dB */
#define ATT_APP_PLATFORM_DROP_MEAN_DB         ATT_APP_PLATFORM_CFGVAL(DROP_MEAN_DB)
#define DEFAULT_ATT_APP_PLATFORM_DROP_MEAN_DB 8.0f

/** \brief BROADBAND_DROP clears when the mean is back within this of its running average, dB */
#define ATT_APP_PLATFORM_DROP_CLEAR_DB         ATT_APP_PLATFORM_CFGVAL(DROP_CLEAR_DB)
#define DEFAULT_ATT_APP_PLATFORM_DROP_CLEAR_DB 4.0f

/** \brief A satellite at least this far below its own running average counts as dropped, dB */
#define ATT_APP_PLATFORM_DROP_SAT_DB         ATT_APP_PLATFORM_CFGVAL(DROP_SAT_DB)
#define DEFAULT_ATT_APP_PLATFORM_DROP_SAT_DB 6.0f

/** \brief BROADBAND_DROP: at least this share of the baselined satellites must have dropped together, percent */
#define ATT_APP_PLATFORM_DROP_SAT_PERCENT         ATT_APP_PLATFORM_CFGVAL(DROP_SAT_PERCENT)
#define DEFAULT_ATT_APP_PLATFORM_DROP_SAT_PERCENT 75

/** \brief BROADBAND_DROP is not evaluated with fewer baselined satellites than this */
#define ATT_APP_PLATFORM_DROP_MIN_SATS         ATT_APP_PLATFORM_CFGVAL(DROP_MIN_SATS)
#define DEFAULT_ATT_APP_PLATFORM_DROP_MIN_SATS 4

/** \brief Consecutive epochs the drop condition must hold before BROADBAND_DROP sets */
#define ATT_APP_PLATFORM_DROP_SET_COUNT         ATT_APP_PLATFORM_CFGVAL(DROP_SET_COUNT)
#define DEFAULT_ATT_APP_PLATFORM_DROP_SET_COUNT 2

/** \brief Consecutive epochs of recovery before BROADBAND_DROP clears */
#define ATT_APP_PLATFORM_DROP_CLEAR_COUNT         ATT_APP_PLATFORM_CFGVAL(DROP_CLEAR_COUNT)
#define DEFAULT_ATT_APP_PLATFORM_DROP_CLEAR_COUNT 3

/** \brief Length of the C/N0 running averages, epochs */
#define ATT_APP_PLATFORM_BASELINE_WINDOW         ATT_APP_PLATFORM_CFGVAL(BASELINE_WINDOW)
#define DEFAULT_ATT_APP_PLATFORM_BASELINE_WINDOW 30

/** \brief Epochs a running average must have seen before it is used for BROADBAND_DROP */
#define ATT_APP_PLATFORM_BASELINE_MIN_SAMPLES         ATT_APP_PLATFORM_CFGVAL(BASELINE_MIN_SAMPLES)
#define DEFAULT_ATT_APP_PLATFORM_BASELINE_MIN_SAMPLES 10

/** \brief A satellite not tracked for more than this many epochs loses its running average */
#define ATT_APP_PLATFORM_SAT_EXPIRE_EPOCHS         ATT_APP_PLATFORM_CFGVAL(SAT_EXPIRE_EPOCHS)
#define DEFAULT_ATT_APP_PLATFORM_SAT_EXPIRE_EPOCHS 30

#endif
