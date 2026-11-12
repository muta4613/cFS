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
 * \file
 *   NMEA 0183 sentence parser and epoch assembler (pure functions).
 */

#include "att_app_nmea.h"

#include <string.h>

/************************************************************************
 * Local Macro Definitions
 ************************************************************************/

#define ATT_APP_NMEA_START_CHAR    '$'
#define ATT_APP_NMEA_CHECKSUM_CHAR '*'
#define ATT_APP_NMEA_FIELD_SEP     ','
#define ATT_APP_NMEA_DECIMAL_POINT '.'

/* "*hh" at the end of every sentence */
#define ATT_APP_NMEA_CHECKSUM_SUFFIX_LEN 3
#define ATT_APP_NMEA_HEX_RADIX           16
#define ATT_APP_NMEA_DEC_RADIX           10
#define ATT_APP_NMEA_HEX_LETTER_OFFSET   10

/* Printable ASCII range allowed inside a sentence body */
#define ATT_APP_NMEA_PRINTABLE_MIN 0x20
#define ATT_APP_NMEA_PRINTABLE_MAX 0x7E

/* Address field: two character talker ID followed by a three character type */
#define ATT_APP_NMEA_ADDRESS_LEN    5
#define ATT_APP_NMEA_TALKER_LEN     2
#define ATT_APP_NMEA_TYPE_LEN       3
#define ATT_APP_NMEA_TYPE_GGA       "GGA"
#define ATT_APP_NMEA_TYPE_GSV       "GSV"
#define ATT_APP_NMEA_ADDRESS_OFFSET 1 /* address starts right after '$' */

/* Most fields any supported sentence may have (GSV: 4 + 4 * 4 + signal ID) */
#define ATT_APP_NMEA_MAX_FIELDS 24

/* Longest run of digits accepted in one number, keeps the arithmetic far from overflow */
#define ATT_APP_NMEA_MAX_DIGITS 9

/* GGA field positions */
#define ATT_APP_NMEA_GGA_NUM_FIELDS  15
#define ATT_APP_NMEA_GGA_F_TIME      1
#define ATT_APP_NMEA_GGA_F_LAT       2
#define ATT_APP_NMEA_GGA_F_LAT_HEMI  3
#define ATT_APP_NMEA_GGA_F_LON       4
#define ATT_APP_NMEA_GGA_F_LON_HEMI  5
#define ATT_APP_NMEA_GGA_F_QUALITY   6
#define ATT_APP_NMEA_GGA_F_SATS_USED 7
#define ATT_APP_NMEA_GGA_F_ALTITUDE  9

/* GGA time field is hhmmss or hhmmss.ss */
#define ATT_APP_NMEA_TIME_MIN_LEN      6
#define ATT_APP_NMEA_TIME_HOUR_POS     0
#define ATT_APP_NMEA_TIME_MINUTE_POS   2
#define ATT_APP_NMEA_TIME_SECOND_POS   4
#define ATT_APP_NMEA_TIME_FRACTION_POS 7
#define ATT_APP_NMEA_TIME_PART_LEN     2
#define ATT_APP_NMEA_MAX_HOUR          23
#define ATT_APP_NMEA_MAX_MINUTE        59
#define ATT_APP_NMEA_MAX_SECOND        60 /* leap second */

/* Angles are sent as [d]ddmm.mmmm */
#define ATT_APP_NMEA_ANGLE_DEG_DIVISOR 100.0
#define ATT_APP_NMEA_MINUTES_PER_DEG   60.0
#define ATT_APP_NMEA_MAX_LATITUDE      90.0
#define ATT_APP_NMEA_MAX_LONGITUDE     180.0
#define ATT_APP_NMEA_MAX_FIX_QUALITY   8
#define ATT_APP_NMEA_MAX_ALTITUDE_M    100000.0

/* GSV field positions */
#define ATT_APP_NMEA_GSV_HEADER_FIELDS  4
#define ATT_APP_NMEA_GSV_FIELDS_PER_SAT 4
#define ATT_APP_NMEA_GSV_F_TOTAL        1
#define ATT_APP_NMEA_GSV_F_MSGNUM       2
#define ATT_APP_NMEA_GSV_F_IN_VIEW      3
#define ATT_APP_NMEA_GSV_SAT_PRN        0
#define ATT_APP_NMEA_GSV_SAT_ELEV       1
#define ATT_APP_NMEA_GSV_SAT_AZIM       2
#define ATT_APP_NMEA_GSV_SAT_CNO        3
#define ATT_APP_NMEA_GSV_MAX_MSGS       9
#define ATT_APP_NMEA_MAX_ELEVATION      90
#define ATT_APP_NMEA_MAX_AZIMUTH        359
#define ATT_APP_NMEA_MAX_CNO            99
#define ATT_APP_NMEA_MAX_UINT8          255

