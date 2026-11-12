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
 *   Specification for the ATT_APP command and telemetry
 *   message payload and constant definitions.
 */
#ifndef DEFAULT_ATT_APP_MSGDEFS_H
#define DEFAULT_ATT_APP_MSGDEFS_H

#include "common_types.h"
#include "att_app_fcncodes.h"
#include "att_app_interface_cfg.h"

typedef struct ATT_APP_DisplayParam_Payload
{
    uint32 ValU32;                                 /**< 32 bit unsigned integer value */
    int16  ValI16;                                 /**< 16 bit signed integer value */
    char   ValStr[ATT_APP_MISSION_STRING_VAL_LEN]; /**< An example string */
} ATT_APP_DisplayParam_Payload_t;

/*************************************************************************/

/**
 * \name Bits of the HealthFlags field in the NAV telemetry packet
 * \{
 */
#define ATT_APP_HEALTH_FLAG_NO_FIX      0x01 /**< \brief Receiver reports no position fix */
#define ATT_APP_HEALTH_FLAG_LOW_CNO_SAT 0x02 /**< \brief At least one tracked satellite is below the C/N0 threshold */
#define ATT_APP_HEALTH_FLAG_BROADBAND_DROP \
    0x04 /**< \brief C/N0 fell on most satellites at once (possible interference) */
/** \} */

/**
 * \name Values of the InputState field in the housekeeping packet
 * \{
 */
#define ATT_APP_INPUT_STATE_CLOSED 0 /**< \brief GNSS input could not be opened, or was closed after a read error */
#define ATT_APP_INPUT_STATE_OPEN   1 /**< \brief GNSS input is open and being read */
/** \} */

/*
** Type definition (ATT_APP housekeeping)
**
** 16 bytes, every field on its natural alignment, no implicit padding.
*/
typedef struct ATT_APP_HkTlm_Payload
{
    uint8  CommandCounter;      /**< \brief Count of accepted ground commands */
    uint8  CommandErrorCounter; /**< \brief Count of rejected ground commands */
    uint8  CnoThreshold;        /**< \brief Current single-satellite low C/N0 threshold, dB-Hz */
    uint8  InputState;          /**< \brief ATT_APP_INPUT_STATE_CLOSED or ATT_APP_INPUT_STATE_OPEN */
    uint32 WakeupCounter;       /**< \brief Number of scheduler wakeups received */
    uint32 EpochCounter;        /**< \brief Number of GNSS epochs processed and published */
    uint32 ParseErrorCounter;   /**< \brief Number of NMEA sentences rejected by the parser */
} ATT_APP_HkTlm_Payload_t;

/**
 * \brief One satellite in the NAV packet
 */
typedef struct ATT_APP_SatCno
{
    uint8 Prn; /**< \brief Satellite ID, 0 for an unused array entry */
    uint8 Cno; /**< \brief Carrier-to-noise density in dB-Hz, 0 if in view but not tracked */
} ATT_APP_SatCno_t;

/*
** Type definition (ATT_APP navigation and signal-health telemetry)
**
** Laid out largest alignment first: two doubles, then 32 bit fields, then
** bytes. The fixed part is 40 bytes and the satellite array is 2 bytes per
** entry, so the payload has no implicit padding as long as
** ATT_APP_MISSION_MAX_SATS is a multiple of 4 (checked at compile time).
*/
typedef struct ATT_APP_NavTlm_Payload
{
    double Latitude;          /**< \brief Decimal degrees, north positive, 0 without a fix */
    double Longitude;         /**< \brief Decimal degrees, east positive, 0 without a fix */
    float  Altitude;          /**< \brief Metres above mean sea level, 0 without a fix */
    float  MeanCno;           /**< \brief Mean C/N0 of the tracked satellites, dB-Hz */
    uint32 EpochCounter;      /**< \brief Epochs processed, including this one */
    uint32 ParseErrorCounter; /**< \brief NMEA sentences rejected by the parser */
    uint8  UtcHour;           /**< \brief UTC time of the fix */
    uint8  UtcMinute;
    uint8  UtcSecond;
    uint8  UtcCentisecond;
    uint8  FixQuality;  /**< \brief GGA fix quality, 0 = no fix */
    uint8  SatsUsed;    /**< \brief Satellites used in the position solution */
    uint8  SatsInView;  /**< \brief Satellites in view as reported by the receiver */
    uint8  HealthFlags; /**< \brief ATT_APP_HEALTH_FLAG_* bits */
    ATT_APP_SatCno_t
        Sats[ATT_APP_MISSION_MAX_SATS]; /**< \brief First SatsInView entries are valid, up to the array size */
} ATT_APP_NavTlm_Payload_t;

#endif
