/****************************************************************************************
* FileName     : mmw_example.c
*
* Description  : This file implements mmwave link example application.
*
****************************************************************************************
* (C) Copyright 2014, Texas Instruments Incorporated. - TI web address www.ti.com
*---------------------------------------------------------------------------------------
*
*  Redistribution and use in source and binary forms, with or without modification,
*  are permitted provided that the following conditions are met:
*
*    Redistributions of source code must retain the above copyright notice,
*    this list of conditions and the following disclaimer.
*
*    Redistributions in binary form must reproduce the above copyright notice,
*    this list of conditions and the following disclaimer in the documentation
*    and/or other materials provided with the distribution.
*
*    Neither the name of Texas Instruments Incorporated nor the names of its
*    contributors may be used to endorse or promote products derived from this
*    software without specific prior written permission.
*
*  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
*  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
*  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
*  PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT  OWNER OR CONTRIBUTORS
*  BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
*  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
*  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
*  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
*  CONTRACT,  STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
*  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
*  POSSIBILITY OF SUCH DAMAGE.
*
*/
/******************************************************************************
* INCLUDE FILES
******************************************************************************
*/
#include <winsock2.h>
#include <stdio.h>
#include <share.h>
#include <string.h>
#include <stdlib.h>
#include "mmw_example.h"
#include "mmw_config.h"
#include <ti/control/mmwavelink/mmwavelink.h>
#include "rls_studio.h"
#include "rls_osi.h"

#pragma comment(lib, "Ws2_32.lib")
#pragma warning(disable:4996) 

/****************************************************************************************
* MACRO DEFINITIONS
****************************************************************************************
*/
#define MMWL_FW_FIRST_CHUNK_SIZE (220U)
#define MMWL_FW_CHUNK_SIZE (228U)
#define MMWL_META_IMG_FILE_SIZE (sizeof(metaImage))

#define GET_BIT_VALUE(data, noOfBits, location)    ((((rlUInt32_t)(data)) >> (location)) &\
                                               (((rlUInt32_t)((rlUInt32_t)1U << (noOfBits))) - (rlUInt32_t)1U))
/* Async Event Timeouts */
#define MMWL_API_INIT_TIMEOUT                (2000) /* 2 Sec*/
#define MMWL_API_START_TIMEOUT               (1000) /* 1 Sec*/
#define MMWL_API_RF_INIT_TIMEOUT             (1000) /* 1 Sec*/

/* MAX unique chirp AWR1243 supports */
#define MAX_UNIQUE_CHIRP_INDEX                (512 -1)

/* MAX index to read back chirp config  */
#define MAX_GET_CHIRP_CONFIG_IDX              14

/* To enable TX2 */
#define ENABLE_TX2                             0

/* Enable Advanced frame config test in the application
1 -> config AdvFrame
0 -> config Legacy Frame */
#define gLinkAdvanceFrameTest 0

/* Controled by Socket */
//#define CONTROL_BY_SOCKET

/******************************************************************************
* GLOBAL VARIABLES/DATA-TYPES DEFINITIONS
******************************************************************************
*/
typedef int (*RL_P_OS_SPAWN_FUNC_PTR)(RL_P_OSI_SPAWN_ENTRY pEntry, const void* pValue, unsigned int flags);
typedef int (*RL_P_OS_DELAY_FUNC_PTR)(unsigned int delay);

/* Global Variable for Device Status */
static unsigned char mmwl_bInitComp = 0U;
static unsigned char mmwl_bStartComp = 0U;
static unsigned char mmwl_bRfInitComp = 0U;
static unsigned char mmwl_bSensorStarted = 0U;
static unsigned char mmwl_bGpadcDataRcv = 0U;

unsigned char gAwr1243CrcType = RL_CRC_TYPE_32BIT;

/* Enable/Disable continuous mode config test in the application */
unsigned char gLinkContModeTest = FALSE;

/* Enable/Disable Dynamic Chirp confing & Dynamic Chirp Enable test in application */
unsigned char gLinkDynChirpTest = FALSE;

/* Enable/Disable Dynamic Profile config test in application */
unsigned char gLinkDynProfileTest = FALSE;

/* TRUE -> Enable LDO bypass. Support only on AWR1243 Rev B EVMs
   FALSE -> Disable LDO bypass.
   CAUTION : DON'T ENABLE LDO BYPASS ON UNSUPPORTED DEVICES. IT MAY DAMAGE EVM. */
unsigned char gLinkBypassLdo = FALSE;

/* Enable/Disable calibration data store restore test in application */
unsigned char gLinkCalibStoreRestoreTest = FALSE;

/* store frame periodicity */
unsigned int framePeriodicity = 0;
/* store frame count */
unsigned int frameCount = 0;

/* SPI Communication handle to AWR1243 device*/
rlComIfHdl_t mmwl_devSpiHdl = NULL;

/* structure parameters of two profile confing and cont mode config are same */
rlProfileCfg_t profileCfgArgs[2] = { 0 };

/* strcture to store dynamic chirp configuration */
rlDynChirpCfg_t dynChirpCfgArgs[3] = { 0 };

/* Strcture to store async event config */
rlRfDevCfg_t rfDevCfg = { 0x0 };

/* Structure to store GPADC measurement data sent by device */
rlRecvdGpAdcData_t rcvGpAdcData = {0};

/* Structure to store mmWave device Calibration data */
rlCalibrationData_t calibData = { 0 };

uint64_t computeCRC(uint8_t *p, uint32_t len, uint8_t width);

/* Function to compare dynamically configured chirp data */
int MMWL_chirpParamCompare(rlChirpCfg_t * chirpData);

/* Socket Info for connection to Master application */
const char deviceID[10] = { "IWR1642" };
const int MASTER_PORT = 5555;
const char MASTER_ADDR[20] = { "127.0.0.1" };
SOCKET MasterSocket;

/** @fn void MMWL_asyncEventHandler(rlUInt8_t deviceIndex, rlUInt16_t sbId,
*    rlUInt16_t sbLen, rlUInt8_t *payload)
*
*   @brief Radar Async Event Handler callback
*   @param[in] msgId - Message Id
*   @param[in] sbId - SubBlock Id
*   @param[in] sbLen - SubBlock Length
*   @param[in] payload - Sub Block Payload
*
*   @return None
*
*   Radar Async Event Handler callback
*/
/* SourceId :  */
/* DesignId :  */
/* Requirements :  */
void MMWL_asyncEventHandler(rlUInt8_t deviceIndex, rlUInt16_t sbId,
    rlUInt16_t sbLen, rlUInt8_t *payload)
{
    unsigned int deviceMap = 0;
    rlUInt16_t msgId = sbId / RL_MAX_SB_IN_MSG;
    rlUInt16_t asyncSB = RL_GET_SBID_FROM_MSG(sbId, msgId);

    /* Host can receive Async Event from RADARSS/MSS */
    switch (msgId)
    {
        /* Async Event from RADARSS */
        case RL_RF_ASYNC_EVENT_MSG:
        {
            switch (asyncSB)
            {
            case RL_RF_AE_INITCALIBSTATUS_SB:
            {
                mmwl_bRfInitComp = 1U;
                printf("Async event: RF-init calibration status \n");
            }
            break;
            case RL_RF_AE_FRAME_TRIGGER_RDY_SB:
            {
                mmwl_bSensorStarted = 1U;
                printf("Async event: Frame trigger \n");
            }
            break;
            case RL_RF_AE_FRAME_END_SB:
            {
                mmwl_bSensorStarted = 0U;
                printf("Async event: Frame stopped \n");
            }
            break;
            case RL_RF_AE_RUN_TIME_CALIB_REPORT_SB:
            {
                printf("Aync event: Run time Calibration Report [0x%x]\n", ((rlRfRunTimeCalibReport_t*)payload)->calibErrorFlag);
            }
            break;
            case RL_RF_AE_MON_TIMING_FAIL_REPORT_SB:
            {
                printf("Aync event: Monitoring Timing Failed Report\n");
                break;
            }
            case RL_RF_AE_GPADC_MEAS_DATA_SB:
            {
                mmwl_bGpadcDataRcv = 1U;
                /* store the GPAdc Measurement data which AWR1243 will read from the analog test pins
                    where user has fed the input signal */
                memcpy(&rcvGpAdcData, payload, sizeof(rlRecvdGpAdcData_t));
                break;
            }
            case RL_RF_AE_CPUFAULT_SB:
            {
                printf("BSS CPU fault \n");
                while(1);
                break;
            }
            case RL_RF_AE_ESMFAULT_SB:
            {
                printf("BSS ESM fault \n");
                break;
            }
            default:
                break;
            }

        }
        break;

        /* Async Event from MSS */
        case RL_DEV_ASYNC_EVENT_MSG:
        {
            switch (asyncSB)
            {
                case RL_DEV_AE_MSSPOWERUPDONE_SB:
                {
                    mmwl_bInitComp = 1U;
                }
                break;
                case RL_DEV_AE_MSS_BOOTERRSTATUS_SB:
                {
                    mmwl_bInitComp = 1U;
                }
                break;
                case RL_DEV_AE_RFPOWERUPDONE_SB:
                {
                    mmwl_bStartComp = 1U;
                }
                break;
                case RL_DEV_AE_MSS_ESMFAULT_SB:
                {
                    printf("MSS ESM Error \n");
                }
                break;
                case RL_DEV_AE_MSS_CPUFAULT_SB:
                {
                    printf("MSS CPU Fault\n");
                }
                break;
            }
        }
        break;

        /* Async Event from MMWL */
        case RL_MMWL_ASYNC_EVENT_MSG:
        {
            case RL_MMWL_AE_MISMATCH_REPORT:
            {
                int errTemp = *(int32_t*)payload;
                /* CRC mismatched in the received Async-Event msg */
                if (errTemp == RL_RET_CODE_CRC_FAILED)
                {
                    printf("CRC mismatched in the received Async-Event msg \n" );
                }
                /* Checksum mismatched in the received msg */
                else if (errTemp == RL_RET_CODE_CHKSUM_FAILED)
                {
                    printf("Checksum mismatched in the received msg \n" );
                }
                /* Polling to HostIRQ is timed out,
                i.e. Device didn't respond to CNYS from the Host */
                else if (errTemp == RL_RET_CODE_HOSTIRQ_TIMEOUT)
                {
                    printf("HostIRQ polling timed out \n");
                }
                else if (errTemp == RL_RET_CODE_RADAR_OSIF_ERROR)
                {
                    printf("mmWaveLink error \n");
                }
                break;
            }
            break;
        }
        default:
        {
            printf("Unhandled Aync Event msgId: 0x%x, asyncSB:0x%x  \n", msgId, asyncSB);
            break;
        }
    }
}

