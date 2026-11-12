/************************************************************************
 * ATT_APP - GNSS signal-health monitor for NASA cFS
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
 * NMEA 0183 sentence parser and epoch assembler for ATT_APP.
 *
 * Everything in this unit is a pure function: no cFE or OSAL calls, no
 * global state, no dynamic memory. Every input is a (pointer, length) pair
 * and is never assumed to be NUL terminated, so the unit can be tested and
 * fuzzed on its own.
 */

#ifndef ATT_APP_NMEA_H
#define ATT_APP_NMEA_H

#include "common_types.h"
#include "att_app_interface_cfg.h"

/************************************************************************
 * Macro Definitions
 ************************************************************************/

/** \brief Longest legal sentence: '$' through the checksum, without CR/LF (NMEA 0183 limit is 82 with CR/LF) */
#define ATT_APP_NMEA_MAX_SENTENCE_LEN 80

/** \brief Shortest sentence that can be framed: "$" + 5 char address + "*hh" */
#define ATT_APP_NMEA_MIN_SENTENCE_LEN 9

/** \brief Satellites described by one GSV sentence */
#define ATT_APP_NMEA_SATS_PER_GSV 4

/** \brief C/N0 value stored for a satellite that is in view but not tracked (empty SNR field) */
#define ATT_APP_NMEA_CNO_NOT_TRACKED 0

/** \brief GGA fix quality value meaning "no position fix" */
#define ATT_APP_NMEA_FIX_QUALITY_NONE 0

/************************************************************************
 * Type Definitions
 ************************************************************************/

/** \brief Parser result codes */
typedef enum
{
    ATT_APP_NMEA_OK = 0,
    ATT_APP_NMEA_ERR_NULL,        /**< NULL pointer argument */
    ATT_APP_NMEA_ERR_LENGTH,      /**< Sentence is empty, too short or too long */
    ATT_APP_NMEA_ERR_FRAMING,     /**< Missing '$', missing "*hh", or non-printable character */
    ATT_APP_NMEA_ERR_CHECKSUM,    /**< Checksum does not match the sentence body */
    ATT_APP_NMEA_ERR_TYPE,        /**< Sentence is not of the type the caller asked to parse */
    ATT_APP_NMEA_ERR_FIELD_COUNT, /**< Wrong number of comma separated fields */
    ATT_APP_NMEA_ERR_FIELD,       /**< A required field is empty or not a valid number */
    ATT_APP_NMEA_ERR_RANGE,       /**< A numeric field is outside its legal range */
    ATT_APP_NMEA_ERR_SEQUENCE     /**< GSV sentence is not the next one of its group */
} ATT_APP_NMEA_Status_t;

/** \brief Sentence types this parser distinguishes */
typedef enum
{
    ATT_APP_NMEA_SentenceType_INVALID = 0, /**< Could not be framed at all */
    ATT_APP_NMEA_SentenceType_GGA,         /**< Fix data */
    ATT_APP_NMEA_SentenceType_GSV,         /**< Satellites in view */
    ATT_APP_NMEA_SentenceType_OTHER        /**< Well framed, but not used by ATT_APP */
} ATT_APP_NMEA_SentenceType_t;

/** \brief Decoded GGA sentence */
typedef struct
{
    double Latitude;       /**< Decimal degrees, north positive (0 when no fix) */
    double Longitude;      /**< Decimal degrees, east positive (0 when no fix) */
    float  Altitude;       /**< Metres above mean sea level (0 when no fix) */
    uint8  UtcHour;        /**< 0-23 */
    uint8  UtcMinute;      /**< 0-59 */
    uint8  UtcSecond;      /**< 0-60 (60 only during a leap second) */
    uint8  UtcCentisecond; /**< 0-99 */
    uint8  FixQuality;     /**< 0 = no fix, 1 = GPS, 2 = DGPS, ... */
    uint8  SatsUsed;       /**< Satellites used in the position solution */
} ATT_APP_NMEA_Gga_t;

/** \brief One satellite from a GSV sentence */
typedef struct
{
    uint16 AzimuthDeg;   /**< 0-359, 0 if not reported */
    uint8  Prn;          /**< Satellite ID */
    uint8  ElevationDeg; /**< 0-90, 0 if not reported */
    uint8  Cno;          /**< Carrier-to-noise density in dB-Hz, #ATT_APP_NMEA_CNO_NOT_TRACKED if not tracked */
} ATT_APP_NMEA_Sat_t;

/** \brief Decoded GSV sentence (one part of a multi-sentence group) */
typedef struct
{
    uint8              TotalMsgs;  /**< Number of sentences in this group */
    uint8              MsgNum;     /**< Position of this sentence in the group, starting at 1 */
    uint8              SatsInView; /**< Satellites in view according to the receiver */
    uint8              NumSats;    /**< Satellites described by this sentence (0-4) */
    ATT_APP_NMEA_Sat_t Sats[ATT_APP_NMEA_SATS_PER_GSV];
} ATT_APP_NMEA_Gsv_t;