/************************************************************************
 * Local Type Definitions
 ************************************************************************/

/* One comma separated field: a view into the caller's buffer, never copied */
typedef struct
{
    const char *Ptr;
    size_t      Len;
} ATT_APP_NMEA_Field_t;

typedef struct
{
    ATT_APP_NMEA_Field_t Fields[ATT_APP_NMEA_MAX_FIELDS];
    size_t               NumFields;
} ATT_APP_NMEA_FieldList_t;

/************************************************************************
 * Local Functions
 ************************************************************************/

/*----------------------------------------------------------------
 * Value of one hex digit, or -1 if the character is not a hex digit
 *-----------------------------------------------------------------*/
static int32 ATT_APP_NMEA_HexValue(char Ch)
{
    int32 Value = -1;

    if (Ch >= '0' && Ch <= '9')
    {
        Value = Ch - '0';
    }
    else if (Ch >= 'A' && Ch <= 'F')
    {
        Value = Ch - 'A' + ATT_APP_NMEA_HEX_LETTER_OFFSET;
    }
    else if (Ch >= 'a' && Ch <= 'f')
    {
        Value = Ch - 'a' + ATT_APP_NMEA_HEX_LETTER_OFFSET;
    }

    return Value;
}

/*----------------------------------------------------------------
 * Frame one sentence and check its checksum.
 *
 * On success *Body points at the first character after '$' and
 * *BodyLength is the number of characters before '*'.
 *-----------------------------------------------------------------*/
static ATT_APP_NMEA_Status_t
ATT_APP_NMEA_Frame(const char *Sentence, size_t Length, const char **Body, size_t *BodyLength)
{
    size_t BodyLen;
    size_t i;
    int32  HighNibble;
    int32  LowNibble;
    uint8  Computed = 0;

    if (Sentence == NULL)
    {
        return ATT_APP_NMEA_ERR_NULL;
    }

    /* Trailing line terminators are not part of the sentence */
    while (Length > 0 && (Sentence[Length - 1] == '\r' || Sentence[Length - 1] == '\n'))
    {
        --Length;
    }

    if (Length < ATT_APP_NMEA_MIN_SENTENCE_LEN || Length > ATT_APP_NMEA_MAX_SENTENCE_LEN)
    {
        return ATT_APP_NMEA_ERR_LENGTH;
    }

    if (Sentence[0] != ATT_APP_NMEA_START_CHAR
        || Sentence[Length - ATT_APP_NMEA_CHECKSUM_SUFFIX_LEN] != ATT_APP_NMEA_CHECKSUM_CHAR)
    {
        return ATT_APP_NMEA_ERR_FRAMING;
    }

    /* Body is everything between '$' and '*' */
    BodyLen = Length - ATT_APP_NMEA_CHECKSUM_SUFFIX_LEN - 1;

    for (i = 0; i < BodyLen; ++i)
    {
        char Ch = Sentence[1 + i];

        if (Ch < ATT_APP_NMEA_PRINTABLE_MIN || Ch > ATT_APP_NMEA_PRINTABLE_MAX || Ch == ATT_APP_NMEA_START_CHAR
            || Ch == ATT_APP_NMEA_CHECKSUM_CHAR)
        {
            return ATT_APP_NMEA_ERR_FRAMING;
        }

        Computed ^= (uint8)Ch;
    }

    HighNibble = ATT_APP_NMEA_HexValue(Sentence[Length - 2]);
    LowNibble  = ATT_APP_NMEA_HexValue(Sentence[Length - 1]);

    if (HighNibble < 0 || LowNibble < 0)
    {
        return ATT_APP_NMEA_ERR_FRAMING;
    }

    if (Computed != (uint8)(HighNibble * ATT_APP_NMEA_HEX_RADIX + LowNibble))
    {
        return ATT_APP_NMEA_ERR_CHECKSUM;
    }

    *Body       = &Sentence[1];
    *BodyLength = BodyLen;

    return ATT_APP_NMEA_OK;
}

/*----------------------------------------------------------------
 * Split a sentence body into comma separated fields.
 * Empty fields are kept (Len == 0) so positions stay meaningful.
 *-----------------------------------------------------------------*/
static ATT_APP_NMEA_Status_t ATT_APP_NMEA_Split(const char *Body, size_t BodyLength, ATT_APP_NMEA_FieldList_t *List)
{
    size_t Start = 0;
    size_t i;

    List->NumFields = 0;

    /* i == BodyLength stands for the end of the last field */
    for (i = 0; i <= BodyLength; ++i)
    {
        if (i == BodyLength || Body[i] == ATT_APP_NMEA_FIELD_SEP)
        {
            if (List->NumFields >= ATT_APP_NMEA_MAX_FIELDS)
            {
                return ATT_APP_NMEA_ERR_FIELD_COUNT;
            }

            List->Fields[List->NumFields].Ptr = &Body[Start];
            List->Fields[List->NumFields].Len = i - Start;
            ++List->NumFields;
            Start = i + 1;
        }
    }

    return ATT_APP_NMEA_OK;
}

