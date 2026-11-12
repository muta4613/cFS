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
 * Auto-Generated stub implementations for functions defined in att_app_nmea header
 */

#include "att_app_nmea.h"
#include "utgenstub.h"

/*
 * ----------------------------------------------------
 * Generated stub function for ATT_APP_NMEA_AssemblerFeed()
 * ----------------------------------------------------
 */
ATT_APP_NMEA_Status_t ATT_APP_NMEA_AssemblerFeed(ATT_APP_NMEA_Assembler_t *Assembler,
                                                 const char               *Sentence,
                                                 size_t                    Length,
                                                 ATT_APP_NMEA_Epoch_t     *Epoch,
                                                 bool                     *EpochReady)
{
    UT_GenStub_SetupReturnBuffer(ATT_APP_NMEA_AssemblerFeed, ATT_APP_NMEA_Status_t);

    UT_GenStub_AddParam(ATT_APP_NMEA_AssemblerFeed, ATT_APP_NMEA_Assembler_t *, Assembler);
    UT_GenStub_AddParam(ATT_APP_NMEA_AssemblerFeed, const char *, Sentence);
    UT_GenStub_AddParam(ATT_APP_NMEA_AssemblerFeed, size_t, Length);
    UT_GenStub_AddParam(ATT_APP_NMEA_AssemblerFeed, ATT_APP_NMEA_Epoch_t *, Epoch);
    UT_GenStub_AddParam(ATT_APP_NMEA_AssemblerFeed, bool *, EpochReady);

    UT_GenStub_Execute(ATT_APP_NMEA_AssemblerFeed, Basic, NULL);

    return UT_GenStub_GetReturnValue(ATT_APP_NMEA_AssemblerFeed, ATT_APP_NMEA_Status_t);
}

/*
 * ----------------------------------------------------
 * Generated stub function for ATT_APP_NMEA_AssemblerFlush()
 * ----------------------------------------------------
 */
bool ATT_APP_NMEA_AssemblerFlush(ATT_APP_NMEA_Assembler_t *Assembler, ATT_APP_NMEA_Epoch_t *Epoch)
{
    UT_GenStub_SetupReturnBuffer(ATT_APP_NMEA_AssemblerFlush, bool);

    UT_GenStub_AddParam(ATT_APP_NMEA_AssemblerFlush, ATT_APP_NMEA_Assembler_t *, Assembler);
    UT_GenStub_AddParam(ATT_APP_NMEA_AssemblerFlush, ATT_APP_NMEA_Epoch_t *, Epoch);

    UT_GenStub_Execute(ATT_APP_NMEA_AssemblerFlush, Basic, NULL);

    return UT_GenStub_GetReturnValue(ATT_APP_NMEA_AssemblerFlush, bool);
}

/*
 * ----------------------------------------------------
 * Generated stub function for ATT_APP_NMEA_AssemblerInit()
 * ----------------------------------------------------
 */
void ATT_APP_NMEA_AssemblerInit(ATT_APP_NMEA_Assembler_t *Assembler)
{
    UT_GenStub_AddParam(ATT_APP_NMEA_AssemblerInit, ATT_APP_NMEA_Assembler_t *, Assembler);

    UT_GenStub_Execute(ATT_APP_NMEA_AssemblerInit, Basic, NULL);
}

/*
 * ----------------------------------------------------
 * Generated stub function for ATT_APP_NMEA_GetSentenceType()
 * ----------------------------------------------------
 */
ATT_APP_NMEA_SentenceType_t ATT_APP_NMEA_GetSentenceType(const char *Sentence, size_t Length)
{
    UT_GenStub_SetupReturnBuffer(ATT_APP_NMEA_GetSentenceType, ATT_APP_NMEA_SentenceType_t);

    UT_GenStub_AddParam(ATT_APP_NMEA_GetSentenceType, const char *, Sentence);
    UT_GenStub_AddParam(ATT_APP_NMEA_GetSentenceType, size_t, Length);

    UT_GenStub_Execute(ATT_APP_NMEA_GetSentenceType, Basic, NULL);

    return UT_GenStub_GetReturnValue(ATT_APP_NMEA_GetSentenceType, ATT_APP_NMEA_SentenceType_t);
}

/*
 * ----------------------------------------------------
 * Generated stub function for ATT_APP_NMEA_ParseGga()
 * ----------------------------------------------------
 */
ATT_APP_NMEA_Status_t ATT_APP_NMEA_ParseGga(const char *Sentence, size_t Length, ATT_APP_NMEA_Gga_t *Gga)
{
    UT_GenStub_SetupReturnBuffer(ATT_APP_NMEA_ParseGga, ATT_APP_NMEA_Status_t);

    UT_GenStub_AddParam(ATT_APP_NMEA_ParseGga, const char *, Sentence);
    UT_GenStub_AddParam(ATT_APP_NMEA_ParseGga, size_t, Length);
    UT_GenStub_AddParam(ATT_APP_NMEA_ParseGga, ATT_APP_NMEA_Gga_t *, Gga);

    UT_GenStub_Execute(ATT_APP_NMEA_ParseGga, Basic, NULL);

    return UT_GenStub_GetReturnValue(ATT_APP_NMEA_ParseGga, ATT_APP_NMEA_Status_t);
}

/*
 * ----------------------------------------------------
 * Generated stub function for ATT_APP_NMEA_ParseGsv()
 * ----------------------------------------------------
 */
ATT_APP_NMEA_Status_t ATT_APP_NMEA_ParseGsv(const char *Sentence, size_t Length, ATT_APP_NMEA_Gsv_t *Gsv)
{
    UT_GenStub_SetupReturnBuffer(ATT_APP_NMEA_ParseGsv, ATT_APP_NMEA_Status_t);

    UT_GenStub_AddParam(ATT_APP_NMEA_ParseGsv, const char *, Sentence);
    UT_GenStub_AddParam(ATT_APP_NMEA_ParseGsv, size_t, Length);
    UT_GenStub_AddParam(ATT_APP_NMEA_ParseGsv, ATT_APP_NMEA_Gsv_t *, Gsv);

    UT_GenStub_Execute(ATT_APP_NMEA_ParseGsv, Basic, NULL);

    return UT_GenStub_GetReturnValue(ATT_APP_NMEA_ParseGsv, ATT_APP_NMEA_Status_t);
}

/*
 * ----------------------------------------------------
 * Generated stub function for ATT_APP_NMEA_VerifyChecksum()
 * ----------------------------------------------------
 */
ATT_APP_NMEA_Status_t ATT_APP_NMEA_VerifyChecksum(const char *Sentence, size_t Length)
{
    UT_GenStub_SetupReturnBuffer(ATT_APP_NMEA_VerifyChecksum, ATT_APP_NMEA_Status_t);

    UT_GenStub_AddParam(ATT_APP_NMEA_VerifyChecksum, const char *, Sentence);
    UT_GenStub_AddParam(ATT_APP_NMEA_VerifyChecksum, size_t, Length);

    UT_GenStub_Execute(ATT_APP_NMEA_VerifyChecksum, Basic, NULL);

    return UT_GenStub_GetReturnValue(ATT_APP_NMEA_VerifyChecksum, ATT_APP_NMEA_Status_t);
}