/** \brief One complete measurement epoch: a GGA plus the GSV groups that followed it */
typedef struct
{
    ATT_APP_NMEA_Gga_t Gga;
    uint8              SatsInView; /**< As reported by the receiver, may exceed NumSats */
    uint8              NumSats;    /**< Valid entries in Sats[] */
    ATT_APP_NMEA_Sat_t Sats[ATT_APP_MISSION_MAX_SATS];
} ATT_APP_NMEA_Epoch_t;

/** \brief Working state of the epoch assembler */
typedef struct
{
    ATT_APP_NMEA_Epoch_t Pending;    /**< Epoch being collected */
    bool                 HaveGga;    /**< Pending holds a GGA */
    uint8                GsvTotal;   /**< Sentences in the GSV group being collected */
    uint8                GsvNextNum; /**< Next GSV sentence number expected, 0 when no group is open */
} ATT_APP_NMEA_Assembler_t;

/************************************************************************
 * Function Prototypes
 ************************************************************************/

/**
 * \brief Check framing and checksum of one sentence
 *
 * A sentence is '$', a body of printable ASCII, '*' and two hex digits.
 * Trailing CR and/or LF characters are accepted and ignored.
 *
 * \param[in] Sentence Start of the sentence, not necessarily NUL terminated
 * \param[in] Length   Number of characters available at Sentence
 *
 * \return #ATT_APP_NMEA_OK if the sentence is well framed and the checksum matches
 */
ATT_APP_NMEA_Status_t ATT_APP_NMEA_VerifyChecksum(const char *Sentence, size_t Length);

/**
 * \brief Identify a sentence by its address field, whatever the talker ID
 *
 * Does not verify the checksum.
 */
ATT_APP_NMEA_SentenceType_t ATT_APP_NMEA_GetSentenceType(const char *Sentence, size_t Length);

/**
 * \brief Parse a GGA (fix data) sentence
 *
 * When the fix quality is 0 the position fields may be empty, as real
 * receivers send them; latitude, longitude and altitude are then set to 0.
 *
 * \param[in]  Sentence Start of the sentence
 * \param[in]  Length   Number of characters available at Sentence
 * \param[out] Gga      Decoded data, zeroed unless #ATT_APP_NMEA_OK is returned
 */
ATT_APP_NMEA_Status_t ATT_APP_NMEA_ParseGga(const char *Sentence, size_t Length, ATT_APP_NMEA_Gga_t *Gga);

/**
 * \brief Parse one GSV (satellites in view) sentence
 *
 * Elevation, azimuth and C/N0 may be empty; a satellite with an empty C/N0
 * is in view but not tracked and is reported with #ATT_APP_NMEA_CNO_NOT_TRACKED.
 *
 * \param[in]  Sentence Start of the sentence
 * \param[in]  Length   Number of characters available at Sentence
 * \param[out] Gsv      Decoded data, zeroed unless #ATT_APP_NMEA_OK is returned
 */
ATT_APP_NMEA_Status_t ATT_APP_NMEA_ParseGsv(const char *Sentence, size_t Length, ATT_APP_NMEA_Gsv_t *Gsv);

/**
 * \brief Reset an epoch assembler to "nothing collected"
 */
void ATT_APP_NMEA_AssemblerInit(ATT_APP_NMEA_Assembler_t *Assembler);

/**
 * \brief Give the assembler the next sentence from the receiver
 *
 * A GGA opens a new epoch; the GSV sentences that follow are merged into
 * it. The epoch being collected is handed out when the next GGA arrives,
 * so an epoch is only ever reported once every sentence of it was seen.
 *
 * \param[in,out] Assembler  Assembler state
 * \param[in]     Sentence   Start of the sentence
 * \param[in]     Length     Number of characters available at Sentence
 * \param[out]    Epoch      Receives the finished epoch when *EpochReady is true
 * \param[out]    EpochReady Set true if this sentence completed an epoch
 *
 * \return #ATT_APP_NMEA_OK, or the reason the sentence was rejected. A
 *         rejected sentence never changes the assembler's epoch data.
 */
ATT_APP_NMEA_Status_t ATT_APP_NMEA_AssemblerFeed(ATT_APP_NMEA_Assembler_t *Assembler,
                                                 const char               *Sentence,
                                                 size_t                    Length,
                                                 ATT_APP_NMEA_Epoch_t     *Epoch,
                                                 bool                     *EpochReady);

/**
 * \brief Hand out the epoch being collected, if any (used at end of input)
 *
 * \return true if *Epoch was filled in
 */
bool ATT_APP_NMEA_AssemblerFlush(ATT_APP_NMEA_Assembler_t *Assembler, ATT_APP_NMEA_Epoch_t *Epoch);

#endif /* ATT_APP_NMEA_H */