/** @fn rlComIfHdl_t MMWL_spiOpen(unsigned char deviceIndex, unsigned in flags)
*
*   @brief SPI Open callback
*   @param[in] deviceIndex - Device Index
*   @param[in] flags - Optional Flags
*
*   @return INT32 Success - Handle to communication channel, Failure - NULL
*
*   SPI Open callback
*/
/* SourceId :  */
/* DesignId :  */
/* Requirements :  */
rlComIfHdl_t MMWL_spiOpen(unsigned char deviceIndex, unsigned int flags)
{
    printf("rlComIfOpen Callback Called for Device [%d]\n", deviceIndex);
    mmwl_devSpiHdl = rlsSpiOpen(&deviceIndex, flags);
    return mmwl_devSpiHdl;
}

/** @fn int MMWL_enableDevice(unsigned char deviceIndex)
*
*   @brief Performs SOP and enables the device.
*
*   @param[in] deviceIndex
*
*   @return int Success - 0, Failure - Error Code
*
*   Power on Slave device API.
*/
int MMWL_enableDevice(unsigned char deviceIndex)
{
    int retVal = RL_RET_CODE_OK;
    /* Enable device in Functional Mode (SOP-4) */
    printf("rlDeviceEnable Callback is called by mmWaveLink for Device [%d]\n", deviceIndex);
    return rlsEnableDevice(deviceIndex);
}

/** @fn int MMWL_disableDevice(unsigned char deviceIndex)
*
*   @brief disables the device.
*
*   @param[in] deviceIndex
*
*   @return int Success - 0, Failure - Error Code
*
*   Power on Slave device API.
*/
int MMWL_disableDevice(unsigned char deviceIndex)
{
    printf("rlDeviceDisable Callback is called by mmWaveLink for Device [%d]\n", deviceIndex);
    return rlsDisableDevice(deviceIndex);
}

/** @fn int MMWL_computeCRC(unsigned char* data, unsigned int dataLen, unsigned char crcLen,
                        unsigned char* outCrc)
*
*   @brief Compute the CRC of given data
*
*   @param[in] data - message data buffer pointer
*    @param[in] dataLen - length of data buffer
*    @param[in] crcLen - length of crc 2/4/8 bytes
*    @param[out] outCrc - computed CRC data
*
*   @return int Success - 0, Failure - Error Code
*
*   Compute the CRC of given data
*/
int MMWL_computeCRC(unsigned char* data, unsigned int dataLen, unsigned char crcLen,
                        unsigned char* outCrc)
{
    uint64_t crcResult = computeCRC(data, dataLen, (16 << crcLen));
    memcpy(outCrc, &crcResult, (2 << crcLen));
    return 0;
}

/** @fn int MMWL_powerOnMaster(deviceMap)
*
*   @brief Power on Master API.
*
*   @param[in] deviceMap - Devic Index
*
*   @return int Success - 0, Failure - Error Code
*
*   Power on Master API.
*/
int MMWL_powerOnMaster(unsigned char deviceMap)
{
    int retVal = RL_RET_CODE_OK, timeOutCnt = 0;
    /*
     \subsection     porting_step1   Step 1 - Define mmWaveLink client callback structure
    The mmWaveLink framework is ported to different platforms using mmWaveLink client callbacks. These
    callbacks are grouped as different structures such as OS callbacks, Communication Interface
    callbacks and others. Application needs to define these callbacks and initialize the mmWaveLink
    framework with the structure.

     Refer to \ref rlClientCbs_t for more details
     */
    rlClientCbs_t clientCtx = { 0 };

    /*Read all the parameters from config file*/
    MMWL_readPowerOnMaster(&clientCtx);

    /* store CRC Type which has been read from mmwaveconfig.txt file */
    gAwr1243CrcType = clientCtx.crcType;

    /*
    \subsection     porting_step2   Step 2 - Implement Communication Interface Callbacks
    The mmWaveLink device support several standard communication protocol among SPI and MailBox
    Depending on device variant, one need to choose the communication channel. For e.g
    xWR1443/xWR1642 requires Mailbox interface and AWR1243 supports SPI interface.
    The interface for this communication channel should include 4 simple access functions:
    -# rlComIfOpen
    -# rlComIfClose
    -# rlComIfRead
    -# rlComIfWrite

    Refer to \ref rlComIfCbs_t for interface details
    */
	// >>>>
    clientCtx.comIfCb.rlComIfOpen = MMWL_spiOpen;
    clientCtx.comIfCb.rlComIfClose = rlsSpiClose;
	clientCtx.comIfCb.rlComIfRead = rlsSpiRead;
	clientCtx.comIfCb.rlComIfWrite = rlsSpiWrite;
	// <<<<

    /*   \subsection     porting_step3   Step 3 - Implement Device Control Interface
    The mmWaveLink driver internally powers on/off the mmWave device. The exact implementation of
    these interface is platform dependent, hence you need to implement below functions:
    -# rlDeviceEnable
    -# rlDeviceDisable
    -# rlRegisterInterruptHandler

    Refer to \ref rlDeviceCtrlCbs_t for interface details
    */
    clientCtx.devCtrlCb.rlDeviceDisable = MMWL_disableDevice;
    clientCtx.devCtrlCb.rlDeviceEnable = MMWL_enableDevice;
    clientCtx.devCtrlCb.rlRegisterInterruptHandler = rlsRegisterInterruptHandler;
    clientCtx.devCtrlCb.rlDeviceWaitIrqStatus = rlsDeviceWaitIrqStatus;

	clientCtx.devCtrlCb.rlDeviceMaskHostIrq = rlsSpiIRQMask;
	clientCtx.devCtrlCb.rlDeviceUnMaskHostIrq = rlsSpiIRQUnMask;

    /*  \subsection     porting_step4     Step 4 - Implement Event Handlers
    The mmWaveLink driver reports asynchronous event indicating mmWave device status, exceptions
    etc. Application can register this callback to receive these notification and take appropriate
    actions

    Refer to \ref rlEventCbs_t for interface details*/
    clientCtx.eventCb.rlAsyncEvent = MMWL_asyncEventHandler;

    /*  \subsection     porting_step5     Step 5 - Implement OS Interface
    The mmWaveLink driver can work in both OS and NonOS environment. If Application prefers to use
    operating system, it needs to implement basic OS routines such as tasks, mutex and Semaphore


    Refer to \ref rlOsiCbs_t for interface details
    */
    /* Mutex */
    clientCtx.osiCb.mutex.rlOsiMutexCreate = osiLockObjCreate;
    clientCtx.osiCb.mutex.rlOsiMutexLock = osiLockObjLock;
    clientCtx.osiCb.mutex.rlOsiMutexUnLock = osiLockObjUnlock;
    clientCtx.osiCb.mutex.rlOsiMutexDelete = osiLockObjDelete;

    /* Semaphore */
    clientCtx.osiCb.sem.rlOsiSemCreate = osiSyncObjCreate;
    clientCtx.osiCb.sem.rlOsiSemWait = osiSyncObjWait;
    clientCtx.osiCb.sem.rlOsiSemSignal = osiSyncObjSignal;
    clientCtx.osiCb.sem.rlOsiSemDelete = osiSyncObjDelete;

    /* Spawn Task */
    clientCtx.osiCb.queue.rlOsiSpawn = (RL_P_OS_SPAWN_FUNC_PTR)osiSpawn;

    /* Sleep/Delay Callback*/
    clientCtx.timerCb.rlDelay = (RL_P_OS_DELAY_FUNC_PTR)osiSleep;

    /*  \subsection     porting_step6     Step 6 - Implement CRC Interface
    The mmWaveLink driver uses CRC for message integrity. If Application prefers to use
    CRC, it needs to implement CRC routine.

    Refer to \ref rlCrcCbs_t for interface details
    */
    clientCtx.crcCb.rlComputeCRC = MMWL_computeCRC;

    /*  \subsection     porting_step7     Step 7 - Define Platform
    The mmWaveLink driver can be configured to run on different platform by
    passing appropriate platform and device type
    */
    clientCtx.platform = RL_PLATFORM_HOST;
	clientCtx.arDevType = RL_AR_DEVICETYPE_16XX;

    /*clear all the interupts flag*/
    mmwl_bInitComp = 0;
    mmwl_bStartComp = 0U;
    mmwl_bRfInitComp = 0U;

    /*  \subsection     porting_step8     step 8 - Call Power ON API and pass client context
    The mmWaveLink driver initializes the internal components, creates Mutex/Semaphore,
    initializes buffers, register interrupts, bring mmWave front end out of reset.
    */
    retVal = rlDevicePowerOn(deviceMap, clientCtx);

    /*  \subsection     porting_step9     step 9 - Test if porting is successful
    Once configuration is complete and mmWave device is powered On, mmWaveLink driver receives
    asynchronous event from mmWave device and notifies application using
    asynchronous event callback

    Refer to \ref MMWL_asyncEventHandler for event details
    */
    while (mmwl_bInitComp == 0U)
    {
        osiSleep(1); /*Sleep 1 msec*/
        timeOutCnt++;
        if (timeOutCnt > MMWL_API_INIT_TIMEOUT)
        {
            retVal = RL_RET_CODE_RESP_TIMEOUT;
            break;
        }
    }
    mmwl_bInitComp = 0U;
    return retVal;
}

int MMWL_fileWrite(unsigned char deviceMap,
                unsigned short remChunks,
                unsigned short chunkLen,
                unsigned char *chunk)
{
    int ret_val = -1;

    rlFileData_t fileChunk = { 0 };
    fileChunk.chunkLen = chunkLen;
    memcpy(fileChunk.fData, chunk, chunkLen);

    ret_val = rlDeviceFileDownload(deviceMap, &fileChunk, remChunks);
    return ret_val;
}