/*----------------------------------------------------------------
 * Parse an unsigned decimal integer made only of digits
 *-----------------------------------------------------------------*/
static ATT_APP_NMEA_Status_t ATT_APP_NMEA_ParseUint(const char *Ptr, size_t Len, uint32 MaxValue, uint32 *Value)
{
    uint32 Result = 0;
    size_t i;

    if (Len == 0 || Len > ATT_APP_NMEA_MAX_DIGITS)
    {
        return ATT_APP_NMEA_ERR_FIELD;
    }

    for (i = 0; i < Len; ++i)
    {
        if (Ptr[i] < '0' || Ptr[i] > '9')
        {
            return ATT_APP_NMEA_ERR_FIELD;
        }

        Result = Result * ATT_APP_NMEA_DEC_RADIX + (uint32)(Ptr[i] - '0');
    }

    if (Result > MaxValue)
    {
        return ATT_APP_NMEA_ERR_RANGE;
    }

    *Value = Result;

    return ATT_APP_NMEA_OK;
}

/*----------------------------------------------------------------
 * Like ATT_APP_NMEA_ParseUint, but an empty field is legal and yields
 * DefaultValue.
 *-----------------------------------------------------------------*/
static ATT_APP_NMEA_Status_t
ATT_APP_NMEA_ParseOptionalUint(const ATT_APP_NMEA_Field_t *Field, uint32 MaxValue, uint32 DefaultValue, uint32 *Value)
{
    if (Field->Len == 0)
    {
        *Value = DefaultValue;
        return ATT_APP_NMEA_OK;
    }

    return ATT_APP_NMEA_ParseUint(Field->Ptr, Field->Len, MaxValue, Value);
}

/*----------------------------------------------------------------
 * Parse a decimal number: [-]digits[.digits]
 *-----------------------------------------------------------------*/
static ATT_APP_NMEA_Status_t ATT_APP_NMEA_ParseDecimal(const ATT_APP_NMEA_Field_t *Field, double *Value)
{
    double Result     = 0.0;
    double Scale      = 1.0;
    bool   Negative   = false;
    bool   SeenPoint  = false;
    size_t NumDigits  = 0;
    size_t FracDigits = 0;
    size_t i          = 0;

    if (Field->Len > 0 && Field->Ptr[0] == '-')
    {
        Negative = true;
        i        = 1;
    }

    for (; i < Field->Len; ++i)
    {
        char Ch = Field->Ptr[i];

        if (Ch == ATT_APP_NMEA_DECIMAL_POINT && !SeenPoint)
        {
            SeenPoint = true;
        }
        else if (Ch >= '0' && Ch <= '9')
        {
            if (SeenPoint)
            {
                ++FracDigits;
                Scale *= ATT_APP_NMEA_DEC_RADIX;
            }
            else
            {
                ++NumDigits;
            }

            if (NumDigits > ATT_APP_NMEA_MAX_DIGITS || FracDigits > ATT_APP_NMEA_MAX_DIGITS)
            {
                return ATT_APP_NMEA_ERR_FIELD;
            }

            Result = Result * ATT_APP_NMEA_DEC_RADIX + (double)(Ch - '0');
        }
        else
        {
            return ATT_APP_NMEA_ERR_FIELD;
        }
    }

    /* At least one digit before the decimal point is required */
    if (NumDigits == 0)
    {
        return ATT_APP_NMEA_ERR_FIELD;
    }

    Result /= Scale;
    *Value  = Negative ? -Result : Result;

    return ATT_APP_NMEA_OK;
}

/*----------------------------------------------------------------
 * Parse a [d]ddmm.mmmm angle and its hemisphere letter into signed
 * decimal degrees.
 *-----------------------------------------------------------------*/