/** @fn int MMWL_rfEnable(deviceMap)
*
*   @brief RFenable API.
*
*   @param[in] deviceMap - Devic Index
*
*   @return int Success - 0, Failure - Error Code
*
*   RFenable API.
*/
int MMWL_rfEnable(unsigned char deviceMap)
{
    int retVal = RL_RET_CODE_OK, timeOutCnt = 0;
    retVal = rlDeviceRfStart(deviceMap);
    while (mmwl_bStartComp == 0U)
    {
        osiSleep(1); /*Sleep 1 msec*/
        timeOutCnt++;
        if (timeOutCnt > MMWL_API_START_TIMEOUT)
        {
            retVal = RL_RET_CODE_RESP_TIMEOUT;
            break;
        }

    }
    mmwl_bStartComp = 0;

    if(retVal == RL_RET_CODE_OK)
    {
        rlVersion_t verArgs = {0};
        retVal = rlDeviceGetVersion(deviceMap,&verArgs);

        printf("\nRF Version [%2d.%2d.%2d.%2d] \nMSS version [%2d.%2d.%2d.%2d] \nmmWaveLink version [%2d.%2d.%2d.%2d]\n",
            verArgs.rf.fwMajor, verArgs.rf.fwMinor, verArgs.rf.fwBuild, verArgs.rf.fwDebug,
            verArgs.master.fwMajor, verArgs.master.fwMinor, verArgs.master.fwBuild, verArgs.master.fwDebug,
            verArgs.mmWaveLink.major, verArgs.mmWaveLink.minor, verArgs.mmWaveLink.build, verArgs.mmWaveLink.debug);
        printf("\nRF Patch Version [%2d.%2d.%2d.%2d] \nMSS Patch version [%2d.%2d.%2d.%2d]\n",
            verArgs.rf.patchMajor, verArgs.rf.patchMinor, ((verArgs.rf.patchBuildDebug & 0xF0) >> 4), (verArgs.rf.patchBuildDebug & 0x0F),
            verArgs.master.patchMajor, verArgs.master.patchMinor, ((verArgs.master.patchBuildDebug & 0xF0) >> 4), (verArgs.master.patchBuildDebug & 0x0F));
    }
    return retVal;
}

/** @fn int MMWL_dataFmtConfig(unsigned char deviceMap)
*
*   @brief Data Format Config API
*
*   @return Success - 0, Failure - Error Code
*
*   Data Format Config API
*/
/* SourceId :  */
/* DesignId :  */
/* Requirements :  */
int MMWL_dataFmtConfig(unsigned char deviceMap)
{
    int retVal = RL_RET_CODE_OK;
    rlDevDataFmtCfg_t dataFmtCfgArgs = { 0 };

    /*dataFmtCfgArgs from config file*/
    MMWL_readDataFmtConfig(&dataFmtCfgArgs);

    retVal = rlDeviceSetDataFmtConfig(deviceMap, &dataFmtCfgArgs);
    return retVal;
}

/** @fn int MMWL_ldoBypassConfig(unsigned char deviceMap)
*
*   @brief LDO Bypass Config API
*
*   @return Success - 0, Failure - Error Code
*
*   LDO Bypass Config API
*/
/* SourceId :  */
/* DesignId :  */
/* Requirements :  */
int MMWL_ldoBypassConfig(unsigned char deviceMap)
{
    int retVal = RL_RET_CODE_OK;
    rlRfLdoBypassCfg_t rfLdoBypassCfgArgs = { 0 };

	// Eson: Set always FALSE because LDO may damage EVM besides 1432.
    /*if (gLinkBypassLdo == TRUE)
    {
        rfLdoBypassCfgArgs.ldoBypassEnable = 1;
    }*/

    printf("Calling rlRfSetLdoBypassConfig With Bypass [%d] \n",
        rfLdoBypassCfgArgs.ldoBypassEnable);

    retVal = rlRfSetLdoBypassConfig(deviceMap, &rfLdoBypassCfgArgs);
    return retVal;
}

/** @fn int MMWL_adcOutConfig(unsigned char deviceMap)
*
*   @brief ADC Configuration API
*
*   @return Success - 0, Failure - Error Code
*
*   ADC Configuration API
*/
/* SourceId :  */
/* DesignId :  */
/* Requirements :  */
int MMWL_adcOutConfig(unsigned char deviceMap)
{
    int retVal = RL_RET_CODE_OK;

    rlAdcOutCfg_t adcOutCfgArgs = { 0 };

    /*read adcOutCfgArgs from config file*/
    MMWL_readAdcOutConfig(&adcOutCfgArgs);


    printf("Calling rlSetAdcOutConfig With [%d]ADC Bits and [%d]ADC Format \n",
        adcOutCfgArgs.fmt.b2AdcBits, adcOutCfgArgs.fmt.b2AdcOutFmt);

    retVal = rlSetAdcOutConfig(deviceMap, &adcOutCfgArgs);
    return retVal;
}

/** @fn int MMWL_channelConfig(unsigned char deviceMap,
                               unsigned short cascading)
*
*   @brief Channel Config API
*
*   @return Success - 0, Failure - Error Code
*
*   Channel Config API
*/
/* SourceId :  */
/* DesignId :  */
/* Requirements :  */
int MMWL_channelConfig(unsigned char deviceMap,
                       unsigned short cascade)
{
    int retVal = RL_RET_CODE_OK;
    /* TBD - Read GUI Values */
    rlChanCfg_t rfChanCfgArgs = { 0 };

    /*read arguments from config file*/
    MMWL_readChannelConfig(&rfChanCfgArgs, cascade);

#if (ENABLE_TX2)
    rfChanCfgArgs.txChannelEn |= (1 << 2); // Enable TX2
#endif

    printf("Calling rlSetChannelConfig With b[%d]Rx and b[%d]Tx Channel Enabled \n",
           rfChanCfgArgs.rxChannelEn, rfChanCfgArgs.txChannelEn);

    retVal = rlSetChannelConfig(deviceMap, &rfChanCfgArgs);
    return retVal;
}

/** @fn int MMWL_setAsyncEventDir(unsigned char deviceMap)
*
*   @brief Update async event message direction and CRC type of Async event
*           from AWR1243 radarSS to Host
*
*   @param[in] deviceMap - Devic Index
*
*   @return int Success - 0, Failure - Error Code
*
*   Update async event message direction and CRC type of Async event
*   from AWR1243 radarSS to Host
*/
int MMWL_setAsyncEventDir(unsigned char  deviceMap)
{
    int32_t         retVal;
    /* set global and monitoring async event direction to Host */
    rfDevCfg.aeDirection = 0x05;
    /* Set the CRC type of Async event received from radarSS */
    rfDevCfg.aeCrcConfig = gAwr1243CrcType;
    retVal = rlRfSetDeviceCfg(deviceMap, &rfDevCfg);

    /* Sanity Check: Was the mmWave link successful? */
    if (retVal != 0)
    {
        /* Error: Link reported an issue. */
        printf("Error: rlSetAsyncEventDir retVal=%d\n", retVal);
        return -1;
    }

    printf("Debug: Finished rlSetAsyncEventDir\n");

    return 0;
}

/** @fn int MMWL_setDeviceCrcType(unsigned char deviceMap)
*
*   @brief Set CRC type of async event from AWR1243 MasterSS
*
*   @param[in] deviceMap - Devic Index
*
*   @return int Success - 0, Failure - Error Code
*
*   Set CRC type of async event from AWR1243 MasterSS
*/
int MMWL_setDeviceCrcType(unsigned char deviceMap)
{
    int32_t         retVal;
    rlDevMiscCfg_t devMiscCfg = {0};
    /* Set the CRC Type for Async Event from MSS */
    devMiscCfg.aeCrcConfig = gAwr1243CrcType;
    retVal = rlDeviceSetMiscConfig(deviceMap, &devMiscCfg);

    /* Sanity Check: Was the mmWave link successful? */
    if (retVal != 0)
    {
        /* Error: Link reported an issue. */
        printf("Error: rlDeviceSetMiscConfig retVal=%d\n", retVal);
        return -1;
    }

    printf("Debug: Finished rlDeviceSetMiscConfig\n");

    return 0;
}

/** @fn int MMWL_basicConfiguration(unsigned char deviceMap, unsigned int cascade)
*
*   @brief Channel, ADC,Data format configuration API.
*
*   @param[in] deviceMap - Devic Index
*    @param[in] unsigned int cascade
*
*   @return int Success - 0, Failure - Error Code
*
*   Channel, ADC,Data format configuration API.
*/
int MMWL_basicConfiguration(unsigned char deviceMap, unsigned int cascade)
{
    int retVal = RL_RET_CODE_OK;

    /* Set which Rx and Tx channels will be enable of the device */
    retVal = MMWL_channelConfig(deviceMap, cascade);
    if (retVal != RL_RET_CODE_OK)
    {
        printf("Channel Config failed with error code %d\n\n", retVal);
        return -1;
    }
    else
    {
        printf(">>>>Channel Configuration success\n\n");
    }

    /* ADC out data format configuration */
    retVal = MMWL_adcOutConfig(deviceMap);
    if (retVal != RL_RET_CODE_OK)
    {
        printf("AdcOut Config failed with error code\n\n", retVal);
        return -1;
    }
    else
    {
        printf(">>>>AdcOut Configuration success\n\n");
    }

    /* LDO bypass configuration */
    retVal = MMWL_ldoBypassConfig(deviceMap);
    if (retVal != RL_RET_CODE_OK)
    {
        printf("LDO Bypass Config failed with error code\n\n",
            deviceMap, retVal);
        return -1;
    }
    else
    {
        printf(">>>>LDO Bypass Configuration success\n\n");
    }

    /* Data format configuration */
    retVal = MMWL_dataFmtConfig(deviceMap);
    if (retVal != RL_RET_CODE_OK)
    {
        printf("Data format Configuration failed with error code\n",
                deviceMap, retVal);
        return -1;
    }
    else
    {
        printf(">>>>Data format Configuration success\n");
    }

    /* low power configuration */
    retVal = MMWL_lowPowerConfig(deviceMap);
    if (retVal != RL_RET_CODE_OK)
    {
        printf("Low Power Configuration failed %u with error\n",
                deviceMap, retVal);
        return -1;
    }
    else
    {
        printf(">>>>Low Power Configuration succes \n");
    }

    /* Async event direction and control configuration for RadarSS */
    retVal = MMWL_setAsyncEventDir(deviceMap);
    if (retVal != RL_RET_CODE_OK)
    {
        printf("AsyncEvent Configuration failed with error code \n\n",
                deviceMap, retVal);
        return -1;
    }
    else
    {
        printf(">>>>AsyncEvent Configuration success \n\n");
    }
    return retVal;
}

/** @fn int MMWL_rfInit(unsigned char deviceMap)
*
*   @brief RFinit API.
*
*   @param[in] deviceMap - Devic Index
*
*   @return int Success - 0, Failure - Error Code
*
*   RFinit API.
*/
int MMWL_rfInit(unsigned char deviceMap)
{
    int retVal = RL_RET_CODE_OK, timeOutCnt = 0;
    rlRfInitCalConf_t rfCalibCfgArgs = { 0 };
    /* Enable all calibrations which is already default setting in the device. Application can
       change this calibraiton setting if required. */
    rfCalibCfgArgs.calibEnMask = 0x1FF0;
    /* This API enables/disables the calibrations to run. If applicaiton needs to disable any
       specific boot-time calibration it can use following API by setting "calibEnMask" with
       appropriate value. All the calibrations are enabled by default in the device. */
    retVal = rlRfInitCalibConfig(deviceMap, &rfCalibCfgArgs);
    if (retVal != RL_RET_CODE_OK)
    {
        printf("RF calib configuration failed with error code\n\n",
            deviceMap, retVal);
        return -1;
    }

    if (retVal == 0)
    {
        mmwl_bRfInitComp = 0;
        /* Run boot time calibrations */
        retVal = rlRfInit(deviceMap);
        while (mmwl_bRfInitComp == 0U)
        {
            osiSleep(1); /*Sleep 1 msec*/
            timeOutCnt++;
            if (timeOutCnt > MMWL_API_RF_INIT_TIMEOUT)
            {
                retVal = RL_RET_CODE_RESP_TIMEOUT;
                break;
            }
        }
        mmwl_bRfInitComp = 0;
    }
    return retVal;
}

/** @fn int MMWL_profileConfig(unsigned char deviceMap)
*
*   @brief Profile configuration API.
*
*   @param[in] deviceMap - Devic Index
*
*   @return int Success - 0, Failure - Error Code
*
*   Profile configuration API.
*/
int MMWL_profileConfig(unsigned char deviceMap)
{
    int retVal = RL_RET_CODE_OK;
    double endFreqConst;

    /*read profileCfgArgs from config file*/
    MMWL_readProfileConfig(&profileCfgArgs[0]);

    endFreqConst = (double)(profileCfgArgs[0].startFreqConst * 53.6441803 + (profileCfgArgs[0].rampEndTime * 10) * \
                   (profileCfgArgs[0].freqSlopeConst * 48.2797623))/53.6441803;
    if ((profileCfgArgs[0].startFreqConst >= 1435388860U) && ((unsigned int)endFreqConst <= 1509954515U))
    {
        /* If start frequency is in between 77GHz to 81GHz use VCO2 */
        profileCfgArgs[0].pfVcoSelect = 0x02;
    }
    else
    {
        /* If start frequency is in between 76GHz to 78GHz use VCO1 */
        profileCfgArgs[0].pfVcoSelect = 0x00;
    }

    printf("Calling rlSetProfileConfig with \nProfileId[%d]\nStart Frequency[%f] GHz\nRamp Slope[%f] MHz/uS \n",
        profileCfgArgs[0].profileId, (float)((profileCfgArgs[0].startFreqConst * 53.6441803)/(1000*1000*1000)),
        (float)(profileCfgArgs[0].freqSlopeConst * 48.2797623)/1000.0);
    /* with this API we can configure 2 profiles (max 4 profiles) at a time */
    retVal = rlSetProfileConfig(deviceMap, 1U, &profileCfgArgs[0U]);
    return retVal;
}

/** @fn int MMWL_chirpConfig(unsigned char deviceMap)
*
*   @brief Chirp configuration API.
*
*   @param[in] deviceMap - Devic Index
*
*   @return int Success - 0, Failure - Error Code
*
*   Chirp configuration API.
*/
int MMWL_chirpConfig(unsigned char deviceMap)
{
    int i, retVal = RL_RET_CODE_OK;
    rlChirpCfg_t setChirpCfgArgs[2] = {0};

    rlChirpCfg_t getChirpCfgArgs[MAX_GET_CHIRP_CONFIG_IDX+1] = {0};

    /*read chirpCfgArgs from config file*/
    MMWL_readChirpConfig(&setChirpCfgArgs[0]);

    /* Setup second chirp configuration based on first read from mmwaveconfig.txt */
    /* check if first chirpConfig end Index is not covering MAX chirp index */
    if (setChirpCfgArgs[0].chirpEndIdx < MAX_UNIQUE_CHIRP_INDEX)
    {
        setChirpCfgArgs[1].chirpStartIdx = setChirpCfgArgs[0].chirpEndIdx + 1U;
    }
    else
    {
        setChirpCfgArgs[1].chirpStartIdx = setChirpCfgArgs[0].chirpEndIdx;
    }
    setChirpCfgArgs[1].chirpEndIdx   = MAX_UNIQUE_CHIRP_INDEX;
    setChirpCfgArgs[1].txEnable  = 2;

    printf("Calling rlSetChirpConfig with \nProfileId[%d]\nStart Idx[%d]\nEnd Idx[%d] \n",
                setChirpCfgArgs[0].profileId, setChirpCfgArgs[0].chirpStartIdx,
                setChirpCfgArgs[0].chirpEndIdx);
    /* With this API we can configure max 512 chirp in one call */
    retVal = rlSetChirpConfig(deviceMap, 2U, &setChirpCfgArgs[0U]);

    /* read back Chirp config, to verify that setChirpConfig actually set to Device
      @Note - This examples read back (10+1) num of chirp config for demonstration,
               which user can raise to match with their requirement */
    retVal = rlGetChirpConfig(deviceMap, setChirpCfgArgs[0].chirpStartIdx,
                              setChirpCfgArgs[0].chirpStartIdx + MAX_GET_CHIRP_CONFIG_IDX, &getChirpCfgArgs[0]);
    if (retVal != RL_RET_CODE_OK)
    {
        printf("GetChirp Configuration failed with error %d \n\n", retVal);
    }
    else
    {
        for (i=0; i <= MAX_GET_CHIRP_CONFIG_IDX; i++)
        {
            /* @Note- This check assumes that all chirp configs are configured by single setChirpCfgArgs[0] */
            /* compare each chirpConfig parameters to lastly configured via rlDynChirpConfig API */
            if ((getChirpCfgArgs[i].profileId != setChirpCfgArgs[0].profileId) || \
                (getChirpCfgArgs[i].freqSlopeVar != setChirpCfgArgs[0].freqSlopeVar) || \
                (getChirpCfgArgs[i].txEnable != setChirpCfgArgs[0].txEnable) || \
                (getChirpCfgArgs[i].startFreqVar != setChirpCfgArgs[0].startFreqVar) || \
                (getChirpCfgArgs[i].idleTimeVar != setChirpCfgArgs[0].idleTimeVar) || \
                (getChirpCfgArgs[i].adcStartTimeVar != setChirpCfgArgs[0].adcStartTimeVar))
            {
                    printf("*** Failed - Parameters are mismatched GetChirpConfig compare to rlSetChirpConfig *** \n");
                    break;
            }

        }

        if (i > MAX_GET_CHIRP_CONFIG_IDX)
        {
            printf("Debug: Get chirp configurations are matching with parameters configured during rlSetChirpConfig \n");
        }
    }

    return retVal;
}