static ATT_APP_NMEA_Status_t ATT_APP_NMEA_ParseAngle(const ATT_APP_NMEA_Field_t *Angle,
                                                     const ATT_APP_NMEA_Field_t *Hemisphere,
                                                     char                        PositiveHemi,
                                                     char                        NegativeHemi,
                                                     double                      MaxDegrees,
                                                     double                     *Degrees)
{
    ATT_APP_NMEA_Status_t Status;
    double                Raw = 0.0;
    double                WholeDegrees;
    double                Minutes;
    double                Result;

    Status = ATT_APP_NMEA_ParseDecimal(Angle, &Raw);
    if (Status != ATT_APP_NMEA_OK)
    {
        return Status;
    }

    if (Raw < 0.0 || Hemisphere->Len != 1 || (Hemisphere->Ptr[0] != PositiveHemi && Hemisphere->Ptr[0] != NegativeHemi))
    {
        return ATT_APP_NMEA_ERR_FIELD;
    }

    /* Truncation toward zero is the intent: Raw is known to be non-negative and small */
    WholeDegrees = (double)(uint32)(Raw / ATT_APP_NMEA_ANGLE_DEG_DIVISOR);
    Minutes      = Raw - WholeDegrees * ATT_APP_NMEA_ANGLE_DEG_DIVISOR;
    Result       = WholeDegrees + Minutes / ATT_APP_NMEA_MINUTES_PER_DEG;

    if (Minutes >= ATT_APP_NMEA_MINUTES_PER_DEG || Result > MaxDegrees)
    {
        return ATT_APP_NMEA_ERR_RANGE;
    }

    *Degrees = (Hemisphere->Ptr[0] == NegativeHemi) ? -Result : Result;

    return ATT_APP_NMEA_OK;
}

/*----------------------------------------------------------------
 * Parse the hhmmss[.ss] UTC time field
 *-----------------------------------------------------------------*/
static ATT_APP_NMEA_Status_t ATT_APP_NMEA_ParseTime(const ATT_APP_NMEA_Field_t *Field, ATT_APP_NMEA_Gga_t *Gga)
{
    ATT_APP_NMEA_Status_t Status;
    uint32                Hour       = 0;
    uint32                Minute     = 0;
    uint32                Second     = 0;
    uint32                Centisec   = 0;
    uint32                FirstDigit = 0;
    size_t                FracLen;

    if (Field->Len < ATT_APP_NMEA_TIME_MIN_LEN)
    {
        return ATT_APP_NMEA_ERR_FIELD;
    }

    Status = ATT_APP_NMEA_ParseUint(&Field->Ptr[ATT_APP_NMEA_TIME_HOUR_POS],
                                    ATT_APP_NMEA_TIME_PART_LEN,
                                    ATT_APP_NMEA_MAX_HOUR,
                                    &Hour);
    if (Status == ATT_APP_NMEA_OK)
    {
        Status = ATT_APP_NMEA_ParseUint(&Field->Ptr[ATT_APP_NMEA_TIME_MINUTE_POS],
                                        ATT_APP_NMEA_TIME_PART_LEN,
                                        ATT_APP_NMEA_MAX_MINUTE,
                                        &Minute);
    }
    if (Status == ATT_APP_NMEA_OK)
    {
        Status = ATT_APP_NMEA_ParseUint(&Field->Ptr[ATT_APP_NMEA_TIME_SECOND_POS],
                                        ATT_APP_NMEA_TIME_PART_LEN,
                                        ATT_APP_NMEA_MAX_SECOND,
                                        &Second);
    }

    /* Optional fraction: ".s", ".ss" or longer; only hundredths are kept */
    if (Status == ATT_APP_NMEA_OK && Field->Len > ATT_APP_NMEA_TIME_MIN_LEN)
    {
        if (Field->Ptr[ATT_APP_NMEA_TIME_MIN_LEN] != ATT_APP_NMEA_DECIMAL_POINT
            || Field->Len <= ATT_APP_NMEA_TIME_FRACTION_POS)
        {
            return ATT_APP_NMEA_ERR_FIELD;
        }

        FracLen = Field->Len - ATT_APP_NMEA_TIME_FRACTION_POS;

        /* Every fraction character must be a digit, even those beyond hundredths */
        Status = ATT_APP_NMEA_ParseUint(&Field->Ptr[ATT_APP_NMEA_TIME_FRACTION_POS], FracLen, UINT32_MAX, &Centisec);
        if (Status == ATT_APP_NMEA_OK)
        {
            FirstDigit = (uint32)(Field->Ptr[ATT_APP_NMEA_TIME_FRACTION_POS] - '0');
            Centisec   = FirstDigit * ATT_APP_NMEA_DEC_RADIX;
            if (FracLen >= ATT_APP_NMEA_TIME_PART_LEN)
            {
                Centisec += (uint32)(Field->Ptr[ATT_APP_NMEA_TIME_FRACTION_POS + 1] - '0');
            }
        }
    }

    if (Status == ATT_APP_NMEA_OK)
    {
        Gga->UtcHour        = (uint8)Hour;
        Gga->UtcMinute      = (uint8)Minute;
        Gga->UtcSecond      = (uint8)Second;
        Gga->UtcCentisecond = (uint8)Centisec;
    }

    return Status;
}

/*----------------------------------------------------------------
 * Frame, checksum and split a sentence, and confirm its three
 * character type (the talker ID is not checked).
 *-----------------------------------------------------------------*/