int MMWL_chirpParamCompare(rlChirpCfg_t * chirpData)
{
    int retVal = RL_RET_CODE_OK, i = 0,j = 0;
    /* compare each chirpConfig parameters to lastly configured via rlDynChirpConfig API */
    while (i <= MAX_GET_CHIRP_CONFIG_IDX)
    {
        if (dynChirpCfgArgs[0].chirpRowSelect == 0x00)
        {
            if ((chirpData->profileId != GET_BIT_VALUE(dynChirpCfgArgs[0].chirpRow[j].chirpNR1, 4, 0)) || \
                (chirpData->freqSlopeVar != GET_BIT_VALUE(dynChirpCfgArgs[0].chirpRow[j].chirpNR1, 6, 8)) || \
                (chirpData->txEnable != GET_BIT_VALUE(dynChirpCfgArgs[0].chirpRow[j].chirpNR1, 3, 16)) || \
                (chirpData->startFreqVar != GET_BIT_VALUE(dynChirpCfgArgs[0].chirpRow[j].chirpNR2, 23, 0)) || \
                (chirpData->idleTimeVar != GET_BIT_VALUE(dynChirpCfgArgs[0].chirpRow[j].chirpNR3, 12, 0)) || \
                (chirpData->adcStartTimeVar != GET_BIT_VALUE(dynChirpCfgArgs[0].chirpRow[j].chirpNR3, 12, 16)))
            {
                break;
            }
            i++;
            chirpData++;
        }
        else if (dynChirpCfgArgs[0].chirpRowSelect == 0x10)
        {
            if ((chirpData->profileId != GET_BIT_VALUE(dynChirpCfgArgs[0].chirpRow[j].chirpNR1, 4, 0)) || \
                (chirpData->freqSlopeVar != GET_BIT_VALUE(dynChirpCfgArgs[0].chirpRow[j].chirpNR1, 6, 8)) || \
                (chirpData->txEnable != GET_BIT_VALUE(dynChirpCfgArgs[0].chirpRow[j].chirpNR1, 3, 16)))
            {
                break;
            }
            i++;
            chirpData++;
            if ((chirpData->profileId != GET_BIT_VALUE(dynChirpCfgArgs[0].chirpRow[j].chirpNR2, 4, 0)) || \
                (chirpData->freqSlopeVar != GET_BIT_VALUE(dynChirpCfgArgs[0].chirpRow[j].chirpNR2, 6, 8)) || \
                (chirpData->txEnable != GET_BIT_VALUE(dynChirpCfgArgs[0].chirpRow[j].chirpNR2, 3, 16)))
            {
                break;
            }
            i++;
            chirpData++;
            if ((chirpData->profileId != GET_BIT_VALUE(dynChirpCfgArgs[0].chirpRow[j].chirpNR3, 4, 0)) || \
                (chirpData->freqSlopeVar != GET_BIT_VALUE(dynChirpCfgArgs[0].chirpRow[j].chirpNR3, 6, 8)) || \
                (chirpData->txEnable != GET_BIT_VALUE(dynChirpCfgArgs[0].chirpRow[j].chirpNR3, 3, 16)))
            {
                break;
            }
            i++;
            chirpData++;
        }
        else if (dynChirpCfgArgs[0].chirpRowSelect == 0x20)
        {
            if (chirpData->startFreqVar != GET_BIT_VALUE(dynChirpCfgArgs[0].chirpRow[j].chirpNR1, 23, 0))
            {
                break;
            }
            i++;
            chirpData++;
            if (chirpData->startFreqVar != GET_BIT_VALUE(dynChirpCfgArgs[0].chirpRow[j].chirpNR2, 23, 0))
            {
                break;
            }
            i++;
            chirpData++;
            if (chirpData->startFreqVar != GET_BIT_VALUE(dynChirpCfgArgs[0].chirpRow[j].chirpNR3, 23, 0))
            {
                break;
            }
            i++;
            chirpData++;
        }
        else if (dynChirpCfgArgs[0].chirpRowSelect == 0x30)
        {
            if ((chirpData->idleTimeVar != GET_BIT_VALUE(dynChirpCfgArgs[0].chirpRow[j].chirpNR1, 12, 0)) || \
                (chirpData->adcStartTimeVar != GET_BIT_VALUE(dynChirpCfgArgs[0].chirpRow[j].chirpNR1, 12, 16)))
            {
                break;
            }
            i++;
            chirpData++;
            if ((chirpData->idleTimeVar != GET_BIT_VALUE(dynChirpCfgArgs[0].chirpRow[j].chirpNR2, 12, 0)) || \
                (chirpData->adcStartTimeVar != GET_BIT_VALUE(dynChirpCfgArgs[0].chirpRow[j].chirpNR2, 12, 16)))
            {
                break;
            }
            i++;
            chirpData++;
            if ((chirpData->idleTimeVar != GET_BIT_VALUE(dynChirpCfgArgs[0].chirpRow[j].chirpNR3, 12, 0)) || \
                (chirpData->adcStartTimeVar != GET_BIT_VALUE(dynChirpCfgArgs[0].chirpRow[j].chirpNR3, 12, 16)))
            {
                break;
            }
            i++;
            chirpData++;
        }
        j++;
    }
    if (i <= MAX_GET_CHIRP_CONFIG_IDX)
    {
        retVal = -1;
    }
    return retVal;
}
int MMWL_getDynChirpConfig(unsigned char deviceMap)
{
    int retVal = RL_RET_CODE_OK,i = 0, j= 0, chirpNotMatch = 0;
    unsigned short chirpStartIdx;
    rlChirpCfg_t chirpCfgArgs[MAX_GET_CHIRP_CONFIG_IDX+1] = {0};
    if (dynChirpCfgArgs[0].chirpRowSelect == 0x00)
    {
        chirpStartIdx = (dynChirpCfgArgs[0].chirpSegSel * 16);
    }
    else
    {
        chirpStartIdx = (dynChirpCfgArgs[0].chirpSegSel * 48);
    }
    /* get the chirp config for (10+1) chirps for which it's being updated by dynChirpConfig API
       @Note - This examples read back (10+1) num of chirp config for demonstration,
               which user can raise to match with their requirement */
    retVal = rlGetChirpConfig(deviceMap, chirpStartIdx, chirpStartIdx + MAX_GET_CHIRP_CONFIG_IDX, &chirpCfgArgs[0]);

    if (retVal != RL_RET_CODE_OK)
    {
        printf("*** Failed - rlGetChirpConfig failed with %d*** \n",retVal);
    }

    retVal = MMWL_chirpParamCompare(&chirpCfgArgs[0]);

    if (retVal != RL_RET_CODE_OK)
    {
        printf("*** Failed - Parameters are mismatched GetChirpConfig compare to dynChirpConfig *** \n");
    }
    else
    {
        printf("Debug: Get chirp configurations are matching with parameters configured via dynChirpConfig \n");
    }

    return retVal;
}

/** @fn int MMWL_frameConfig(unsigned char deviceMap)
*
*   @brief Frame configuration API.
*
*   @param[in] deviceMap - Devic Index
*
*   @return int Success - 0, Failure - Error Code
*
*   Frame configuration API.
*/
int MMWL_frameConfig(unsigned char deviceMap)
{
    int retVal = RL_RET_CODE_OK;
    rlFrameCfg_t frameCfgArgs = { 0 };

    /*read frameCfgArgs from config file*/
    MMWL_readFrameConfig(&frameCfgArgs);

    framePeriodicity = (frameCfgArgs.framePeriodicity * 5)/(1000*1000);
    frameCount = frameCfgArgs.numFrames;

    printf("Calling rlSetFrameConfig with \nStart Idx[%d]\nEnd Idx[%d]\nLoops[%d]\nPeriodicity[%d]ms \n",
        frameCfgArgs.chirpStartIdx, frameCfgArgs.chirpEndIdx,
        frameCfgArgs.numLoops, (frameCfgArgs.framePeriodicity * 5)/(1000*1000));

    retVal = rlSetFrameConfig(deviceMap, &frameCfgArgs);
    return retVal;
}

/** @fn int MMWL_advFrameConfig(unsigned char deviceMap)
*
*   @brief Advance Frame configuration API.
*
*   @param[in] deviceMap - Devic Index
*
*   @return int Success - 0, Failure - Error Code
*
*   Frame configuration API.
*/
int MMWL_advFrameConfig(unsigned char deviceMap)
{
    int i, retVal = RL_RET_CODE_OK;
    rlAdvFrameCfg_t AdvframeCfgArgs = { 0 };
    rlAdvFrameCfg_t GetAdvFrameCfgArgs = { 0 };
    /* reset frame periodicity to zero */
    framePeriodicity = 0;

    /*read frameCfgArgs from config file*/
    MMWL_readAdvFrameConfig(&AdvframeCfgArgs);

    /* Add all subframes periodicity to get whole frame periodicity */
    for (i=0; i < AdvframeCfgArgs.frameSeq.numOfSubFrames; i++)
        framePeriodicity += AdvframeCfgArgs.frameSeq.subFrameCfg[i].subFramePeriodicity;

    framePeriodicity = (framePeriodicity * 5)/(1000*1000);
    /* store total number of frames configured */
    frameCount = AdvframeCfgArgs.frameSeq.numFrames;

    printf("Calling rlSetAdvFrameConfig with \nnumOfSubFrames[%d]\nforceProfile[%d]\nnumFrames[%d]\ntriggerSelect[%d]ms \n",
        AdvframeCfgArgs.frameSeq.numOfSubFrames, AdvframeCfgArgs.frameSeq.forceProfile,
        AdvframeCfgArgs.frameSeq.numFrames, AdvframeCfgArgs.frameSeq.triggerSelect);

    retVal = rlSetAdvFrameConfig(deviceMap, &AdvframeCfgArgs);
    if (retVal == 0)
    {
        retVal = rlGetAdvFrameConfig(deviceMap, &GetAdvFrameCfgArgs);
        if ((AdvframeCfgArgs.frameSeq.forceProfile != GetAdvFrameCfgArgs.frameSeq.forceProfile) || \
            (AdvframeCfgArgs.frameSeq.frameTrigDelay != GetAdvFrameCfgArgs.frameSeq.frameTrigDelay) || \
            (AdvframeCfgArgs.frameSeq.numFrames != GetAdvFrameCfgArgs.frameSeq.numFrames) || \
            (AdvframeCfgArgs.frameSeq.numOfSubFrames != GetAdvFrameCfgArgs.frameSeq.numOfSubFrames) || \
            (AdvframeCfgArgs.frameSeq.triggerSelect != GetAdvFrameCfgArgs.frameSeq.triggerSelect))
        {
            printf("MMWL_readAdvFrameConfig failed...\n");
            return retVal;
        }
    }
    return retVal;
}

/** @fn int MMWL_dataPathConfig(unsigned char deviceMap)
*
*   @brief Data path configuration API. Configures CQ data size on the
*           lanes and number of samples of CQ[0-2] to br transferred.
*
*   @param[in] deviceMap - Devic Index
*
*   @return int Success - 0, Failure - Error Code
*
*   Data path configuration API. Configures CQ data size on the
*   lanes and number of samples of CQ[0-2] to br transferred.
*/
int MMWL_dataPathConfig(unsigned char deviceMap)
{
    int retVal = RL_RET_CODE_OK;
    rlDevDataPathCfg_t dataPathCfgArgs = { 0 };

    /* read dataPathCfgArgs from config file */
    MMWL_readDataPathConfig(&dataPathCfgArgs);

    printf("Calling rlDeviceSetDataPathConfig with HSI Interface[%d] Selected \n",
            dataPathCfgArgs.intfSel);

    /* same API is used to configure CQ data size on the
     * lanes and number of samples of CQ[0-2] to br transferred.
     */
    retVal = rlDeviceSetDataPathConfig(deviceMap, &dataPathCfgArgs);
    return retVal;
}

/** @fn int MMWL_lvdsLaneConfig(unsigned char deviceMap)
*
*   @brief Lane Config API
*
*   @return Success - 0, Failure - Error Code
*
*   Lane Config API
*/
/* SourceId :  */
/* DesignId :  */
/* Requirements :  */
int MMWL_lvdsLaneConfig(unsigned char deviceMap)
{
    int retVal = RL_RET_CODE_OK;
    rlDevLvdsLaneCfg_t lvdsLaneCfgArgs = { 0 };

    /*read lvdsLaneCfgArgs from config file*/
    MMWL_readLvdsLaneConfig(&lvdsLaneCfgArgs);

    retVal = rlDeviceSetLvdsLaneConfig(deviceMap, &lvdsLaneCfgArgs);
    return retVal;
}