static ATT_APP_NMEA_Status_t
ATT_APP_NMEA_Tokenize(const char *Sentence, size_t Length, const char *ExpectedType, ATT_APP_NMEA_FieldList_t *List)
{
    ATT_APP_NMEA_Status_t Status;
    const char           *Body       = NULL;
    size_t                BodyLength = 0;

    Status = ATT_APP_NMEA_Frame(Sentence, Length, &Body, &BodyLength);
    if (Status != ATT_APP_NMEA_OK)
    {
        return Status;
    }

    Status = ATT_APP_NMEA_Split(Body, BodyLength, List);
    if (Status != ATT_APP_NMEA_OK)
    {
        return Status;
    }

    if (List->Fields[0].Len != ATT_APP_NMEA_ADDRESS_LEN
        || memcmp(&List->Fields[0].Ptr[ATT_APP_NMEA_TALKER_LEN], ExpectedType, ATT_APP_NMEA_TYPE_LEN) != 0)
    {
        return ATT_APP_NMEA_ERR_TYPE;
    }

    return ATT_APP_NMEA_OK;
}

/************************************************************************
 * Public Functions
 ************************************************************************/

/*----------------------------------------------------------------
 *
 * Purpose: Implemented per public API
 *          See description in header file for argument/return detail
 *
 *-----------------------------------------------------------------*/
ATT_APP_NMEA_Status_t ATT_APP_NMEA_VerifyChecksum(const char *Sentence, size_t Length)
{
    const char *Body       = NULL;
    size_t      BodyLength = 0;

    return ATT_APP_NMEA_Frame(Sentence, Length, &Body, &BodyLength);
}

/*----------------------------------------------------------------
 *
 * Purpose: Implemented per public API
 *          See description in header file for argument/return detail
 *
 *-----------------------------------------------------------------*/
ATT_APP_NMEA_SentenceType_t ATT_APP_NMEA_GetSentenceType(const char *Sentence, size_t Length)
{
    ATT_APP_NMEA_SentenceType_t Type = ATT_APP_NMEA_SentenceType_INVALID;
    const char                 *TypePtr;

    /* Needs "$" + 5 character address followed by ',' or '*' */
    if (Sentence != NULL && Length > ATT_APP_NMEA_ADDRESS_OFFSET + ATT_APP_NMEA_ADDRESS_LEN
        && Sentence[0] == ATT_APP_NMEA_START_CHAR)
    {
        char Terminator = Sentence[ATT_APP_NMEA_ADDRESS_OFFSET + ATT_APP_NMEA_ADDRESS_LEN];

        if (Terminator == ATT_APP_NMEA_FIELD_SEP || Terminator == ATT_APP_NMEA_CHECKSUM_CHAR)
        {
            TypePtr = &Sentence[ATT_APP_NMEA_ADDRESS_OFFSET + ATT_APP_NMEA_TALKER_LEN];

            if (memcmp(TypePtr, ATT_APP_NMEA_TYPE_GGA, ATT_APP_NMEA_TYPE_LEN) == 0)
            {
                Type = ATT_APP_NMEA_SentenceType_GGA;
            }
            else if (memcmp(TypePtr, ATT_APP_NMEA_TYPE_GSV, ATT_APP_NMEA_TYPE_LEN) == 0)
            {
                Type = ATT_APP_NMEA_SentenceType_GSV;
            }
            else
            {
                Type = ATT_APP_NMEA_SentenceType_OTHER;
            }
        }
    }

    return Type;
}

/*----------------------------------------------------------------
 *
 * Purpose: Implemented per public API
 *          See description in header file for argument/return detail
 *
 *-----------------------------------------------------------------*/