/** @fn int MMWL_laneConfig(unsigned char deviceMap)
*
*   @brief Lane Enable API
*
*   @return Success - 0, Failure - Error Code
*
*   Lane Enable API
*/
/* SourceId :  */
/* DesignId :  */
/* Requirements :  */
int MMWL_laneConfig(unsigned char deviceMap)
{
    int retVal = RL_RET_CODE_OK;
    rlDevLaneEnable_t laneEnCfgArgs = { 0 };

    /*read laneEnCfgArgs from config file*/
    MMWL_readLaneConfig(&laneEnCfgArgs);

    retVal = rlDeviceSetLaneConfig(deviceMap, &laneEnCfgArgs);
    return retVal;
}

/** @fn int MMWL_hsiLaneConfig(unsigned char deviceMap)
*
*   @brief LVDS lane configuration API.
*
*   @param[in] deviceMap - Devic Index
*
*   @return int Success - 0, Failure - Error Code
*
*   LVDS lane configuration API.
*/
int MMWL_hsiLaneConfig(unsigned char deviceMap)
{
    int retVal = RL_RET_CODE_OK;
    /*lane configuration*/
    retVal = MMWL_laneConfig(deviceMap);
    if (retVal != RL_RET_CODE_OK)
    {
        printf("LaneConfig failed with error code %d\n", retVal);
        return -1;
    }
    else
    {
        printf("LaneConfig success\n");
    }
    /*LVDS lane configuration*/
    retVal = MMWL_lvdsLaneConfig(deviceMap);
    if (retVal != RL_RET_CODE_OK)
    {
        printf("LvdsLaneConfig failed with error code %d\n", retVal);
        return -1;
    }
    else
    {
        printf("LvdsLaneConfig success\n");
    }
    return retVal;
}

/** @fn int MMWL_setHsiClock(unsigned char deviceMap)
*
*   @brief High Speed Interface Clock Config API
*
*   @return Success - 0, Failure - Error Code
*
*   HSI Clock Config API
*/
/* SourceId :  */
/* DesignId :  */
/* Requirements :  */
int MMWL_setHsiClock(unsigned char deviceMap)
{
    int retVal = RL_RET_CODE_OK;
    rlDevHsiClk_t hsiClkgs = { 0 };

    /*read hsiClkgs from config file*/
    MMWL_readSetHsiClock(&hsiClkgs);

    printf("Calling rlDeviceSetHsiClk with HSI Clock[%d] \n",
            hsiClkgs.hsiClk);

    retVal = rlDeviceSetHsiClk(deviceMap, &hsiClkgs);
    return retVal;
}

/** @fn int MMWL_hsiDataRateConfig(unsigned char deviceMap)
*
*   @brief LVDS/CSI2 Clock Config API
*
*   @return Success - 0, Failure - Error Code
*
*   LVDS/CSI2 Clock Config API
*/
/* SourceId :  */
/* DesignId :  */
/* Requirements :  */
int MMWL_hsiDataRateConfig(unsigned char deviceMap)
{
    int retVal = RL_RET_CODE_OK;
    rlDevDataPathClkCfg_t dataPathClkCfgArgs = { 0 };

    /*read lvdsClkCfgArgs from config file*/
    MMWL_readLvdsClkConfig(&dataPathClkCfgArgs);

    printf("Calling rlDeviceSetDataPathClkConfig with HSI Data Rate[%d] Selected \n",
            dataPathClkCfgArgs.dataRate);

    retVal = rlDeviceSetDataPathClkConfig(deviceMap, &dataPathClkCfgArgs);
    return retVal;
}

/** @fn int MMWL_hsiClockConfig(unsigned char deviceMap)
*
*   @brief Clock configuration API.
*
*   @param[in] deviceMap - Devic Index
*
*   @return int Success - 0, Failure - Error Code
*
*   Clock configuration API.
*/
int MMWL_hsiClockConfig(unsigned char deviceMap)
{
    int retVal = RL_RET_CODE_OK, readAllParams = 0;

    /*LVDS clock configuration*/
    retVal = MMWL_hsiDataRateConfig(deviceMap);
    if (retVal != RL_RET_CODE_OK)
    {
        printf("LvdsClkConfig failed with error code %d\n\n", retVal);
        return -1;
    }
    else
    {
        printf("MMWL_hsiDataRateConfig success\n\n");
    }

    /*set high speed clock configuration*/
    retVal = MMWL_setHsiClock(deviceMap);
    if (retVal != RL_RET_CODE_OK)
    {
        printf("MMWL_setHsiClock failed with error code %d\n\n", retVal);
        return -1;
    }
    else
    {
        printf("MMWL_setHsiClock success\n\n");
    }

    return retVal;
}

/** @fn int MMWL_gpadcMeasConfig(unsigned char deviceMap)
*
*   @brief API to set GPADC configuration.
*
*   @param[in] deviceMap - Devic Index
*
*   @return int Success - 0, Failure - Error Code.
*
*   API to set GPADC Configuration. And device will    send GPADC
*    measurement data in form of Asynchronous event over SPI to
*    Host. User needs to feed input signal on the device pins where
*    they want to read the measurement data inside the device.
*/
int MMWL_gpadcMeasConfig(unsigned char deviceMap)
{
    int retVal = RL_RET_CODE_OK;
    int timeOutCnt = 0;
    rlGpAdcCfg_t gpadcCfg = {0};

    /* enable all the sensors [0-6] to read gpADC measurement data */
    gpadcCfg.enable = 0x3F;
    /* set the number of samples device needs to collect to do the measurement */
    gpadcCfg.numOfSamples[0].sampleCnt = 16;
    gpadcCfg.numOfSamples[1].sampleCnt = 16;
    gpadcCfg.numOfSamples[2].sampleCnt = 16;
    gpadcCfg.numOfSamples[3].sampleCnt = 16;
    gpadcCfg.numOfSamples[4].sampleCnt = 16;
    gpadcCfg.numOfSamples[5].sampleCnt = 16;
    gpadcCfg.numOfSamples[6].sampleCnt = 16;

    retVal = rlSetGpAdcConfig(deviceMap, &gpadcCfg);

    if(retVal == RL_RET_CODE_OK)
    {
        /* The actual GPADC measurement is done at the end of current burst/frame,
		   so GPADC data async event might get delayed by max one burst/frame */
        while (mmwl_bGpadcDataRcv == 0U)
        {
            osiSleep(1); /*Sleep 1 msec*/
            timeOutCnt++;
            if (timeOutCnt > MMWL_API_RF_INIT_TIMEOUT)
            {
                retVal = RL_RET_CODE_RESP_TIMEOUT;
                break;
            }
        }
    }

    return retVal;
}

/** @fn int MMWL_sensorStart(unsigned char deviceMap)
*
*   @brief API to Start sensor.
*
*   @param[in] deviceMap - Devic Index
*
*   @return int Success - 0, Failure - Error Code
*
*   API to Start sensor.
*/
int MMWL_sensorStart(unsigned char deviceMap)
{
    int retVal = RL_RET_CODE_OK;
    int timeOutCnt = 0;
    mmwl_bSensorStarted = 0U;
    retVal = rlSensorStart(deviceMap);
#ifndef ENABLE_TEST_SOURCE
    if ((rfDevCfg.aeControl & 0x1) == 0x0)
    {
        while (mmwl_bSensorStarted == 0U)
        {
            osiSleep(1); /*Sleep 1 msec*/
            timeOutCnt++;
            if (timeOutCnt > MMWL_API_RF_INIT_TIMEOUT)
            {
                retVal = RL_RET_CODE_RESP_TIMEOUT;
                break;
            }
        }
    }
#endif
    return retVal;
}

/** @fn int MMWL_sensorStop(unsigned char deviceMap)
*
*   @brief API to Stop sensor.
*
*   @param[in] deviceMap - Devic Index
*
*   @return int Success - 0, Failure - Error Code
*
*   API to Stop Sensor.
*/
int MMWL_sensorStop(unsigned char deviceMap)
{
    int retVal = RL_RET_CODE_OK, timeOutCnt =0;
    retVal = rlSensorStop(deviceMap);
#ifndef ENABLE_TEST_SOURCE
    if (retVal == RL_RET_CODE_OK)
    {
        if ((rfDevCfg.aeControl & 0x2) == 0x0)
        {
            while (mmwl_bSensorStarted == 1U)
            {
                osiSleep(1); /*Sleep 1 msec*/
                timeOutCnt++;
                if (timeOutCnt > MMWL_API_RF_INIT_TIMEOUT)
                {
                    retVal = RL_RET_CODE_RESP_TIMEOUT;
                    break;
                }
            }
        }
    }
#endif
    return retVal;
}

/** @fn int MMWL_setContMode(unsigned char deviceMap)
*
*   @brief API to set continuous mode.
*
*   @param[in] deviceMap - Devic Index
*
*   @return int Success - 0, Failure - Error Code
*
*   API to set continuous mode.
*/
int MMWL_setContMode(unsigned char deviceMap)
{
    int retVal = RL_RET_CODE_OK;
    rlContModeCfg_t contModeCfgArgs = { 0 };
    contModeCfgArgs.digOutSampleRate = profileCfgArgs[0].digOutSampleRate;
    contModeCfgArgs.hpfCornerFreq1 = profileCfgArgs[0].hpfCornerFreq1;
    contModeCfgArgs.hpfCornerFreq2 = profileCfgArgs[0].hpfCornerFreq2;
    contModeCfgArgs.startFreqConst = profileCfgArgs[0].startFreqConst;
    contModeCfgArgs.txOutPowerBackoffCode = profileCfgArgs[0].txOutPowerBackoffCode;
    contModeCfgArgs.txPhaseShifter = profileCfgArgs[0].txPhaseShifter;

    /*read contModeCfgArgs from config file*/
    MMWL_readContModeConfig(&contModeCfgArgs);

    printf("Calling setContMode with\n digOutSampleRate[%d]\nstartFreqConst[%d]\ntxOutPowerBackoffCode[%d]\nRXGain[%d]\n\n", \
        contModeCfgArgs.digOutSampleRate, contModeCfgArgs.startFreqConst, contModeCfgArgs.txOutPowerBackoffCode, \
        contModeCfgArgs.rxGain);
    retVal = rlSetContModeConfig(deviceMap, &contModeCfgArgs);
    return retVal;
}