ATT_APP_NMEA_Status_t ATT_APP_NMEA_ParseGga(const char *Sentence, size_t Length, ATT_APP_NMEA_Gga_t *Gga)
{
    ATT_APP_NMEA_Status_t    Status;
    ATT_APP_NMEA_FieldList_t List;
    ATT_APP_NMEA_Gga_t       Result;
    uint32                   Quality  = 0;
    uint32                   SatsUsed = 0;
    double                   Altitude = 0.0;

    if (Gga == NULL)
    {
        return ATT_APP_NMEA_ERR_NULL;
    }

    memset(Gga, 0, sizeof(*Gga));
    memset(&Result, 0, sizeof(Result));

    Status = ATT_APP_NMEA_Tokenize(Sentence, Length, ATT_APP_NMEA_TYPE_GGA, &List);

    if (Status == ATT_APP_NMEA_OK && List.NumFields != ATT_APP_NMEA_GGA_NUM_FIELDS)
    {
        Status = ATT_APP_NMEA_ERR_FIELD_COUNT;
    }

    if (Status == ATT_APP_NMEA_OK)
    {
        Status = ATT_APP_NMEA_ParseTime(&List.Fields[ATT_APP_NMEA_GGA_F_TIME], &Result);
    }

    if (Status == ATT_APP_NMEA_OK)
    {
        Status = ATT_APP_NMEA_ParseUint(List.Fields[ATT_APP_NMEA_GGA_F_QUALITY].Ptr,
                                        List.Fields[ATT_APP_NMEA_GGA_F_QUALITY].Len,
                                        ATT_APP_NMEA_MAX_FIX_QUALITY,
                                        &Quality);
    }

    if (Status == ATT_APP_NMEA_OK)
    {
        Status = ATT_APP_NMEA_ParseUint(List.Fields[ATT_APP_NMEA_GGA_F_SATS_USED].Ptr,
                                        List.Fields[ATT_APP_NMEA_GGA_F_SATS_USED].Len,
                                        ATT_APP_NMEA_MAX_UINT8,
                                        &SatsUsed);
    }

    /* Position is only required, and only trusted, when the receiver reports a fix */
    if (Status == ATT_APP_NMEA_OK && Quality != ATT_APP_NMEA_FIX_QUALITY_NONE)
    {
        Status = ATT_APP_NMEA_ParseAngle(&List.Fields[ATT_APP_NMEA_GGA_F_LAT],
                                         &List.Fields[ATT_APP_NMEA_GGA_F_LAT_HEMI],
                                         'N',
                                         'S',
                                         ATT_APP_NMEA_MAX_LATITUDE,
                                         &Result.Latitude);

        if (Status == ATT_APP_NMEA_OK)
        {
            Status = ATT_APP_NMEA_ParseAngle(&List.Fields[ATT_APP_NMEA_GGA_F_LON],
                                             &List.Fields[ATT_APP_NMEA_GGA_F_LON_HEMI],
                                             'E',
                                             'W',
                                             ATT_APP_NMEA_MAX_LONGITUDE,
                                             &Result.Longitude);
        }

        if (Status == ATT_APP_NMEA_OK)
        {
            Status = ATT_APP_NMEA_ParseDecimal(&List.Fields[ATT_APP_NMEA_GGA_F_ALTITUDE], &Altitude);
        }

        if (Status == ATT_APP_NMEA_OK
            && (Altitude > ATT_APP_NMEA_MAX_ALTITUDE_M || Altitude < -ATT_APP_NMEA_MAX_ALTITUDE_M))
        {
            Status = ATT_APP_NMEA_ERR_RANGE;
        }

        Result.Altitude = (float)Altitude;
    }

    if (Status == ATT_APP_NMEA_OK)
    {
        Result.FixQuality = (uint8)Quality;
        Result.SatsUsed   = (uint8)SatsUsed;
        *Gga              = Result;
    }

    return Status;
}

/*----------------------------------------------------------------
 *
 * Purpose: Implemented per public API
 *          See description in header file for argument/return detail
 *
 *-----------------------------------------------------------------*/
ATT_APP_NMEA_Status_t ATT_APP_NMEA_ParseGsv(const char *Sentence, size_t Length, ATT_APP_NMEA_Gsv_t *Gsv)
{
    ATT_APP_NMEA_Status_t       Status;
    ATT_APP_NMEA_FieldList_t    List;
    ATT_APP_NMEA_Gsv_t          Result;
    const ATT_APP_NMEA_Field_t *SatFields;
    size_t                      SatFieldCount;
    size_t                      NumSats = 0;
    size_t                      i;
    uint32                      Total  = 0;
    uint32                      MsgNum = 0;
    uint32                      InView = 0;
    uint32                      Prn    = 0;
    uint32                      Elev   = 0;
    uint32                      Azim   = 0;
    uint32                      Cno    = 0;

    if (Gsv == NULL)
    {
        return ATT_APP_NMEA_ERR_NULL;
    }

    memset(Gsv, 0, sizeof(*Gsv));
    memset(&Result, 0, sizeof(Result));

    Status = ATT_APP_NMEA_Tokenize(Sentence, Length, ATT_APP_NMEA_TYPE_GSV, &List);

    if (Status == ATT_APP_NMEA_OK)
    {
        if (List.NumFields < ATT_APP_NMEA_GSV_HEADER_FIELDS)
        {
            Status = ATT_APP_NMEA_ERR_FIELD_COUNT;
        }
        else
        {
            /*
             * After the header come complete groups of four fields per satellite.
             * NMEA 4.10 receivers append one extra "signal ID" field, which is ignored.
             */
            SatFieldCount = List.NumFields - ATT_APP_NMEA_GSV_HEADER_FIELDS;
            NumSats       = SatFieldCount / ATT_APP_NMEA_GSV_FIELDS_PER_SAT;

            if ((SatFieldCount % ATT_APP_NMEA_GSV_FIELDS_PER_SAT) > 1 || NumSats > ATT_APP_NMEA_SATS_PER_GSV)
            {
                Status = ATT_APP_NMEA_ERR_FIELD_COUNT;
            }
        }
    }

    if (Status == ATT_APP_NMEA_OK)
    {
        Status = ATT_APP_NMEA_ParseUint(List.Fields[ATT_APP_NMEA_GSV_F_TOTAL].Ptr,
                                        List.Fields[ATT_APP_NMEA_GSV_F_TOTAL].Len,
                                        ATT_APP_NMEA_GSV_MAX_MSGS,
                                        &Total);
    }

    if (Status == ATT_APP_NMEA_OK)
    {
        Status = ATT_APP_NMEA_ParseUint(List.Fields[ATT_APP_NMEA_GSV_F_MSGNUM].Ptr,
                                        List.Fields[ATT_APP_NMEA_GSV_F_MSGNUM].Len,
                                        ATT_APP_NMEA_GSV_MAX_MSGS,
                                        &MsgNum);
    }

    if (Status == ATT_APP_NMEA_OK && (Total == 0 || MsgNum == 0 || MsgNum > Total))
    {
        Status = ATT_APP_NMEA_ERR_RANGE;
    }

    if (Status == ATT_APP_NMEA_OK)
    {
        Status = ATT_APP_NMEA_ParseUint(List.Fields[ATT_APP_NMEA_GSV_F_IN_VIEW].Ptr,
                                        List.Fields[ATT_APP_NMEA_GSV_F_IN_VIEW].Len,
                                        ATT_APP_NMEA_MAX_UINT8,
                                        &InView);
    }

    for (i = 0; i < NumSats && Status == ATT_APP_NMEA_OK; ++i)
    {
        SatFields = &List.Fields[ATT_APP_NMEA_GSV_HEADER_FIELDS + i * ATT_APP_NMEA_GSV_FIELDS_PER_SAT];

        /* The satellite ID is the one field that can never be empty */
        Status = ATT_APP_NMEA_ParseUint(SatFields[ATT_APP_NMEA_GSV_SAT_PRN].Ptr,
                                        SatFields[ATT_APP_NMEA_GSV_SAT_PRN].Len,
                                        ATT_APP_NMEA_MAX_UINT8,
                                        &Prn);

        if (Status == ATT_APP_NMEA_OK && Prn == 0)
        {
            Status = ATT_APP_NMEA_ERR_RANGE;
        }

        if (Status == ATT_APP_NMEA_OK)
        {
            Status = ATT_APP_NMEA_ParseOptionalUint(&SatFields[ATT_APP_NMEA_GSV_SAT_ELEV],
                                                    ATT_APP_NMEA_MAX_ELEVATION,
                                                    0,
                                                    &Elev);
        }

        if (Status == ATT_APP_NMEA_OK)
        {
            Status = ATT_APP_NMEA_ParseOptionalUint(&SatFields[ATT_APP_NMEA_GSV_SAT_AZIM],
                                                    ATT_APP_NMEA_MAX_AZIMUTH,
                                                    0,
                                                    &Azim);
        }

        if (Status == ATT_APP_NMEA_OK)
        {
            Status = ATT_APP_NMEA_ParseOptionalUint(&SatFields[ATT_APP_NMEA_GSV_SAT_CNO],
                                                    ATT_APP_NMEA_MAX_CNO,
                                                    ATT_APP_NMEA_CNO_NOT_TRACKED,
                                                    &Cno);
        }

        if (Status == ATT_APP_NMEA_OK)
        {
            Result.Sats[i].Prn          = (uint8)Prn;
            Result.Sats[i].ElevationDeg = (uint8)Elev;
            Result.Sats[i].AzimuthDeg   = (uint16)Azim;
            Result.Sats[i].Cno          = (uint8)Cno;
        }
    }

    if (Status == ATT_APP_NMEA_OK)
    {
        Result.TotalMsgs  = (uint8)Total;
        Result.MsgNum     = (uint8)MsgNum;
        Result.SatsInView = (uint8)InView;
        Result.NumSats    = (uint8)NumSats;
        *Gsv              = Result;
    }

    return Status;
}

/*----------------------------------------------------------------
 *
 * Purpose: Implemented per public API
 *          See description in header file for argument/return detail
 *
 *-----------------------------------------------------------------*/
void ATT_APP_NMEA_AssemblerInit(ATT_APP_NMEA_Assembler_t *Assembler)
{
    if (Assembler != NULL)
    {
        memset(Assembler, 0, sizeof(*Assembler));
    }
}

/*----------------------------------------------------------------
 * Merge one decoded GSV sentence into the epoch being collected
 *-----------------------------------------------------------------*/