/** @fn int MMWL_dynChirpEnable(unsigned char deviceMap)
*
*   @brief API to enable Dynamic chirp feature.
*
*   @param[in] deviceMap - Devic Index
*
*   @return int Success - 0, Failure - Error Code
*
*   API to enable Dynamic chirp feature.
*/
int MMWL_dynChirpEnable(unsigned char deviceMap)
{
    int retVal = RL_RET_CODE_OK;
    rlDynChirpEnCfg_t dynChirpEnCfgArgs = { 0 };

    retVal = rlSetDynChirpEn(deviceMap, &dynChirpEnCfgArgs);
    return retVal;
}

/** @fn int MMWL_dynChirpConfig(unsigned char deviceMap)
*
*   @brief API to config chirp dynamically.
*
*   @param[in] deviceMap - Devic Index
*
*   @return int Success - 0, Failure - Error Code
*
*   API to config chirp dynamically.
*/
int  MMWL_setDynChirpConfig(unsigned char deviceMap)
{
    int retVal = RL_RET_CODE_OK;
    unsigned int cnt;
    rlDynChirpCfg_t * dataDynChirp[3U] = { &dynChirpCfgArgs[0], &dynChirpCfgArgs[1], &dynChirpCfgArgs[2]};

    dynChirpCfgArgs[0].programMode = 0;

    /* Configure NR1 for 48 chirps */
    dynChirpCfgArgs[0].chirpRowSelect = 0x10;
    dynChirpCfgArgs[0].chirpSegSel = 0;
    /* Copy this dynamic chirp config to other config and update chirp segment number */
    memcpy(&dynChirpCfgArgs[1], &dynChirpCfgArgs[0], sizeof(rlDynChirpCfg_t));
    memcpy(&dynChirpCfgArgs[2], &dynChirpCfgArgs[0], sizeof(rlDynChirpCfg_t));
    /* Configure NR2 for 48 chirps */
    dynChirpCfgArgs[1].chirpRowSelect = 0x20;
    dynChirpCfgArgs[1].chirpSegSel = 1;
    /* Configure NR3 for 48 chirps */
    dynChirpCfgArgs[2].chirpRowSelect = 0x30;
    dynChirpCfgArgs[2].chirpSegSel = 2;

    for (cnt = 0; cnt < 16; cnt++)
    {
        /* Reconfiguring frequency slope for 48 chirps */
        dynChirpCfgArgs[0].chirpRow[cnt].chirpNR1 |= (((3*cnt) & 0x3FU) << 8);
        dynChirpCfgArgs[0].chirpRow[cnt].chirpNR2 |= (((3*cnt + 1) & 0x3FU) << 8);
        dynChirpCfgArgs[0].chirpRow[cnt].chirpNR3 |= (((3*cnt + 2) & 0x3FU) << 8);
        /* Reconfiguring start frequency for 48 chirps */
        dynChirpCfgArgs[1].chirpRow[cnt].chirpNR1 |= 3*cnt;
        dynChirpCfgArgs[1].chirpRow[cnt].chirpNR2 |= 3*cnt + 1;
        dynChirpCfgArgs[1].chirpRow[cnt].chirpNR3 |= 3*cnt + 2;
        /* Reconfiguring ideal time for 48 chirps */
        dynChirpCfgArgs[2].chirpRow[cnt].chirpNR1 |= 3 * cnt;
        dynChirpCfgArgs[2].chirpRow[cnt].chirpNR2 |= 3 * cnt + 1;
        dynChirpCfgArgs[2].chirpRow[cnt].chirpNR3 |= 3 * cnt + 2;
    }

    printf("Calling DynChirpCfg with chirpSegSel[%d]\nchirpNR1[%d]\n\n", \
        dynChirpCfgArgs[0].chirpSegSel, dynChirpCfgArgs[0].chirpRow[0].chirpNR1);
    retVal = rlSetDynChirpCfg(deviceMap, 2U, &dataDynChirp[0]);
    return retVal;
}

/** @fn int MMWL_powerOff(unsigned char deviceMap)
*
*   @brief API to poweroff device.
*
*   @param[in] deviceMap - Devic Index
*
*   @return int Success - 0, Failure - Error Code
*
*   API to poweroff device.
*/
int MMWL_powerOff(unsigned char deviceMap)
{
    int retVal = RL_RET_CODE_OK;
    retVal = rlDevicePowerOff();
    mmwl_bInitComp = 0;
    mmwl_bStartComp = 0U;
    mmwl_bRfInitComp = 0U;
    mmwl_devSpiHdl = NULL;

    return retVal;
}

/** @fn int MMWL_lowPowerConfig(deviceMap)
*
*   @brief LowPower configuration API.
*
*   @param[in] deviceMap - Devic Index
*
*   @return int Success - 0, Failure - Error Code
*
*   LowPower configuration API.
*/
int MMWL_lowPowerConfig(unsigned char deviceMap)
{
    int retVal = RL_RET_CODE_OK;
    /* TBD - Read GUI Values */
    rlLowPowerModeCfg_t rfLpModeCfgArgs = { 0 };

    /*read rfLpModeCfgArgs from config file*/
    MMWL_readLowPowerConfig(&rfLpModeCfgArgs);

    retVal = rlSetLowPowerModeConfig(deviceMap, &rfLpModeCfgArgs);
    return retVal;
}

int setupMasterSocket(){
	WSADATA WSAData;
	SOCKADDR_IN serverAddr;
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.s_addr = inet_addr(MASTER_ADDR);
	serverAddr.sin_port = htons(MASTER_PORT);

	// Setup Master socket
	WSAStartup(MAKEWORD(2, 0), &WSAData);
	MasterSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	// Connect to Master
	printf("Connecting to Master on %s:%d\n", MASTER_ADDR, MASTER_PORT);
	int iCon = connect(MasterSocket, (SOCKADDR *)&serverAddr, sizeof(serverAddr));
	if (iCon == SOCKET_ERROR) {
		printf("Connect to Master failed.\n");
		closesocket(MasterSocket);
		return 0;
	}
	else{
		printf("Connected to Master.\n");
	}

	// Send data by socket
	char sendBuffer[1024];
	strcpy(sendBuffer, deviceID);
	int iSend = send(MasterSocket, sendBuffer, (int)strlen(sendBuffer), 0);
	if (iSend == SOCKET_ERROR) {
		printf("Send ID to Master failed.\n");
		closesocket(MasterSocket);
		return 0;
	}
	else{
		printf("ID of \"%s\" sent!\n", sendBuffer);
	}

	return 1;
}

void triggerSensorLoop(unsigned char deviceMap){
#ifdef CONTROL_BY_SOCKET
	printf("\n========================Waiting for commands from socket==============================\n\n");
	while (TRUE){
		// Receive command from master
		char recvBuffer[1024];
		int iRecv = recv(MasterSocket, recvBuffer, sizeof(recvBuffer), 0);
		recvBuffer[iRecv] = '\0';
		if (iRecv <= 0) {
			printf("Receive failed.\n");
			MMWL_sensorStop(deviceMap);	// Stop sensor anyway
			closesocket(MasterSocket);
			return;
		}
		else{
			printf("Command \"%s\" received.\n", recvBuffer);
		}
		if (!strcmp(recvBuffer, "start")){
			printf("Starting sensor...\n\n");
			int retVal = MMWL_sensorStart(deviceMap);
			if (retVal != RL_RET_CODE_OK)
			{
				printf("Sensor Start failed with error code %d \n\n", retVal);
				return;
			}
			else
			{
				printf(">>>>Sensor Start successful \n\n");
			}
		}else if(!strcmp(recvBuffer, "stop")){
			printf("Stopping sensor...\n\n");
			int retVal = MMWL_sensorStop(deviceMap);
			if (retVal != RL_RET_CODE_OK)
			{
				if (retVal == RL_RET_CODE_FRAME_ALREADY_ENDED)
				{
					printf("Frame is already stopped when sensorStop CMD was issued\n");
				}
				else
				{
					printf("Sensor Stop failed with error code %d \n\n", retVal);
					return -1;
				}
			}
			else
			{
				printf(">>>>Sensor Stop successful \n\n");
			}
		}else{
			printf("==== Illegal command received!\n\n ====");
		}
	}
#else
	char key = '\0';
	while (TRUE)
	{
		printf("\n========================Press Enter to Start, press anykey+enter to exit==============================\n\n");
		key = getchar();
		if (key != 10){
			MMWL_sensorStop(deviceMap);	// Stop sensor anyway
			return 1;
		}
		int retVal = MMWL_sensorStart(deviceMap);
		if (retVal != RL_RET_CODE_OK)
		{
			printf("Sensor Start failed with error code %d \n\n", retVal);
			return -1;
		}
		else
		{
			printf(">>>>Sensor Start successful \n\n");
		}

		printf("\n========================Press Enter to Stop, press anykey+Enter to exit===========================\n\n");
		key = getchar();
		if (key != 10){
			MMWL_sensorStop(deviceMap);	// Stop sensor anyway
			return 1;
		}
		retVal = MMWL_sensorStop(deviceMap);
		if (retVal != RL_RET_CODE_OK)
		{
			if (retVal == RL_RET_CODE_FRAME_ALREADY_ENDED)
			{
				printf("Frame is already stopped when sensorStop CMD was issued\n");
			}
			else
			{
				printf("Sensor Stop failed with error code %d \n\n", retVal);
				return -1;
			}
		}
		else
		{
			printf(">>>>Sensor Stop successful \n\n");
		}
	}
#endif
}

int MMWL_App()
{
#ifdef CONTROL_BY_SOCKET
	printf("\n========================Socket connecting to Master======================\n\n");
	if (!setupMasterSocket()){
		printf("Setup socket connection to Master failed!\n\n");
		return -1;
	}
#endif

	int retVal = RL_RET_CODE_OK;
	unsigned char deviceMap = RL_DEVICE_MAP_CASCADED_1;
	
	printf("\n=============================Open Config File===========================\n\n");
    retVal = MMWL_openConfigFile();
    if (retVal != RL_RET_CODE_OK)
    {
        printf("failed to Open configuration file\n");
        return -1;
    }
	else
	{
		printf(">>>>success to Open configuration file\n");
	}

    /*  \subsection     api_sequence1     Seq 1 - Call Power ON API
    The mmWaveLink driver initializes the internal components, creates Mutex/Semaphore,
    initializes buffers, register interrupts, bring mmWave front end out of reset.
    */
	printf("\n=============================Device Power On===========================\n\n");
    retVal = MMWL_powerOnMaster(deviceMap);
    if (retVal != RL_RET_CODE_OK)
    {
        printf("mmWave Device Power on failed with error %d \n\n", retVal);
        return -1;
    }
    else
    {
        printf(">>>>mmWave Device Power on success \n\n");
    }

    /* Change CRC Type of Async Event generated by MSS to what is being requested by user in mmwaveconfig.txt */
    /*retVal = MMWL_setDeviceCrcType(deviceMap);
    if (retVal != RL_RET_CODE_OK)
    {
        printf("CRC Type set for MasterSS failed for deviceMap %u with error code %d \n\n",
                deviceMap, retVal);
        return -1;
    }
    else
    {
        printf("CRC Type set for MasterSS success for deviceMap %u \n\n", deviceMap);
    }*/

    /*  \subsection     api_sequence3     Seq 3 - Enable the mmWave Front end (Radar/RF subsystem)
    The mmWave Front end once enabled runs boot time routines and upon completion sends asynchronous event
    to notify the result
    */
	printf("\n=============================Enable RF===========================\n\n");
    retVal = MMWL_rfEnable(deviceMap);
    if (retVal != RL_RET_CODE_OK)
    {
        printf("Radar/RF subsystem Power up failed with error %d \n\n", retVal);
        return -1;
    }
    else
    {
        printf(">>>>Radar/RF subsystem Power up success \n\n");
    }

    /*  \subsection     api_sequence4     Seq 4 - Basic/Static Configuration
    The mmWave Front end needs to be configured for mmWave Radar operations. basic
    configuration includes Rx/Tx channel configuration, ADC configuration etc
    */
    printf("\n======================Basic/Static Configuration======================\n");
    retVal = MMWL_basicConfiguration(deviceMap, 0);
    if (retVal != RL_RET_CODE_OK)
    {
        printf("Basic/Static configuration failed with error %d \n", retVal);
        return -1;
    }
    else
    {
        printf(">>>>Basic/Static configuration success \n\n");
    }

    /*  \subsection     api_sequence5     Seq 5 - Initializes the mmWave Front end
    The mmWave Front end once configured needs to be initialized. During initialization
    mmWave Front end performs calibration and once calibration is complete, it
    notifies the application using asynchronous event
    */
	printf("\n==========================RF Initilization============================\n");
    retVal = MMWL_rfInit(deviceMap);
    if (retVal != RL_RET_CODE_OK)
    {
        printf("RF Initialization/Calibration failed with error code %d \n\n", retVal);
        return -1;
    }
    else
    {
        printf(">>>>RF Initialization/Calibration successful\n\n");
    }

    /*  \subsection     api_sequence6     Seq 6 - FMCW profile configuration
    TI mmWave devices supports Frequency Modulated Continuous Wave(FMCW) Radar. User
    Need to define characteristics of FMCW signal using profile configuration. A profile
    contains information about FMCW signal such as Start Frequency (76 - 81 GHz), Ramp
    Slope (e.g 30MHz/uS). Idle Time etc. It also configures ADC samples, Sampling rate,
    Receiver gain, Filter configuration parameters

    \ Note - User can define upto 4 different profiles
    */
    printf("\n======================FMCW(Profile/Chirp) Configuration======================\n");
    retVal = MMWL_profileConfig(deviceMap);
    if (retVal != RL_RET_CODE_OK)
    {
        printf("Profile Configuration failed with error code %d \n\n", retVal);
        return -1;
    }
    else
    {
        printf(">>>>Profile Configuration success \n\n");
    }

    /*  \subsection     api_sequence7     Seq 7 - FMCW chirp configuration
    A chirp is always associated with FMCW profile from which it inherits coarse information
    about FMCW signal. Using chirp configuration user can further define fine
    variations to coarse parameters such as Start Frequency variation(0 - ~400 MHz), Ramp
    Slope variation (0 - ~3 MHz/uS), Idle Time variation etc. It also configures which transmit channels to be used
    for transmitting FMCW signal.

    \ Note - User can define upto 512 unique chirps
    */
    retVal = MMWL_chirpConfig(deviceMap);
    if (retVal != RL_RET_CODE_OK)
    {
        printf("Chirp Configuration failed with error %d \n\n", retVal);
        return -1;
    }
    else
    {
        printf(">>>>Chirp Configuration success \n\n", deviceMap);
    }

    /*  \subsection     api_sequence8     Seq 8 - Data Path (CSI2/LVDS) Configuration
    TI mmWave device supports CSI2 or LVDS interface for sending RAW ADC data. mmWave device
    can also send Chirp Profile and Chirp Quality data over LVDS/CSI2. User need to select
    the high speed interface and also what data it expects to receive.

    \ Note - This API is only applicable for AWR1243 when mmWaveLink driver is running on External Host
    */
    printf("\n==================Data Path(LVDS/CSI2) Configuration==================\n");
    retVal = MMWL_dataPathConfig(deviceMap);
    if (retVal != RL_RET_CODE_OK)
    {
        printf("Data Path Configuration failed with error %d \n\n", retVal);
        return -1;
    }
    else
    {
        printf(">>>>Data Path Configuration successful \n\n");
    }

    /*  \subsection     api_sequence9     Seq 9 - CSI2/LVDS CLock and Data Rate Configuration
    User need to configure what data rate is required to send the data on high speed interface. For
    e.g 150mbps, 300mbps etc.
    \ Note - This API is only applicable for AWR1243 when mmWaveLink driver is running on External Host
    */
    retVal = MMWL_hsiClockConfig(deviceMap);
    if (retVal != RL_RET_CODE_OK)
    {
        printf("CSI2/LVDS Clock Configuration failed with error %d \n\n", retVal);
        return -1;
    }
    else
    {
        printf(">>>>CSI2/LVDS Clock Configuration success \n\n");
    }

    /*  \subsection     api_sequence10     Seq 10 - CSI2/LVDS Lane Configuration
    User need to configure how many LVDS/CSI2 lane needs to be enabled
    \ Note - This API is only applicable for AWR1243 when mmWaveLink driver is running on External Host
    */
    retVal = MMWL_hsiLaneConfig(deviceMap);
    if (retVal != RL_RET_CODE_OK)
    {
        printf("CSI2/LVDS Lane Config failed with error %d \n", retVal);
        return -1;
    }
    else
    {
        printf(">>>>CSI2/LVDS Lane Configuration success\n");
    }
    
	printf("\n========================Frame Configuration=============================\n\n");
#if(gLinkAdvanceFrameTest)
	{
		/*  \subsection     api_sequence11     Seq 11 - FMCW Advance frame configuration
		A frame defines sequence of chirps and how this sequence needs to be repeated over time.
		*/
		retVal = MMWL_advFrameConfig(deviceMap);

		if (retVal != RL_RET_CODE_OK)
		{
			printf("Adv Frame Configuration failed with error %d \n", retVal);
			return -1;
		}
		else
		{
			printf(">>>>Adv Frame Configuration success \n");
		}
	}
#else
	{
		/*  \subsection     api_sequence11     Seq 11 - FMCW frame configuration
		A frame defines sequence of chirps and how this sequence needs to be repeated over time.
		*/
		retVal = MMWL_frameConfig(deviceMap);
		if (retVal != RL_RET_CODE_OK)
		{
			printf("Frame Configuration failed with error %d \n", retVal);
			return -1;
		}
		else
		{
			printf(">>>>Frame Configuration success\n");
		}
	}
#endif

    /*if (gLinkContModeTest == TRUE)
    {
        retVal = MMWL_setContMode(deviceMap);
        if (retVal != RL_RET_CODE_OK)
        {
            printf("Continuous mode Config failed for deviceMap %u with error code %d \n\n",
                deviceMap, retVal);
            return -1;
        }
        else
        {
            printf(">>>>Continuous mode Config successful for deviceMap %u \n\n", deviceMap);
        }
    }*/

	// ========================================
	printf("\n========================Toggle Sensor Frame=============================\n\n");
	triggerSensorLoop(deviceMap);
	// ========================================

	/* Note- Before Calling this API user must feed in input signal to device's pins,
	else device will return garbage data in GPAdc measurement over Async event.
	Measurement data is stored in 'rcvGpAdcData' structure after this API call. */
	/*printf("\n=============================GPAdc Config===========================\n\n");
	retVal = MMWL_gpadcMeasConfig(deviceMap);
	if (retVal != RL_RET_CODE_OK)
	{
		printf("GPAdc measurement API failed with error code %d \n\n", retVal);
		return -1;
	}
	else
	{
		printf(">>>>GPAdc measurement API success\n\n");
	}*/

	printf("\n=============================Device Power Off===========================\n\n");
    retVal = MMWL_powerOff(deviceMap);
    if (retVal != RL_RET_CODE_OK)
    {
        printf("Device power off failed with error code %d \n\n", retVal);
        return -1;
    }
    else
    {
        printf(">>>>Device power off success\n\n");
    }
	
	printf("\n=============================Close Config File===========================\n\n");
    MMWL_closeConfigFile();

#ifdef CONTROL_BY_SOCKET
	/* Release listening socket */
	closesocket(MasterSocket);
#endif

    return 0;
}

void main(void)
{
    int retVal;

    printf("=========== mmWaveLink Example Application =========== \n");
    retVal = MMWL_App();
    if(retVal == RL_RET_CODE_OK)
    {
        printf("=========== mmWaveLink Example Application execution Successful=========== \n");
    }
    else
    {
        printf("=========== mmWaveLink Example Application execution Failed =========== \n");
    }

    /* Wait for Enter click */
    getchar();
    printf("=========== mmWaveLink Example Application: Exit =========== \n");

}