static ATT_APP_NMEA_Status_t ATT_APP_NMEA_AssemblerAddGsv(ATT_APP_NMEA_Assembler_t *Assembler,
                                                          const ATT_APP_NMEA_Gsv_t *Gsv)
{
    ATT_APP_NMEA_Epoch_t *Pending = &Assembler->Pending;
    uint32                InView;
    uint8                 i;

    if (Gsv->MsgNum == 1)
    {
        /*
         * First sentence of a group. A multi-constellation receiver sends one
         * group per talker in the same epoch, so the totals are accumulated.
         */
        Assembler->GsvTotal = Gsv->TotalMsgs;

        InView              = (uint32)Pending->SatsInView + Gsv->SatsInView;
        Pending->SatsInView = (InView > ATT_APP_NMEA_MAX_UINT8) ? ATT_APP_NMEA_MAX_UINT8 : (uint8)InView;
    }
    else if (Gsv->MsgNum != Assembler->GsvNextNum || Gsv->TotalMsgs != Assembler->GsvTotal)
    {
        /* A sentence of the group was lost: stop collecting this group */
        Assembler->GsvNextNum = 0;
        return ATT_APP_NMEA_ERR_SEQUENCE;
    }

    for (i = 0; i < Gsv->NumSats && Pending->NumSats < ATT_APP_MISSION_MAX_SATS; ++i)
    {
        Pending->Sats[Pending->NumSats] = Gsv->Sats[i];
        ++Pending->NumSats;
    }

    Assembler->GsvNextNum = (Gsv->MsgNum < Gsv->TotalMsgs) ? (uint8)(Gsv->MsgNum + 1) : 0;

    return ATT_APP_NMEA_OK;
}

/*----------------------------------------------------------------
 *
 * Purpose: Implemented per public API
 *          See description in header file for argument/return detail
 *
 *-----------------------------------------------------------------*/
ATT_APP_NMEA_Status_t ATT_APP_NMEA_AssemblerFeed(ATT_APP_NMEA_Assembler_t *Assembler,
                                                 const char               *Sentence,
                                                 size_t                    Length,
                                                 ATT_APP_NMEA_Epoch_t     *Epoch,
                                                 bool                     *EpochReady)
{
    ATT_APP_NMEA_Status_t Status;
    ATT_APP_NMEA_Gga_t    Gga;
    ATT_APP_NMEA_Gsv_t    Gsv;

    if (Assembler == NULL || Sentence == NULL || Epoch == NULL || EpochReady == NULL)
    {
        return ATT_APP_NMEA_ERR_NULL;
    }

    *EpochReady = false;

    switch (ATT_APP_NMEA_GetSentenceType(Sentence, Length))
    {
        case ATT_APP_NMEA_SentenceType_GGA:
            Status = ATT_APP_NMEA_ParseGga(Sentence, Length, &Gga);
            if (Status == ATT_APP_NMEA_OK)
            {
                /* A new GGA closes the epoch that was being collected */
                *EpochReady = ATT_APP_NMEA_AssemblerFlush(Assembler, Epoch);

                Assembler->Pending.Gga = Gga;
                Assembler->HaveGga     = true;
            }
            break;

        case ATT_APP_NMEA_SentenceType_GSV:
            Status = ATT_APP_NMEA_ParseGsv(Sentence, Length, &Gsv);
            if (Status == ATT_APP_NMEA_OK && Assembler->HaveGga)
            {
                Status = ATT_APP_NMEA_AssemblerAddGsv(Assembler, &Gsv);
            }
            break;

        case ATT_APP_NMEA_SentenceType_OTHER:
            /* Not used, but a corrupted sentence is still worth counting */
            Status = ATT_APP_NMEA_VerifyChecksum(Sentence, Length);
            break;

        default:
            /* Cannot even be framed; let the framing check name the reason */
            Status = ATT_APP_NMEA_VerifyChecksum(Sentence, Length);
            if (Status == ATT_APP_NMEA_OK)
            {
                Status = ATT_APP_NMEA_ERR_FRAMING;
            }
            break;
    }

    return Status;
}

/*----------------------------------------------------------------
 *
 * Purpose: Implemented per public API
 *          See description in header file for argument/return detail
 *
 *-----------------------------------------------------------------*/
bool ATT_APP_NMEA_AssemblerFlush(ATT_APP_NMEA_Assembler_t *Assembler, ATT_APP_NMEA_Epoch_t *Epoch)
{
    bool HadEpoch = false;

    if (Assembler != NULL && Epoch != NULL && Assembler->HaveGga)
    {
        *Epoch   = Assembler->Pending;
        HadEpoch = true;
    }

    if (Assembler != NULL)
    {
        memset(Assembler, 0, sizeof(*Assembler));
    }

    return HadEpoch;
}
