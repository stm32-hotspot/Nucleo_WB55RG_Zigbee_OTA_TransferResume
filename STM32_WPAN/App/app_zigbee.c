/**
  ******************************************************************************
  * File Name          : App/app_zigbee.c
  * Description        : Zigbee Application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2019-2021 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "app_common.h"
#include "app_entry.h"
#include "dbg_trace.h"
#include "app_zigbee.h"
#include "zigbee_interface.h"
#include "shci.h"
#include "stm_logging.h"
#include "app_conf.h"
#include "stm32wbxx_core_interface_def.h"
#include "zigbee_types.h"
#include "stm32_seq.h"

#include <assert.h>
#include "zcl/zcl.h"
#include "zcl/general/zcl.ota.h"

#include "ee.h"
#include "hw_flash.h"

/* Private defines -----------------------------------------------------------*/
#define APP_ZIGBEE_STARTUP_FAIL_DELAY          500U
#define SW1_ENDPOINT                           17
#define CHANNEL                                19

#define HW_TS_SERVER_1S_NB_TICKS               (1*1000*1000/CFG_TS_TICK_VAL)
#define LED_TOGGLE_TIMING                      (0.1*1000*1000/CFG_TS_TICK_VAL)  /**< 0.5s */
#define OTA_DOWNLOAD_RETRY_DELAY               (0.1*1000*1000/CFG_TS_TICK_VAL)  /**< 0.5s */
#define CFG_NVM                                1u         /* use FLASH */
//#define OTA_DISPLAY_TIMING                   1u         /* Display all times (load transaction & NVM or Eeprom save )  */
#define OTA_PREVENT_DOWNGRADE                  TRUE  /* For security reason firmware downgrade should be prenvented */
#define OTA_ABORT_RETRY_ENABLE                 TRUE  /* Enable download resume retries after abort */
#define USE_TAG_WRITE_CB                       False /* Set to TRUE to handle multiple tags in single OTA image  */


/* external definition */
enum ZbStatusCodeT ZbStartupWait(struct ZigBeeT *zb, struct ZbStartupT *config);

/* Private function prototypes -----------------------------------------------*/

/* General application functions */
static void APP_ZIGBEE_StackLayersInit(void);
static void APP_ZIGBEE_ConfigEndpoints(void);
static void APP_ZIGBEE_NwkForm(void);
static void APP_ZIGBEE_App_Init(void);
static void APP_ZIGBEE_SW1_Process(void);
static void APP_ZIGBEE_SW2_Process(void);
static void APP_ZIGBEE_SW3_Process(void);

static void APP_ZIGBEE_TraceError(const char *pMess, uint32_t ErrCode);
static void APP_ZIGBEE_CheckWirelessFirmwareInfo(void);

static void Wait_Getting_Ack_From_M0(void);
static void Receive_Ack_From_M0(void);
static void Receive_Notification_From_M0(void);

/* ZCL OTA cluster related functions */
static void APP_ZIGBEE_OTA_Client_Init(void);
static void APP_ZIGBEE_OTA_Client_ServerDiscovery( void );

static void APP_ZIGBEE_OTA_Client_DiscoverComplete_cb(struct ZbZclClusterT *clusterPtr, enum ZclStatusCodeT status,void *arg);
static enum ZclStatusCodeT APP_ZIGBEE_OTA_Client_ImageNotify_cb(struct ZbZclClusterT *clusterPtr, uint8_t payload_type,
                                                                uint8_t jitter, struct ZbZclOtaImageDefinition *image_definition,
                                                                struct ZbApsdeDataIndT *data_ind, struct ZbZclHeaderT *zcl_header);
static void APP_ZIGBEE_OTA_Client_QueryNextImage_cb(struct ZbZclClusterT *clusterPtr, enum ZclStatusCodeT status,
                                                    struct ZbZclOtaImageDefinition *image_definition, uint32_t image_size, void *arg);
static enum ZclStatusCodeT APP_ZIGBEE_OTA_Client_WriteImage_cb(struct ZbZclClusterT *clusterPtr, struct ZbZclOtaHeader *header,
                                                               uint8_t length, uint8_t *data, void *arg);
#ifdef USE_TAG_WRITE_CB
static enum ZclStatusCodeT APP_ZIGBEE_OTA_Client_WriteTag_cb(struct ZbZclClusterT * clusterPtr, struct ZbZclOtaHeader * header,
                                                            uint16_t tag_id, uint32_t tag_length, uint8_t data_length,
                                                            uint8_t * data, void * arg);
#endif
static enum ZclStatusCodeT APP_ZIGBEE_OTA_Client_ImageValidate_cb(struct ZbZclClusterT *clusterPtr, struct ZbZclOtaHeader *header, void *arg);
static void APP_ZIGBEE_OTA_Client_Reboot_cb(struct ZbZclClusterT *clusterPtr, void *arg);
static enum ZclStatusCodeT APP_ZIGBEE_OTA_Client_AbortDownload_cb(struct ZbZclClusterT *clusterPtr, enum ZbZclOtaCommandId commandId, void *arg);

/* OTA application related functions */
static bool APP_ZIGBEE_OTA_Client_CheckPriviousDownload(struct ZbZclOtaImageDefinition *image_definition);
static inline int APP_ZIGBEE_FindImageType(unsigned int fileType);
static inline void APP_ZIGBEE_OTA_Client_Request_Upgrade(void);
static inline void APP_ZIGBEE_OTA_Client_StartDownload(void);
static inline APP_ZIGBEE_StatusTypeDef APP_ZIGBEE_OTA_Client_WriteFirmwareData(struct Zigbee_OTA_client_info* client_info);
static inline APP_ZIGBEE_StatusTypeDef APP_ZIGBEE_CheckDeviceCapabilities(void);
static void APP_ZIGBEE_PerformReset(void);
static void APP_ZIGBEE_LEDToggle(void);

static inline uint32_t GetFirstSecureSector(void);
static inline void Delete_Sectors(void);

/* NVM related function */
static bool APP_ZIGBEE_OTA_ctx_save_nvm(struct Zigbee_OTA_client_info* client_info);
static bool APP_ZIGBEE_OTA_ctx_load_nvm(void);
static bool APP_ZIGBEE_persist_load(void);
static bool APP_ZIGBEE_persist_save(void);
static void APP_ZIGBEE_persist_delete(void);
static void APP_ZIGBEE_persist_notify_cb(struct ZigBeeT *zb, void *cbarg);
static enum ZbStatusCodeT APP_ZIGBEE_ZbStartupPersist(struct ZigBeeT *zb);
static void APP_ZIGBEE_PersistCompleted_callback(void);
#ifdef CFG_NVM
static void APP_ZIGBEE_NVM_Init(void);
static bool APP_ZIGBEE_NVM_Read(void);
static bool APP_ZIGBEE_NVM_Write(void);
static void APP_ZIGBEE_NVM_Erase(void);
#endif /* CFG_NVM */

/* Private variables -----------------------------------------------*/
static TL_CmdPacket_t *p_ZIGBEE_otcmdbuffer;
static TL_EvtPacket_t *p_ZIGBEE_notif_M0_to_M4;
static TL_EvtPacket_t *p_ZIGBEE_request_M0_to_M4;
static __IO uint32_t CptReceiveNotifyFromM0 = 0;
static __IO uint32_t CptReceiveRequestFromM0 = 0;

PLACE_IN_SECTION("MB_MEM1") ALIGN(4) static TL_ZIGBEE_Config_t ZigbeeConfigBuffer;
PLACE_IN_SECTION("MB_MEM2") ALIGN(4) static TL_CmdPacket_t ZigbeeOtCmdBuffer;
PLACE_IN_SECTION("MB_MEM2") ALIGN(4) static uint8_t ZigbeeNotifRspEvtBuffer[sizeof(TL_PacketHeader_t) + TL_EVT_HDR_SIZE + 255U];
PLACE_IN_SECTION("MB_MEM2") ALIGN(4) static uint8_t ZigbeeNotifRequestBuffer[sizeof(TL_PacketHeader_t) + TL_EVT_HDR_SIZE + 255U];

/* OTA app variables */
const struct OTA_currentFileVersion OTA_currentFileVersionTab[] = {
  {fileType_COPRO_WIRELESS, CURRENT_FW_COPRO_WIRELESS_FILE_VERSION},
  {fileType_APP, CURRENT_FW_APP_FILE_VERSION},
};

static struct Zigbee_OTA_client_info OTA_client_info;
static struct ZbZclOtaClientConfig client_config = {
  .profile_id = ZCL_PROFILE_HOME_AUTOMATION,
  .endpoint = SW1_ENDPOINT,
  .activation_policy = ZCL_OTA_ACTIVATION_POLICY_SERVER,
  .timeout_policy = ZCL_OTA_TIMEOUT_POLICY_APPLY_UPGRADE,
};




struct zigbee_app_info {
  bool has_init;
  struct ZigBeeT *zb;
  enum ZbStatusCodeT join_status;
  uint32_t join_delay;
  uint32_t persistNumWrites;
  bool fresh_startup;

  struct ZbZclClusterT *ota_client;
};

static struct zigbee_app_info zigbee_app_info;

static uint8_t      TS_ID_LED;
static uint8_t      TS_DOWNLOAD_RESUME;
/* NVM variables */
/* cache in uninit RAM to store/retrieve persistent data */
union cache
{
  uint8_t  U8_data[ST_PERSIST_MAX_ALLOC_BUFFER_SZ];   // In bytes
  uint32_t U32_data[ST_PERSIST_MAX_ALLOC_SZ / 4U];    // In U32 words
};

__attribute__ ((section(".noinit"))) union cache cache_persistent_data;
__attribute__ ((section(".noinit"))) union cache cache_diag_reference;

/* timer to delay reading attribute back from persistence */
static uint8_t  TS_ID1;
static uint8_t  TS_ID2;
static uint8_t  bPersistDataUsed;

/* Functions Definition ------------------------------------------------------*/

/**
 * @brief  OTA client Discover callback
 * @param  clusterPtr: ZCL Cluster pointer
 * @param  arg: Passed argument
 * @retval None
 */
static void APP_ZIGBEE_OTA_Client_DiscoverComplete_cb(struct ZbZclClusterT *clusterPtr, enum ZclStatusCodeT status,void *arg){
  enum ZclStatusCodeT internal_status = ZCL_STATUS_SUCCESS;
  uint64_t requested_server_ext = 0;
  //UNUSED (status);

  if (status == ZCL_STATUS_SUCCESS )
  {
    /* The OTA server extended address in stored in ZCL_OTA_ATTR_UPGRADE_SERVER_ID attribute */
    requested_server_ext = ZbZclAttrIntegerRead(zigbee_app_info.ota_client, ZCL_OTA_ATTR_UPGRADE_SERVER_ID, NULL, &internal_status);
    UNUSED (requested_server_ext);
    if(internal_status != ZCL_STATUS_SUCCESS){
      APP_DBG("ZbZclAttrIntegerRead failed.\n");
    }

    APP_DBG("OTA Server located ...");
    UTIL_SEQ_SetEvt(EVENT_ZIGBEE_OTA_SERVER_FOUND);
  }
  else
  {
    APP_DBG("OTA Server not found after TimeOut. Retry a discovery");
    UTIL_SEQ_SetTask( 1U << CFG_TASK_ZIGBEE_OTA_SERVER_DISCOVERY, CFG_SCH_PRIO_0 );
  }
}

/**
 * @brief  OTA client Image Notify callback
 * @param  clusterPtr: ZCL Cluster pointer
 * @param  payload_type: Payload type
 * @param  jitter: Upgrade jitter given to the client
 * @param  image_definition: Image query definition pointer
 * @param  data_ind: APS layer packet info
 * @param  zcl_header: ZCL header
 * @retval ZCL status code
 */
static enum ZclStatusCodeT APP_ZIGBEE_OTA_Client_ImageNotify_cb(struct ZbZclClusterT *clusterPtr, uint8_t payload_type,
                                                                uint8_t jitter, struct ZbZclOtaImageDefinition *image_definition,
                                                                struct ZbApsdeDataIndT *data_ind, struct ZbZclHeaderT *zcl_header){
  bool checkFileType = false, checkFileVersion = false;
  int pos = -1;

  APP_DBG("[OTA] Image Notify request received.");

  /* Print message info according to Image Notify request payload type */
  switch(payload_type){
    case ZCL_OTA_NOTIFY_TYPE_FILE_VERSION:
      APP_DBG("[OTA] Available upgrade jitter: %d.", jitter);
      APP_DBG("[OTA] Available upgrade manufacturer code: %d.", image_definition->manufacturer_code);
      APP_DBG("[OTA] Available upgrade image type: 0x%04x.", image_definition->image_type);
      APP_DBG("[OTA] Available upgrade (new) file version: 0x%08x.", image_definition->file_version);

      if(image_definition->manufacturer_code != ST_ZIGBEE_MANUFACTURER_CODE){
        APP_DBG("[OTA] Unauthorized OTA upgrade manufacturer.\n");
        return ZCL_STATUS_FAILURE;
      }

      /* check image type and file version */
      checkFileType = true;
      checkFileVersion = true;
      break;

    default:
      APP_DBG("[OTA] Not supported payload type.\n");
      return ZCL_STATUS_FAILURE;
  }

  /* Check file type */
  if(checkFileType){
    pos = APP_ZIGBEE_FindImageType(image_definition->image_type);
    if(pos<0){
      /* Unknown file type */
      APP_DBG("[OTA] Unknown file type type.\n");
      return ZCL_STATUS_FAILURE;
    }
  }

  /* Check file version */
  if(checkFileVersion){
    if(OTA_currentFileVersionTab[pos].fileVersion == image_definition->file_version){
      APP_DBG("[OTA] Already up-to-date for this file type.\n");
      return ZCL_STATUS_FAILURE;
    }

    #if (OTA_PREVENT_DOWNGRADE)
    if(OTA_currentFileVersionTab[pos].fileVersion > image_definition->file_version){
      APP_DBG("[OTA] Can't downgrade firmeware version : Current version 0x%08x more recent than server version  0x%08x.\n",OTA_currentFileVersionTab[pos].fileVersion,image_definition->file_version);
      return ZCL_STATUS_FAILURE;
    }
    #endif
  }

  APP_DBG("[OTA] Everything is OK, can process to the OTA upgrade.\n");
  /* Requesting an upgrade according to server image type
   * => re-send an Query Next Image request  back to the upgrade server */
  OTA_client_info.image_type = image_definition->image_type;
  OTA_client_info.current_file_version = OTA_currentFileVersionTab[pos].fileVersion;
  UTIL_SEQ_SetTask(1U << CFG_TASK_ZIGBEE_OTA_REQUEST_UPGRADE, CFG_SCH_PRIO_0);

  return ZCL_STATUS_SUCCESS;
}

/**
 * @brief  OTA client Query Next Image callback
 * @param  clusterPtr: ZCL Cluster pointer
 * @param  status: ZCL status code returned by the server for the requested image
 * @param  image_definition: Image query definition pointer
 * @param  image_size: OTA file format image total size
 * @param  arg: Passed arg
 * @retval ZCL status code
 */
static void APP_ZIGBEE_OTA_Client_QueryNextImage_cb(struct ZbZclClusterT *clusterPtr, enum ZclStatusCodeT status,
                                                    struct ZbZclOtaImageDefinition *image_definition, uint32_t image_size, void *arg){
  struct Zigbee_OTA_client_info* client_info = (struct Zigbee_OTA_client_info*) arg;
  bool ota_resume_status = false;
  APP_DBG("[OTA] Client Query Next Image request response received.");
  if(status != ZCL_STATUS_SUCCESS){
    APP_DBG("[OTA] A such image is not available.\n");
    return;
  } else {
    APP_DBG("[OTA] A such image is available.");
  }

  switch(image_definition->image_type){
    case fileType_COPRO_WIRELESS:
      client_info->ctx.base_address = FUOTA_COPRO_FW_BINARY_ADDRESS;
      client_info->ctx.magic_keyword = FUOTA_MAGIC_KEYWORD_COPRO_WIRELESS;
      client_info->ctx.file_type = fileType_COPRO_WIRELESS;
      break;

    case fileType_APP:
      client_info->ctx.base_address = FUOTA_APP_FW_BINARY_ADDRESS;
      client_info->ctx.magic_keyword = FUOTA_MAGIC_KEYWORD_APP;
      client_info->ctx.file_type = fileType_APP;
      break;

    default:
      APP_DBG("[OTA] Error, unknown image type.\n");
      return;
  }
  client_info->requested_image_size = image_size;
  client_info->ctx.binary_srv_crc = 0;
  client_info->ctx.binary_calc_crc = 0;
  client_info->ctx.file_version = image_definition->file_version;
  if(APP_ZIGBEE_CheckDeviceCapabilities() != APP_ZIGBEE_OK){
    APP_DBG("[OTA] Not enough space. No download.\n");
    return;
  }

/* Check if we have previous downloaded data */
if(client_info->flags & OTA_CLIENT_CTX_FOUND_FLAG)
{
  /* Check and validate previous OTA data in flash (in case we resume a transfer) */
  /* Validate date against received header */
  ota_resume_status = APP_ZIGBEE_OTA_Client_CheckPriviousDownload(image_definition);
  if(ota_resume_status)
  {
    /* Checks are ok , we can resume download */
    client_info->flags |= OTA_CLIENT_RESUME_DOWNLOAD_FLAG;
    APP_DBG("[OTA] Resuming download for this image type.\n");
  }
  else
  {
    /* Image type header mismatch previous data in NVM , clean up to start a fresh download */
    client_info->flags &= ~OTA_CLIENT_CTX_FOUND_FLAG;
    APP_DBG("Delete_Sectors");
    Delete_Sectors();
    APP_DBG("[OTA] checks failled , starting a fresh download.\n");
    
  }
}
if(client_info->flags & OTA_CLIENT_RESUME_DOWNLOAD_FLAG){
  /* Check previous ota state machine */
  if(client_info->zigbee_ota_ctx_nvm.OtaCurrentState == DOWNLOADING_IMAGE)
  {
    /* Continue downloading */
  }
  if(client_info->zigbee_ota_ctx_nvm.OtaCurrentState == VERIFYING_IMAGE)
  {
    static enum ZclStatusCodeT  ret_val;
    /* Jump to verification stage */
    ret_val = APP_ZIGBEE_OTA_Client_ImageValidate_cb(NULL,NULL,client_info);
    if(ret_val == ZCL_STATUS_SUCCESS)
    {
      /* Schedule reboot if validation is ok */
      APP_DBG("**************************************************************");
      APP_DBG("[OTA] Rebooting.");
      HAL_Delay(100);
      UTIL_SEQ_SetTask(1U << (uint32_t)CFG_TASK_FUOTA_RESET, CFG_SCH_PRIO_0);
      return;
    }
  }
}
  client_info->OTA_state = DOWNLOADING_IMAGE;
  APP_DBG("[OTA] For image type 0x%04x, %d byte(s) will be downloaded.", image_definition->image_type, image_size);
  UTIL_SEQ_SetTask(1U << CFG_TASK_ZIGBEE_OTA_START_DOWNLOAD, CFG_SCH_PRIO_0);
  APP_DBG("[OTA] Starting download.\n");
}


/**
 * @brief  OTA client Calc CRC for a payload
 * @param  data: payload to calc
 * @param  size: length of payload
 */
static void APP_ZIGBEE_OTA_Client_Crc_Calc( struct Zigbee_OTA_client_info * client_info ) {
  uint8_t     modulo;
  uint16_t    index, size;
  uint32_t *  crc_data;

  // -- Prepare pointer & size --
  size = client_info->write_info.firmware_buffer_current_offset;
  crc_data = (uint32_t *)client_info->write_info.firmware_buffer;
  modulo = size % 4u;
  if ( modulo != 0u )
  {
      memset( &crc_data[size], 0, ( 4u - modulo ) );
      size  += ( 4u - modulo );
  }

  for ( index = 0; index < ( size / 4u ); index++ )
  {
    client_info->ctx.binary_calc_crc ^= crc_data[index];
  }
}


/**
 * @brief  OTA client Write Image callback
 * @param  clusterPtr: ZCL Cluster pointer
 * @param  header: ZCL OTA file format image header
 * @param  length: received chunk length
 * @param  data: received chunk payload
 * @param  arg: Passed arg
 * @retval ZCL status code
 */
static enum ZclStatusCodeT APP_ZIGBEE_OTA_Client_WriteImage_cb(struct ZbZclClusterT *clusterPtr, struct ZbZclOtaHeader *header,
                                                               uint8_t length, uint8_t * data, void *arg){
  static uint32_t current_offset = 0;

  struct Zigbee_OTA_client_info* client_info = (struct Zigbee_OTA_client_info*) arg;
  enum ZclStatusCodeT status = ZCL_STATUS_SUCCESS;
  uint8_t size = 0;
  uint8_t remaining_size = 0;
#ifdef OTA_DISPLAY_TIMING
  static uint32_t  lStartTime = 0;
  uint32_t  lStopTime, lTime1, lTime2;
#endif // OTA_DISPLAY_TIMING
  current_offset += length;
  size = length;
  /* Check if we can resume previous download (if any) */
  if(client_info->flags & OTA_CLIENT_RESUME_DOWNLOAD_FLAG)
  {
    /* Resume OTA process from a previsouly downloaded file */
    current_offset+= client_info->zigbee_ota_ctx_nvm.flash_offset;
    /* reset flag */
    client_info->flags &= ~OTA_CLIENT_RESUME_DOWNLOAD_FLAG;
    /* Update ota cluster ZCL_OTA_ATTR_FILE_OFFSET attribute, need to skip header length (which is sent with every block response ) + TAG length */
    if(ZbZclAttrIntegerWrite(zigbee_app_info.ota_client,ZCL_OTA_ATTR_FILE_OFFSET,client_info->zigbee_ota_ctx_nvm.flash_offset+header->header_length+OTA_HEADER_TAG_SIZE  ) != ZCL_STATUS_SUCCESS)
    {
      APP_DBG("[OTA] FUOTA failed to update ota cluster attr with value offset= 0x%04X)", current_offset);
      return ZCL_STATUS_FAILURE;
    }

    APP_DBG("[OTA] FUOTA Transfer resuming from NVM ( offset= 0x%04X)", current_offset);
    return status;
  }
  if(client_info->write_info.firmware_buffer_current_offset + size > RAM_FIRMWARE_BUFFER_SIZE)
  {
    size = RAM_FIRMWARE_BUFFER_SIZE - client_info->write_info.firmware_buffer_current_offset;
    remaining_size = length - size;
    client_info->write_info.buffer_full = true;
  } else if(client_info->write_info.firmware_buffer_current_offset+size == RAM_FIRMWARE_BUFFER_SIZE){
    client_info->write_info.buffer_full = true;
  }

  memcpy((client_info->write_info.firmware_buffer+client_info->write_info.firmware_buffer_current_offset), data, size);
  client_info->write_info.firmware_buffer_current_offset += size;

  if(client_info->write_info.buffer_full){
#ifdef OTA_DISPLAY_TIMING
    lStopTime = HAL_GetTick();
#endif // OTA_DISPLAY_TIMING

    /* Write to Flash Memory */
    if ( APP_ZIGBEE_OTA_Client_WriteFirmwareData(client_info) != APP_ZIGBEE_OK ){
      return ZCL_STATUS_FAILURE;
    }

#ifdef OTA_DISPLAY_TIMING
    /* Display Transfer Progress */
    lTime1 = lStopTime - lStartTime;
    lTime2 = ( HAL_GetTick() - lStopTime );
    APP_DBG("[OTA] FUOTA Transfer (current_offset = 0x%04X, load time = %d ms and save time = %d ms)", current_offset, lTime1, lTime2);
#else // OTA_DISPLAY_TIMING
    APP_DBG("[OTA] FUOTA Transfer (current_offset = 0x%04X)", current_offset);
#endif // OTA_DISPLAY_TIMING

    // -- Calc CRC --
    APP_ZIGBEE_OTA_Client_Crc_Calc( client_info );

    memset(client_info->write_info.firmware_buffer, 0, RAM_FIRMWARE_BUFFER_SIZE);
    memcpy(client_info->write_info.firmware_buffer, data+size, remaining_size);
    client_info->write_info.firmware_buffer_current_offset = remaining_size;
    client_info->write_info.buffer_full = false;

#ifdef OTA_DISPLAY_TIMING
    lStartTime = HAL_GetTick();
#endif // OTA_DISPLAY_TIMING
  }

  /* Handling donwload pause request */
  if(client_info->flags & OTA_CLIENT_PAUSE_DOWNLOAD_FLAG )
  {
    /* reset flag */
    client_info->flags &= ~OTA_CLIENT_PAUSE_DOWNLOAD_FLAG;
    /* Wait before requesting the next block. */
    return ZCL_STATUS_WAIT_FOR_DATA;
  }

  return status;
}


#ifdef USE_TAG_WRITE_CB
/**
* @brief  OTA client WriteTag callback
* @param  clusterPtr: ZCL Cluster pointer
* @param  header: ZCL OTA file format image header
* @param  tag_id: Tag identifier
* @param  tag_length : length of Tag
* @param  data_length  : length of Tag data
* @param  data  : Tag data
* @param  arg: Passed arg
* @retval ZCL status code
*/
static enum ZclStatusCodeT APP_ZIGBEE_OTA_Client_WriteTag_cb(struct ZbZclClusterT * clusterPtr, struct ZbZclOtaHeader * header,
                                                            uint16_t tag_id, uint32_t tag_length, uint8_t data_length,
                                                            uint8_t * data, void * arg)
{
   struct Zigbee_OTA_client_info* client_info = (struct Zigbee_OTA_client_info*) arg;
   enum ZclStatusCodeT status = ZCL_STATUS_SUCCESS;
   UNUSED(tag_length);

   switch (tag_id)
   {
       case ZCL_OTA_SUB_TAG_UPGRADE_IMAGE:
           /* APP_DBG("[OTA] Writing blocks. \n"); */
           status = APP_ZIGBEE_OTA_Client_WriteImage_cb(clusterPtr, header, data_length, data, arg);
           break;

       case ZCL_OTA_SUB_TAG_IMAGE_INTEGRITY_CODE:
           APP_DBG("[OTA] Get check crc. \n");
           client_info->ctx.binary_srv_crc = (uint32_t)data[0] | ( (uint16_t)data[1] << 8 );
           if ( data_length == 4u )
           {
             client_info->ctx.binary_srv_crc |= ( (uint32_t)data[2] << 16u ) | ( (uint32_t)data[3] << 24u );
           }
           break;

       default:
           status = ZCL_STATUS_INVALID_FIELD;
           break;
   }
   return status;
}
#endif

/**
 * @brief  OTA client Image Validate callback
 * @param  clusterPtr: ZCL Cluster pointer
 * @param  header: ZCL OTA file format image header
 * @param  arg: Passed arg
 * @retval ZCL status code
 */
static enum ZclStatusCodeT APP_ZIGBEE_OTA_Client_ImageValidate_cb(struct ZbZclClusterT *clusterPtr, struct ZbZclOtaHeader *header, void *arg){
  struct Zigbee_OTA_client_info* client_info = (struct Zigbee_OTA_client_info*) arg;
  enum ZclStatusCodeT status = ZCL_STATUS_SUCCESS;
  uint64_t last_double_word = 0;
  double l_transfer_throughput = 0;
  uint32_t lTransfertThroughputInt, lTransfertThroughputDec;

  /* Download finished => stop Toggling of the LED */
  HW_TS_Stop(TS_ID_LED);
  BSP_LED_Off(LED_GREEN);
  APP_DBG("LED_GREEN OFF");
  client_info->OTA_state=VERIFYING_IMAGE;
  /* Write the last RAM buffer to Flash */
  if(client_info->write_info.firmware_buffer_current_offset != 0){
    /* Write to Flash Memory */
    APP_ZIGBEE_OTA_Client_WriteFirmwareData(client_info);
    client_info->write_info.firmware_buffer_current_offset = 0;
  }

  APP_DBG("**************************************************************\n");
  APP_DBG("[OTA] Validating the image.");
  
  /* Finding the magic number */

  /* Last double word in Flash
   * => the magic if the firmware is valid
   */
  client_info->write_info.flash_current_offset -= 8;
  memcpy(&last_double_word, (void const*)(client_info->ctx.base_address + client_info->write_info.flash_current_offset), 8);
  if(((last_double_word & 0x00000000FFFFFFFF) != client_info->ctx.magic_keyword)
     && (((last_double_word & 0xFFFFFFFF00000000) >> 32) != client_info->ctx.magic_keyword)){
    APP_DBG("[OTA] Wrong magic keyword: invalid firmware.\n");
    status = ZCL_STATUS_INVALID_IMAGE;
    return status;
  }

  APP_DBG("[OTA] The downloaded firmware is valid.\n");
  client_info->download_time = (HAL_GetTick()- client_info->download_time)/1000;
  l_transfer_throughput = (((double)client_info->requested_image_size/client_info->download_time) / 1000) * 8;
  lTransfertThroughputInt = (uint32_t)l_transfer_throughput;
  lTransfertThroughputDec = (uint32_t)( ( l_transfer_throughput - lTransfertThroughputInt ) * 100 );

  APP_DBG("**************************************************************");
  APP_DBG(" FUOTA : END OF TRANSFER COMPLETED");

  if(client_info->image_type == IMAGE_TYPE_FW_COPRO_WIRELESS){
    APP_DBG("  - M0 wireless coprocessor firmware.");
  } else {
    APP_DBG("  - M4 application firmware.");
  }

  APP_DBG("  - %d bytes downloaded in %d seconds.",  client_info->requested_image_size, client_info->download_time);
  APP_DBG("  - Average throughput = %d.%d kbit/s.", lTransfertThroughputInt, lTransfertThroughputDec );
  APP_DBG("**************************************************************");

  BSP_LED_On(LED_GREEN);
  APP_DBG("LED_GREEN ON");

  return status;
}

/**
 * @brief  OTA client Reboot callback
 * @param  clusterPtr: ZCL Cluster pointer
 * @param  arg: Passed arg
 * @retval None
 */
static void APP_ZIGBEE_OTA_Client_Reboot_cb(struct ZbZclClusterT *clusterPtr, void *arg){
  UNUSED(arg);

  APP_DBG("**************************************************************");
  APP_DBG("[OTA] Rebooting.");

  APP_DBG("LED_BLUE OFF");
  BSP_LED_Off(LED_BLUE);
  APP_DBG("LED_GREEN OFF");
  BSP_LED_Off(LED_GREEN);
  APP_DBG("**************************************************************\n");
  
  HAL_Delay(100);
  UTIL_SEQ_SetTask(1U << (uint32_t)CFG_TASK_FUOTA_RESET, CFG_SCH_PRIO_0);
}

/**
 * @brief  OTA client Abort callback
 * @param  clusterPtr: ZCL Cluster pointer
 * @param  commandId: ZCL OTA command ID associated to the block transfer abortion
 * @param  arg: Passed arg
 * @retval ZCL status code
 */
static enum ZclStatusCodeT APP_ZIGBEE_OTA_Client_AbortDownload_cb(struct ZbZclClusterT *clusterPtr, enum ZbZclOtaCommandId commandId, void *arg){
 
  struct Zigbee_OTA_client_info* client_info = (struct Zigbee_OTA_client_info*) arg;
  APP_DBG("[OTA] Server aborted download.");
  HW_TS_Stop(TS_ID_LED);
  BSP_LED_Off(LED_GREEN);
  BSP_LED_On(LED_RED);

//  if(commandId == ZCL_OTA_COMMAND_IMAGE_BLOCK_RESPONSE)
//  {
  #ifdef OTA_ABORT_RETRY_ENABLE
    client_info->OTA_abort_retries++;
    if(client_info->OTA_abort_retries < OTA_CLIENT_ABORT_MAX_RETRIES )
    {
      APP_DBG("[OTA] retrying resume download in 500ms");
      HW_TS_Start(TS_DOWNLOAD_RESUME, (uint32_t)OTA_DOWNLOAD_RETRY_DELAY);
    }
    else
    {
      
      APP_DBG("[OTA] FATAL ERROR :resuming download failled after %d attempts",client_info->OTA_abort_retries);
      client_info->OTA_abort_retries=0;
      return ZCL_STATUS_SUCCESS;
    }
  #endif
 //}

  return ZCL_STATUS_ABORT;
}

/**
 * @brief  OTA client request upgrade
 * @param  None
 * @retval None
 */
static inline void APP_ZIGBEE_OTA_Client_Request_Upgrade(void){
  enum ZclStatusCodeT status;
  struct ZbZclOtaImageDefinition image_definition;

  APP_DBG("[OTA] Requesting an update.");
  APP_DBG("[OTA] Image type: 0x%04x.", OTA_client_info.image_type);
  APP_DBG("[OTA] Current file version: 0x%08x.\n", OTA_client_info.current_file_version);

  /* Image definition configuration */
  memset(&image_definition, 0, sizeof(image_definition));
  image_definition.manufacturer_code = ST_ZIGBEE_MANUFACTURER_CODE;
  image_definition.image_type = OTA_client_info.image_type;
  image_definition.file_version = OTA_client_info.current_file_version;

  OTA_client_info.OTA_state = QUERYING_NEXT_IMAGE;
  /* Sending Discovery request to server */
  APP_DBG("[OTA] Sending Query Next Image request.");
  /* HW version is provided as additional info in Query Next Image request */
  status = ZbZclOtaClientQueryNextImageReq(zigbee_app_info.ota_client, &image_definition,
                                           ZCL_OTA_QUERY_FIELD_CONTROL_HW_VERSION, CURRENT_HARDWARE_VERSION);
  if(status != ZCL_STATUS_SUCCESS){
    APP_DBG("ZbZclOtaClientDiscover failed.\n");
  }
}

/**
 * @brief  OTA client start download
 * @param  None
 * @retval None
 */
static inline void APP_ZIGBEE_OTA_Client_StartDownload(void){
  HW_TS_Start(TS_ID_LED, (uint32_t)LED_TOGGLE_TIMING);
  OTA_client_info.download_time = HAL_GetTick();
  ZbZclOtaClientImageTransferStart(zigbee_app_info.ota_client);
}

/**
 * @brief  OTA client pause download
 * @param  None
 * @retval None
 */
static inline void APP_ZIGBEE_OTA_Client_PauseDownload(void){
  /* Request download pause */
  OTA_client_info.flags |= OTA_CLIENT_PAUSE_DOWNLOAD_FLAG ;
}


/**
 * @brief  OTA client resume download
 * @param  None
 * @retval None
 */
static inline void APP_ZIGBEE_OTA_Client_ResumeDownload(void){
  /* Resume download */
  ZbZclOtaClientImageTransferResume(zigbee_app_info.ota_client);
}

/**
 * @brief  OTA client check for previous ota data vs server image_definition
 * @param  struct ZbZclOtaImageDefinition *image_definition : image type header received 
 * @retval bool : True : Resume download, False : erase sectors and start a new download
 */
static bool APP_ZIGBEE_OTA_Client_CheckPriviousDownload(struct ZbZclOtaImageDefinition *image_definition){
  /* Check previous downloaded image type & version */
  bool status = true;
  if(image_definition->file_version != OTA_client_info.zigbee_ota_ctx_nvm.file_version)
  {
    status = false;
  }

  if(image_definition->image_type != OTA_client_info.zigbee_ota_ctx_nvm.previous_image_type)
  {
    status = false;
  }

  return status;
}


/**
 * @brief  OTA Server find image type helper
 * @param  fileType: requested file type
 * @retval index value
 */
static inline int APP_ZIGBEE_FindImageType(unsigned int fileType)
{
    for(unsigned int i=0; i<(sizeof(OTA_currentFileVersionTab)/sizeof(*OTA_currentFileVersionTab)); i++)
    {
        if(OTA_currentFileVersionTab[i].fileType == fileType){ return i; }
    }
    return -1;
}

/**
 * @brief  OTA client writing firmware data from internal RAM cache to flash
 * @param  client_info: OTA client internal structure
 * @retval Application status code
 */
static inline APP_ZIGBEE_StatusTypeDef APP_ZIGBEE_OTA_Client_WriteFirmwareData(struct Zigbee_OTA_client_info* client_info){
  uint64_t l_read64 = 0;

  /* Write to Flash Memory */
  for(unsigned int flash_index = 0; flash_index < client_info->write_info.firmware_buffer_current_offset; flash_index+=8){
    while( LL_HSEM_1StepLock( HSEM, CFG_HW_FLASH_SEMID ) );
    HAL_FLASH_Unlock();
    while(LL_FLASH_IsActiveFlag_OperationSuspended());

    memcpy(&l_read64, &(client_info->write_info.firmware_buffer[flash_index]), 8);
    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD,
        client_info->ctx.base_address + client_info->write_info.flash_current_offset,
        l_read64) == HAL_OK)
    {
      /* Read back value for verification */
      l_read64 = 0;
      l_read64 = *(uint64_t*)(client_info->ctx.base_address + client_info->write_info.flash_current_offset);
      if(l_read64 != (*(uint64_t*)(client_info->write_info.firmware_buffer+flash_index)))
      {
        APP_DBG("FLASH: Comparison failed l_read64 = 0x%jx / ram_array = 0x%jx",
                l_read64, client_info->write_info.firmware_buffer[flash_index]);
        return APP_ZIGBEE_ERROR;
      }
    }
    else
    {
      APP_DBG("HAL_FLASH_Program FAILED at flash_index = %d ,  flash offset = 0x%04X", flash_index, client_info->write_info.flash_current_offset);
      return APP_ZIGBEE_ERROR;
    }


    client_info->write_info.flash_current_offset += sizeof(uint64_t);

 
  }

  /* Save client.info ctx to NVM */
  if(APP_ZIGBEE_OTA_ctx_save_nvm(client_info))
  {
    APP_DBG("[OTA] ctx save : flash offset =0x%04X pushed to NVM",client_info->write_info.flash_current_offset);
  }
   HAL_FLASH_Lock();
   LL_HSEM_ReleaseLock( HSEM, CFG_HW_FLASH_SEMID, 0 );




  return APP_ZIGBEE_OK;
}

/**
 * @brief  Getting flash first secure sector helper
 * @param  None
 * @retval First flash secure sector
 */
static inline uint32_t GetFirstSecureSector(void)
{
  uint32_t first_secure_sector_idx, sfsa_field, sbrv_field, sbrv_field_sector;

  /* Read SFSA */
  sfsa_field = (READ_BIT(FLASH->SFR, FLASH_SFR_SFSA) >> FLASH_SFR_SFSA_Pos);
  APP_DBG("SFSA OB = 0x%x", sfsa_field);
  APP_DBG("SFSA Option Bytes set to sector = 0x%x (0x080%x)", sfsa_field, sfsa_field*4096);

  /* Read SBRV */
  /* Contains the word aligned CPU2 boot reset start address offset within the selected memory area by C2OPT. */
  sbrv_field = (READ_BIT(FLASH->SRRVR, FLASH_SRRVR_SBRV) >> FLASH_SRRVR_SBRV_Pos);
  APP_DBG("SBRV OB = 0x%x", sbrv_field);
  /* Divide sbrv_field by 1024 to be compared to SFSA value */
  sbrv_field_sector = sbrv_field / 1024;
  APP_DBG("SBRV Option Bytes set to sector = 0x%x (0x080%x)", sbrv_field_sector, sbrv_field*4);

  /* If SBRV is below SFSA then set first_secure_sector_idx to SBRV */
  if (sbrv_field_sector < sfsa_field)
  {
    first_secure_sector_idx = sbrv_field_sector;
  }
  else
  {
    first_secure_sector_idx = sfsa_field;
  }

  APP_DBG("first_secure_sector_idx = 0x%x", first_secure_sector_idx);

  return first_secure_sector_idx;
}

/**
 * @brief  Deleting non secure sectors helper
 * @param  None
 * @retval None
 */
static inline void Delete_Sectors( void )
{
  /**
   * The number of sectors to erase is read from SRAM1.
   * It shall be checked whether the number of sectors to erase does not overlap on the secured Flash
   * The limit can be read from the SFSA option byte which provides the first secured sector address.
   */

  uint32_t page_error;
  FLASH_EraseInitTypeDef p_erase_init;
  uint32_t first_secure_sector_idx;

  first_secure_sector_idx = GetFirstSecureSector();

  p_erase_init.TypeErase = FLASH_TYPEERASE_PAGES;
  p_erase_init.Page = *((uint8_t*) SRAM1_BASE + 1);
  if(p_erase_init.Page < (CFG_APP_START_SECTOR_INDEX - 1))
  {
    /**
     * Something has been wrong as there is no case we should delete the OTA application
     * Reboot on the firmware application
     */
    *(uint8_t*)SRAM1_BASE = CFG_REBOOT_ON_DOWNLOADED_FW;
    NVIC_SystemReset();
  }
  p_erase_init.NbPages = *((uint8_t*) SRAM1_BASE + 2);

  if ((p_erase_init.Page + p_erase_init.NbPages) > first_secure_sector_idx)
  {
    p_erase_init.NbPages = first_secure_sector_idx - p_erase_init.Page;
  }

  APP_DBG("Erase FLASH Memory from sector %d (0x080%x) to sector %d (0x080%x)", p_erase_init.Page, p_erase_init.Page*4096, p_erase_init.NbPages+p_erase_init.Page, (p_erase_init.NbPages+p_erase_init.Page)*4096);

  HAL_FLASH_Unlock();

  HAL_FLASHEx_Erase(&p_erase_init, &page_error);

  HAL_FLASH_Lock();

  return;
}

/**
 * @brief  Getting available internal flash space size
 * @param  None
 * @retval Application status code
 */
static inline APP_ZIGBEE_StatusTypeDef APP_ZIGBEE_CheckDeviceCapabilities(void)
{
  APP_ZIGBEE_StatusTypeDef status = APP_ZIGBEE_OK;

  /* Get Flash memory size available to copy binary from Server */
  uint32_t first_sector_idx;
  uint32_t first_secure_sector_idx;
  uint32_t free_sectors;
  uint32_t free_size;

  APP_DBG("Check Device capabilities");

  first_secure_sector_idx = GetFirstSecureSector();

  first_sector_idx = *((uint8_t*) SRAM1_BASE + 1);
  if (first_sector_idx == 0)
  {
    APP_DBG("ERROR : SRAM1_BASE + 1 == 0");
    first_sector_idx = CFG_APP_START_SECTOR_INDEX;
  }
  APP_DBG("First available sector = %d (0x080%x)", first_sector_idx, first_sector_idx*4096);

  free_sectors = first_secure_sector_idx - first_sector_idx;
  free_size = free_sectors*4096;

  APP_DBG("free_sectors = %d , -> %d bytes of FLASH Free", free_sectors, free_size);

  APP_DBG("Server requests    : %d bytes", OTA_client_info.requested_image_size);
  APP_DBG("Client Free memory : %d bytes", free_size);

  if (free_size < OTA_client_info.requested_image_size)
  {
    status = APP_ZIGBEE_ERROR;
    APP_DBG("WARNING: Not enough Free Flash Memory available to download binary from Server!");
  }
  else
  {
    APP_DBG("Device contains enough Flash Memory to download binary");
  }

  return status;
}

/**
 * @brief Task responsible for the reset at the end of OTA transfer.
 * @param  None
 * @retval None
 */
static void APP_ZIGBEE_PerformReset(void)
{
  OTA_client_info.OTA_state = REBOOTING;
  APP_DBG("*******************************************************");
  APP_DBG(" FUOTA_CLIENT : END OF TRANSFER COMPLETED");

  if (OTA_client_info.image_type == fileType_APP)
  {
    APP_DBG("  --> Request to reboot on FW Application");
    APP_DBG("*******************************************************");

    /* Reboot on Downloaded FW Application */
    *(uint8_t*)SRAM1_BASE = CFG_REBOOT_ON_DOWNLOADED_FW;

    HAL_Delay(100);
    NVIC_SystemReset();
  }
  else
  {
    if (OTA_client_info.image_type == fileType_COPRO_WIRELESS)
    {
      APP_DBG("  --> Request to reboot on FUS");
      APP_DBG("*******************************************************");
      HAL_Delay(100);

      /**
       * Wireless firmware update is requested
       * Request CPU2 to reboot on FUS by sending two FUS command
       */
      SHCI_C2_FUS_GetState( NULL );
      SHCI_C2_FUS_GetState( NULL );
      while(1)
      {
        HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON, PWR_SLEEPENTRY_WFI);
      }
    }
    else
    {
      APP_DBG("APP_ZIGBEE_PerformReset: OtaContext.file_type not recognized");
      return;
    }
  }
}

/**
 * @brief  Application timer server callback (led toggling)
 * @param  None
 * @retval None
 */
static void APP_ZIGBEE_LEDToggle(void)
{
  BSP_LED_Toggle(LED_GREEN);
}

/*************************************************************
 *
 * NVM FUNCTIONS
 *
 *************************************************************/
/**
 * @brief  notify to save persistent data callback
 * @param  zb: Zigbee device object pointer, cbarg: callback arg pointer
 * @retval None
 */
static void APP_ZIGBEE_persist_notify_cb(struct ZigBeeT *zb, void *cbarg)
{
#ifdef OTA_DISPLAY_TIMING
  static uint32_t lPersistentStartTime = 0;
  uint32_t  lTime1, lTime2;

  lTime1 = ( HAL_GetTick() - lPersistentStartTime );
  lPersistentStartTime = HAL_GetTick();
#endif // OTA_DISPLAY_TIMING

  /* Save the persistent data */
  APP_DBG("Notification to save persistent data requested from stack.");
  APP_ZIGBEE_persist_save();

#ifdef OTA_DISPLAY_TIMING
  lTime2 = ( HAL_GetTick() - lPersistentStartTime );
  lPersistentStartTime = HAL_GetTick();
  APP_DBG("Persistent Data: Interval Time : %d ms and Save Time : %d ms", lTime1, lTime2);
#endif // OTA_DISPLAY_TIMING
}

/**
 * @brief  Start Zigbee Network from persistent data
 * @param  zb: Zigbee device object pointer
 * @retval Zigbee stack Status code
 */
static enum ZbStatusCodeT APP_ZIGBEE_ZbStartupPersist(struct ZigBeeT* zb)
{
   bool read_status;
   enum ZbStatusCodeT status = ZB_STATUS_SUCCESS;

   /* Restore persistence */
   read_status = APP_ZIGBEE_persist_load();

   if (read_status)
   {
       /* Make sure the EPID is cleared, before we are allowed to restore persistence */
       uint64_t epid = 0U;
       ZbNwkSet(zb, ZB_NWK_NIB_ID_ExtendedPanId, &epid, sizeof(uint64_t));

       /* Start-up from persistence */
       APP_DBG("APP_ZIGBEE_ZbStartupPersist: restoring stack persistence");
       status = ZbStartupPersist(zb, &cache_persistent_data.U8_data[ST_PERSIST_FLASH_DATA_OFFSET], cache_persistent_data.U32_data[0],NULL, NULL, NULL);
       if (status == ZB_STATUS_SUCCESS)
       {
         bPersistDataUsed = TRUE;
       }
   }
   else
   {
       /* Failed to restart from persistence */
       APP_DBG("APP_ZIGBEE_ZbStartupPersist: no persistence data to restore");
       status = ZB_STATUS_ALLOC_FAIL;
   }

   /* Only for debug purpose, depending of persistent data, following traces
      could display bytes that are irrelevants to on off cluster */
   if (status == ZB_STATUS_SUCCESS)
   {
      /* read the last bytes of data where the ZCL on off persistent data shall be*/
      uint32_t len = cache_persistent_data.U32_data[0] + 4 ;
      APP_DBG("ClusterID %02x %02x",cache_persistent_data.U8_data[len-9],cache_persistent_data.U8_data[len-10]);
      APP_DBG("Endpoint %02x %02x",cache_persistent_data.U8_data[len-7],cache_persistent_data.U8_data[len-8]);
      APP_DBG("Direction %02x",cache_persistent_data.U8_data[len-6]);
      APP_DBG("AttrID %02x %02x",cache_persistent_data.U8_data[len-4],cache_persistent_data.U8_data[len-5]);
      APP_DBG("Len %02x %02x",cache_persistent_data.U8_data[len-2],cache_persistent_data.U8_data[len-3]);
      APP_DBG("Value %02x",cache_persistent_data.U8_data[len-1]);
   }

   return status;
}/* APP_ZIGBEE_ZbStartupPersist */

/**
 * @brief  timer callback to wait end of restore cluster persistence form M0
 * @param  None
 * @retval None
 */
static void APP_ZIGBEE_PersistCompleted_callback(void)
{
  /* No cluster attributes to restore */
}/* APP_ZIGBEE_PersistCompleted_callback */


/**
 * @brief  save OTA ctx persistent data
 * @param  None
 * @retval true if success, false if fail
 */
static bool APP_ZIGBEE_OTA_ctx_save_nvm (struct Zigbee_OTA_client_info* client_info) {

  int ee_status = 0;
  uint32_t*  p_data;
  /* Populate data struct */
  client_info->zigbee_ota_ctx_nvm.flash_offset = client_info->write_info.flash_current_offset;
  client_info->zigbee_ota_ctx_nvm.previous_image_type = client_info->ctx.file_type;
  client_info->zigbee_ota_ctx_nvm.file_version = client_info->ctx.file_version;
  client_info->zigbee_ota_ctx_nvm.OtaCurrentState= client_info->OTA_state;

  p_data = (uint32_t *)&client_info->zigbee_ota_ctx_nvm.flash_offset;
 /* loop i = number of uint32_t in zigbee_ota_ctx_nvm*/ 
  for(uint8_t i =0; i< (sizeof(client_info->zigbee_ota_ctx_nvm)/sizeof(uint32_t));i++)
  {
    ee_status = EE_Write(0, USER_DB_START_ADDR+i, *(p_data+i));
    if (ee_status != EE_OK)
    {
      if (ee_status == EE_CLEAN_NEEDED) /* Shall not be there if CFG_EE_AUTO_CLEAN = 1*/
      {
        APP_DBG("CLEAN NEEDED, CLEANING");
        EE_Clean(0,0);
      }
      else
      {
        /* Failed to write , an Erase shall be done */
        APP_DBG("APP_ZIGBEE_NVM_Write failed @ %d status %d", i, ee_status);
        return false;
      }
    }
  }


  return true;

}

/**
 * @brief  load OTA ctx persistent data
 * @param  None
 * @retval true if success, false if fail
 */
static bool APP_ZIGBEE_OTA_ctx_load_nvm () {
  bool status = true;
  int ee_status = 0;
  uint32_t*  p_data;

  HAL_FLASH_Unlock();
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_PGSERR | FLASH_FLAG_WRPERR | FLASH_FLAG_OPTVERR);

  /* Searching for previous OTA CTX */
  p_data = (uint32_t *)&OTA_client_info.zigbee_ota_ctx_nvm.flash_offset;
  for(uint8_t i =0; i< (sizeof(OTA_client_info.zigbee_ota_ctx_nvm)/sizeof(uint32_t));i++)
  {
    ee_status = EE_Read(0, USER_DB_START_ADDR+i, p_data+i);
    if (ee_status != EE_OK )
    {
      APP_DBG("[OTA] ctx_load : Can't find previous OTA ctx file version in NVM !");
      status = false;
      return status;
    }
  }
  APP_DBG("[OTA] ctx_load : successfully loaded previous OTA ctx from NVM");
  OTA_client_info.flags |= OTA_CLIENT_CTX_FOUND_FLAG;
  OTA_client_info.write_info.flash_current_offset = OTA_client_info.zigbee_ota_ctx_nvm.flash_offset;
  HAL_FLASH_Lock();
  return status;

}

/**
 * @brief  Load persistent data
 * @param  None
 * @retval true if success, false if fail
 */
static bool APP_ZIGBEE_persist_load(void)
{
#ifdef CFG_NVM
    APP_DBG("Retrieving persistent data from FLASH");
    if ( APP_ZIGBEE_NVM_Read() == false )
    {
      return false;
    }
#else /* CFG_NVM */
    /* Check length range */
    if ((cache_persistent_data.U32_data[0] == 0) ||
        (cache_persistent_data.U32_data[0] > ST_PERSIST_MAX_ALLOC_SZ))
    {
        APP_DBG("No data or too large length : %d",cache_persistent_data.U32_data[0]);
        return false;
    }
#endif /* CFG_NVM */

    /* Verify Tag */
    if ( cache_persistent_data.U32_data[1] != ST_PERSIST_TAG )
    {
      APP_DBG("Bad Verification TAG in RAM : 0x%04X",cache_persistent_data.U32_data[1]);
      return false;
    }

    return true;
} /* APP_ZIGBEE_persist_load */


/**
 * @brief  Save persistent data
 * @param  None
 * @retval true if success , false if fail
 */
static bool APP_ZIGBEE_persist_save(void)
{
    uint32_t len;
#ifdef OTA_DISPLAY_TIMING
    static union cache  szPersistentDataTemp = { 0, };
#endif // OTA_DISPLAY_TIMING

    /* Clear the RAM cache before saving */
    memset(cache_persistent_data.U8_data, 0x00, ST_PERSIST_MAX_ALLOC_BUFFER_SZ);

    /* Call the satck API t get current persistent data */
    len = ZbPersistGet(zigbee_app_info.zb, 0, 0);

    /* Check Length range */
    if (len == 0U)
    {
        /* If the persistence length was zero then no data available. */
        APP_DBG("APP_ZIGBEE_persist_save: no persistence data to save !");
        return false;
    }

    if (len > ST_PERSIST_MAX_ALLOC_SZ)
    {
        /* if persistence length to big to store */
        APP_DBG("APP_ZIGBEE_persist_save: persist size too large for storage (%d)", len);
        return false;
    }

    /* Store in cache the persistent data */
    len = ZbPersistGet(zigbee_app_info.zb, &cache_persistent_data.U8_data[ST_PERSIST_FLASH_DATA_OFFSET], len);

    /* Store in cache the persistent data length */
    cache_persistent_data.U32_data[0] = len;

    /* Store in cache the persistent verification Tag */
    cache_persistent_data.U32_data[1] = ST_PERSIST_TAG;

#ifdef OTA_DISPLAY_TIMING
    // -- Compare with previous --
    if ( memcmp( szPersistentDataTemp.U8_data, cache_persistent_data.U8_data, ( len + 8 ) ) == 0 )
      { APP_DBG("APP_ZIGBEE_persist_save: This Persistence is same as previous"); }
    memcpy( szPersistentDataTemp.U8_data, cache_persistent_data.U8_data, (len + 8 ) );
#endif // OTA_DISPLAY_TIMING

    zigbee_app_info.persistNumWrites++;
    APP_DBG("APP_ZIGBEE_persist_save: Persistence written in cache RAM (num writes = %d) len=%d",
             zigbee_app_info.persistNumWrites, cache_persistent_data.U32_data[0]);

#ifdef CFG_NVM
    if(!APP_ZIGBEE_NVM_Write())
    {
      return false;
    }
    APP_DBG("APP_ZIGBEE_persist_save: Persistent data FLASHED");
#endif /* CFG_NVM */

    return true;
} /* APP_ZIGBEE_persist_save */


/**
 * @brief  Delete persistent data
 * @param  None
 * @retval None
 */
static void APP_ZIGBEE_persist_delete(void)
{
  /* Clear RAM cache */
  memset(cache_persistent_data.U8_data, 0x00, ST_PERSIST_MAX_ALLOC_BUFFER_SZ);
  APP_DBG("Persistent Data RAM cache cleared");
#ifdef CFG_NVM
  APP_DBG("FLASH ERASED");
  APP_ZIGBEE_NVM_Erase();
#endif /* CFG_NVM */

  // -- Reset Chip --
  *(uint8_t*)SRAM1_BASE = CFG_REBOOT_ON_OTA_FW;
  NVIC_SystemReset();
} /* APP_ZIGBEE_persist_delete */


#ifdef CFG_NVM

/**
 * @brief  Init the NVM
 * @param  None
 * @retval None
 */
static void APP_ZIGBEE_NVM_Init(void)
{
  int eeprom_init_status;

  APP_DBG("Flash starting address = %x",HW_FLASH_ADDRESS  + CFG_NVM_BASE_ADDRESS);
  eeprom_init_status = EE_Init( 0 , HW_FLASH_ADDRESS + CFG_NVM_BASE_ADDRESS );

  if (eeprom_init_status != EE_OK)
  {
    /* format NVM since init failed */
    eeprom_init_status= EE_Init( 1, HW_FLASH_ADDRESS + CFG_NVM_BASE_ADDRESS );
  }
  APP_DBG("EE_init status = %d",eeprom_init_status);

} /* APP_ZIGBEE_NVM_Init */

/**
*@brief  Read the persistent data from NVM
* @param  None
* @retval true if success , false if failed
*/
static bool APP_ZIGBEE_NVM_Read(void)
{
  uint16_t  num_words;
  uint16_t  num_bytes;
  uint16_t  iIndex;
  bool      status = true;
  int       ee_status = 0;

  HAL_FLASH_Unlock();
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_PGSERR | FLASH_FLAG_WRPERR | FLASH_FLAG_OPTVERR);

  /* Read the data length from cache */
  ee_status = EE_Read(0, ZIGBEE_DB_START_ADDR, &cache_persistent_data.U32_data[0]);
  if (ee_status != EE_OK)
  {
    APP_DBG("Read -> persistent data length not found ERASE to be done - Read Stopped");
    status = false;
  }
  else
  {
    /* Check length is not too big nor zero */
    num_bytes = cache_persistent_data.U32_data[0];
    if ( ( num_bytes == 0 ) || ( num_bytes > ST_PERSIST_MAX_ALLOC_SZ ) )
    {
      APP_DBG("No data or too large length : %d", num_bytes);
      status = false;
    }
    else
    {
      /* Length is within range. Adjust the length to be U32 aligned */
      num_words = (uint16_t) ( num_bytes / 4 ) ;
      if ( cache_persistent_data.U32_data[0] % 4 != 0 )
      {
        num_words++;
      }
      // -- Add the Verification Tag  --
      num_words++;

      /* copy the read data from Flash to cache including length */
      for ( iIndex = 1; iIndex <= num_words; iIndex++)
      {
        /* read data from first data in U32 unit */
        ee_status = EE_Read(0, iIndex + ZIGBEE_DB_START_ADDR, &cache_persistent_data.U32_data[iIndex] );
        if (ee_status != EE_OK)
        {
          APP_DBG("Read not found leaving");
          status = false;
          break;
        }
      }
    }
  }

  HAL_FLASH_Lock();
  if(status)
  {
    APP_DBG("READ PERSISTENT DATA LEN = %d", num_bytes);
  }

  return status;
} /* APP_ZIGBEE_NVM_Read */


/**
 * @brief  Write the persistent data in NVM
 * @param  None
 * @retval None
 */
static bool APP_ZIGBEE_NVM_Write(void)
{
  int ee_status = 0;
  uint16_t num_words;
  uint16_t iIndex;

  // -- Offset in Word for Length + Tag */
  num_words = ( ST_PERSIST_FLASH_DATA_OFFSET / 4u );
  num_words += (uint16_t) ( cache_persistent_data.U32_data[0] / 4u );

  /* Adjust the length to be U32 aligned */
  if ( cache_persistent_data.U32_data[0] % 4 != 0 )
  {
    num_words++;
  }

  // -- Save data in flash --
  for ( iIndex = 0; iIndex < num_words; iIndex++ )
  {
    ee_status = EE_Write(0, (uint16_t)iIndex + ZIGBEE_DB_START_ADDR, cache_persistent_data.U32_data[iIndex]);
    if (ee_status != EE_OK)
    {
      if (ee_status == EE_CLEAN_NEEDED) /* Shall not be there if CFG_EE_AUTO_CLEAN = 1*/
      {
        APP_DBG("CLEAN NEEDED, CLEANING");
        EE_Clean(0,0);
      }
      else
      {
        /* Failed to write , an Erase shall be done */
        APP_DBG("APP_ZIGBEE_NVM_Write failed @ %d status %d", iIndex, ee_status);
        break;
      }
    }
  }

  if(ee_status != EE_OK)
  {
     APP_DBG("WRITE STOPPED, need a FLASH ERASE");
     return false;
  }

  APP_DBG("WRITTEN PERSISTENT DATA LEN = %d", cache_persistent_data.U32_data[0]);
  return true;
} /* APP_ZIGBEE_NVM_Write */


/**
 * @brief  Erase the NVM
 * @param  None
 * @retval None
 */
static void APP_ZIGBEE_NVM_Erase(void)
{
   EE_Init(1, HW_FLASH_ADDRESS + CFG_NVM_BASE_ADDRESS + ZIGBEE_DB_START_ADDR); /* Erase Flash except user data */
} /* APP_ZIGBEE_NVM_Erase */

#endif /* CFG_NVM */


/**
 * @brief  Zigbee application initialization
 * @param  None
 * @retval None
 */
static void APP_ZIGBEE_App_Init(void){
  /* Tasks associated to OTA upgrade process */
  UTIL_SEQ_RegTask(1U << (uint32_t)CFG_TASK_ZIGBEE_OTA_REQUEST_UPGRADE, UTIL_SEQ_RFU, APP_ZIGBEE_OTA_Client_Request_Upgrade);
  UTIL_SEQ_RegTask(1U << (uint32_t)CFG_TASK_ZIGBEE_OTA_START_DOWNLOAD, UTIL_SEQ_RFU, APP_ZIGBEE_OTA_Client_StartDownload);
  UTIL_SEQ_RegTask(1U << (uint32_t)CFG_TASK_ZIGBEE_OTA_RESUME_DOWNLOAD, UTIL_SEQ_RFU, APP_ZIGBEE_OTA_Client_ResumeDownload);
  UTIL_SEQ_RegTask(1U << (uint32_t)CFG_TASK_ZIGBEE_OTA_SERVER_DISCOVERY, UTIL_SEQ_RFU, APP_ZIGBEE_OTA_Client_ServerDiscovery);
  UTIL_SEQ_RegTask(1U << (uint32_t)CFG_TASK_FUOTA_RESET, UTIL_SEQ_RFU, APP_ZIGBEE_PerformReset);

  /* Timer associated to GREEN LED toggling */
  HW_TS_Create(CFG_TIM_PROC_ID_ISR, &TS_ID_LED, hw_ts_Repeated, APP_ZIGBEE_LEDToggle);

  /* Timer associated to OTA download resume */
  HW_TS_Create(CFG_TIM_PROC_ID_ISR, &TS_DOWNLOAD_RESUME, hw_ts_SingleShot, APP_ZIGBEE_OTA_Client_ResumeDownload);

  /* Initialize Zigbee OTA Client parameters */
  APP_ZIGBEE_OTA_Client_Init();
} /* APP_ZIGBEE_App_Init */


static void APP_ZIGBEE_OTA_Client_ServerDiscovery( void )
{
  enum ZclStatusCodeT   status;
  struct ZbApsAddrT     dst;

  /* Destination address configuration */
  memset(&dst, 0, sizeof(dst));
  dst.mode = ZB_APSDE_ADDRMODE_SHORT;
  dst.endpoint = SW1_ENDPOINT;
  dst.nwkAddr = 0x0;
  OTA_client_info.OTA_state = DISCOVERING_OTA_SERVER;
  status = ZbZclOtaClientDiscover(zigbee_app_info.ota_client, &dst);
  if(status != ZCL_STATUS_SUCCESS)
  {
    APP_DBG("ZbZclOtaClientDiscover failed.\n");
  }
}


/**
 * @brief  OTA client initialization
 * @param  None
 * @retval None
 */
static void APP_ZIGBEE_OTA_Client_Init(void)
{
  uint16_t  iShortAddress;

  /* Client info fields set to 0 */
  memset(&OTA_client_info, 0, sizeof(OTA_client_info));

  APP_DBG("Searching for OTA server.");
  BSP_LED_On(LED_GREEN);

  /* Requesting a discovery for any available OTA server */
  UTIL_SEQ_SetTask( 1U << CFG_TASK_ZIGBEE_OTA_SERVER_DISCOVERY, CFG_SCH_PRIO_0 );

  /* Wait a Discovery */
  UTIL_SEQ_WaitEvt(EVENT_ZIGBEE_OTA_SERVER_FOUND);
  BSP_LED_Off(LED_GREEN);

  /**
   * This is a safe clear in case the engi bytes are not all written
   * The error flag should be cleared before moving forward
   */
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_OPTVERR);



  BSP_LED_On(LED_BLUE);
  if ( bPersistDataUsed != FALSE )
  {
    BSP_LED_On(LED_GREEN);
  }
  if(APP_ZIGBEE_OTA_ctx_load_nvm())
  {
    APP_DBG("[OTA] ctx_load : OTA NVM flash offset restored succesfuly \n");
  }
  else{
    APP_DBG("Delete_Sectors");
    Delete_Sectors();
  }
  iShortAddress = ZbShortAddress( zigbee_app_info.zb );
  APP_DBG("OTA Client with Short Address 0x%04X.", iShortAddress );
  APP_DBG("OTA Client init done!\n");

} /* APP_ZIGBEE_OTA_Client_Init */

/**
 * @brief  Zigbee application initialization
 * @param  None
 * @retval None
 */
void APP_ZIGBEE_Init(void)
{
  SHCI_CmdStatus_t ZigbeeInitStatus;

  APP_DBG("APP_ZIGBEE_Init");

  /* Check the compatibility with the Coprocessor Wireless Firmware loaded */
  APP_ZIGBEE_CheckWirelessFirmwareInfo();

  /* Register cmdbuffer */
  APP_ZIGBEE_RegisterCmdBuffer(&ZigbeeOtCmdBuffer);

  /* Init config buffer and call TL_ZIGBEE_Init */
  APP_ZIGBEE_TL_INIT();

  /* Register task */
  /* Create the different tasks */
  UTIL_SEQ_RegTask(1U << (uint32_t)CFG_TASK_NOTIFY_FROM_M0_TO_M4, UTIL_SEQ_RFU, APP_ZIGBEE_ProcessNotifyM0ToM4);
  UTIL_SEQ_RegTask(1U << (uint32_t)CFG_TASK_REQUEST_FROM_M0_TO_M4, UTIL_SEQ_RFU, APP_ZIGBEE_ProcessRequestM0ToM4);

  /* Task associated with network creation process */
  UTIL_SEQ_RegTask(1U << CFG_TASK_ZIGBEE_NETWORK_FORM, UTIL_SEQ_RFU, APP_ZIGBEE_NwkForm);

  /* Task associated with push button SW1 */
  UTIL_SEQ_RegTask(1U << CFG_TASK_BUTTON_SW1, UTIL_SEQ_RFU, APP_ZIGBEE_SW1_Process); /* request download pause */

  /* Task associated with push button SW2 */
  UTIL_SEQ_RegTask(1U << CFG_TASK_BUTTON_SW2, UTIL_SEQ_RFU, APP_ZIGBEE_SW2_Process); /* clear persistence */

  /* Task associated with push button SW3 */
  UTIL_SEQ_RegTask(1U << CFG_TASK_BUTTON_SW3, UTIL_SEQ_RFU, APP_ZIGBEE_SW3_Process); /* request download resume */

  /* Task associated with application init */
  UTIL_SEQ_RegTask(1U << CFG_TASK_ZIGBEE_APP_START, UTIL_SEQ_RFU, APP_ZIGBEE_App_Init);

  /* Start the Zigbee on the CPU2 side */
  ZigbeeInitStatus = SHCI_C2_ZIGBEE_Init();
  /* Prevent unused argument(s) compilation warning */
  UNUSED(ZigbeeInitStatus);

    /* NVM Init */
#if CFG_NVM
  APP_ZIGBEE_NVM_Init();
#endif

  /* Initialize Zigbee stack layers */
  APP_ZIGBEE_StackLayersInit();
} /* APP_ZIGBEE_Init */

/**
 * @brief  Initialize Zigbee stack layers
 * @param  None
 * @retval None
 */
static void APP_ZIGBEE_StackLayersInit(void)
{
  enum ZbStatusCodeT status;
  APP_DBG("APP_ZIGBEE_StackLayersInit");

  zigbee_app_info.zb = ZbInit(0U, NULL, NULL);
  assert(zigbee_app_info.zb != NULL);

  /* Create the endpoint and cluster(s) */
  APP_ZIGBEE_ConfigEndpoints();

  BSP_LED_Off(LED_RED);
  BSP_LED_Off(LED_GREEN);
  BSP_LED_Off(LED_BLUE);

  /* Configure the joining parameters */
  zigbee_app_info.join_status = (enum ZbStatusCodeT) 0x01; /* init to error status */
  zigbee_app_info.join_delay = HAL_GetTick(); /* now */

  /* STEP 1 - TRY to START FROM PERSISTENCE */

/* define if we need to do a fresh start */
  zigbee_app_info.fresh_startup = true;

  /* First we disable the persistent notification */
  ZbPersistNotifyRegister(zigbee_app_info.zb,NULL,NULL);

  /* Call a startup from persistence */
  status = APP_ZIGBEE_ZbStartupPersist(zigbee_app_info.zb);
  if(status == ZB_STATUS_SUCCESS)
  {
     /* no fresh stratup need anymore */
     zigbee_app_info.fresh_startup = false;
     APP_DBG("ZbStartupPersist: SUCCESS, restarted from persistence");
     BSP_LED_On(LED_GREEN);

     /* STEP 2 - Get the ZCL on off attribute restored from persistence */
     /* start a timer to wait M0 to complete Cluster persitence*/
     HW_TS_Create(CFG_TIM_WAIT_BEOFRE_READ_ATTR, &TS_ID1, hw_ts_SingleShot, APP_ZIGBEE_PersistCompleted_callback);
     HW_TS_Start(TS_ID1, 1000);

     /* STEP3 - Activate back the persistent notification */
     /* Register Persistent data change notification */
     ZbPersistNotifyRegister(zigbee_app_info.zb,APP_ZIGBEE_persist_notify_cb,NULL);

     /* Call the callback once here to save persistence data */
     APP_ZIGBEE_persist_notify_cb(zigbee_app_info.zb,NULL);

     /* Call the ZIGBEE app init */
     UTIL_SEQ_SetTask(1U << CFG_TASK_ZIGBEE_APP_START, CFG_SCH_PRIO_0);
  }
  else
  {
       /* Start-up form persistence failed perform a fresh ZbStartup */
       APP_DBG("ZbStartupPersist: FAILED to restart from persistence with status: 0x%02x",status);
  }

  if(zigbee_app_info.fresh_startup)
  {
    /* Go for fresh start */
    UTIL_SEQ_SetTask(1U << CFG_TASK_ZIGBEE_NETWORK_FORM, CFG_SCH_PRIO_0);
  }
} /* APP_ZIGBEE_StackLayersInit */

/**
 * @brief  Configure Zigbee application endpoints
 * @param  None
 * @retval None
 */
static void APP_ZIGBEE_ConfigEndpoints(void)
{
  struct ZbApsmeAddEndpointReqT req;
  struct ZbApsmeAddEndpointConfT conf;

  memset(&req, 0, sizeof(req));
  req.profileId = ZCL_PROFILE_HOME_AUTOMATION;
  req.deviceId = ZCL_DEVICE_DATA_COLLECTION_UNIT;

  /* Endpoint: SW1_ENDPOINT */
  req.endpoint = SW1_ENDPOINT;
  ZbZclAddEndpoint(zigbee_app_info.zb, &req, &conf);
  assert(conf.status == ZB_STATUS_SUCCESS);

  /* OTA client callbacks*/
  ZbZclOtaClientGetDefaultCallbacks(&client_config.callbacks);
  client_config.callbacks.discover_complete = APP_ZIGBEE_OTA_Client_DiscoverComplete_cb;
  client_config.callbacks.image_notify = APP_ZIGBEE_OTA_Client_ImageNotify_cb;
  client_config.callbacks.query_next = APP_ZIGBEE_OTA_Client_QueryNextImage_cb;
//  .callbacks.update_raw = APP_ZIGBEE_OTA_Client_UpdateRaw_cb;
#ifdef USE_TAG_WRITE_CB
  client_config.callbacks.write_tag = APP_ZIGBEE_OTA_Client_WriteTag_cb;
#endif
  client_config.callbacks.write_image = APP_ZIGBEE_OTA_Client_WriteImage_cb;
  client_config.callbacks.image_validate = APP_ZIGBEE_OTA_Client_ImageValidate_cb;
//  client_config.callbacks.upgrade_end = APP_ZIGBEE_OTA_Client_UpgradeEnd_cb;
  client_config.callbacks.reboot = APP_ZIGBEE_OTA_Client_Reboot_cb;
  client_config.callbacks.abort_download = APP_ZIGBEE_OTA_Client_AbortDownload_cb;

  /* OTA Client */
  zigbee_app_info.ota_client = ZbZclOtaClientAlloc(zigbee_app_info.zb, &client_config, &OTA_client_info);
  assert(zigbee_app_info.ota_client != NULL);
  ZbZclClusterEndpointRegister(zigbee_app_info.ota_client);

} /* APP_ZIGBEE_ConfigEndpoints */

/**
 * @brief  Handle Zigbee network forming and joining
 * @param  None
 * @retval None
 */
static void APP_ZIGBEE_NwkForm(void)
{
  if ((zigbee_app_info.join_status != ZB_STATUS_SUCCESS) && (HAL_GetTick() >= zigbee_app_info.join_delay))
  {
    struct ZbStartupT config;
    enum ZbStatusCodeT status;

    /* Configure Zigbee Logging (only need to do this once, but this is a good place to put it) */
    ZbSetLogging(zigbee_app_info.zb, ZB_LOG_MASK_LEVEL_5, NULL);

    /* Attempt to join a zigbee network */
    ZbStartupConfigGetProDefaults(&config);

    APP_DBG("Network config : APP_STARTUP_CENTRALIZED_ROUTER");
    config.startupControl = ZbStartTypeJoin;

    /* Using the default HA preconfigured Link Key */
    memcpy(config.security.preconfiguredLinkKey, sec_key_ha, ZB_SEC_KEYSIZE);
    config.channelList.count = 1;
    config.channelList.list[0].page = 0;
    config.channelList.list[0].channelMask = 1 << CHANNEL; /* Channel in use*/

    /* Using ZbStartupWait (blocking) here instead of ZbStartup, in order to demonstrate how to do
     * a blocking call on the M4. */
    status = ZbStartupWait(zigbee_app_info.zb, &config);

    APP_DBG("ZbStartup Callback (status = 0x%02x)", status);
    zigbee_app_info.join_status = status;

    if (status == ZB_STATUS_SUCCESS) {
      zigbee_app_info.join_delay = 0U;

      ZbPersistNotifyRegister(zigbee_app_info.zb,APP_ZIGBEE_persist_notify_cb,NULL);
      /* Call the callback once here to save persistence data */
      APP_ZIGBEE_persist_notify_cb(zigbee_app_info.zb,NULL);
    }
    else
    {
      APP_DBG("Startup failed, attempting again after a short delay (%d ms)", APP_ZIGBEE_STARTUP_FAIL_DELAY);

      // -- Reset Zigbee to be sure that we re-start with a good setting --
      if ( status == ZB_NWK_STATUS_INVALID_REQUEST ) {
        ZbReset( zigbee_app_info.zb );
      }

      zigbee_app_info.join_delay = HAL_GetTick() + APP_ZIGBEE_STARTUP_FAIL_DELAY;
    }
  }

  /* If Network forming/joining was not successful reschedule the current task to retry the process */
  if (zigbee_app_info.join_status != ZB_STATUS_SUCCESS)
  {
    UTIL_SEQ_SetTask(1U << CFG_TASK_ZIGBEE_NETWORK_FORM, CFG_SCH_PRIO_0);
  }
  else
  {

    /* Since we're using group addressing (broadcast), shorten the broadcast timeout */
    uint32_t bcast_timeout = 3;
    ZbNwkSet(zigbee_app_info.zb, ZB_NWK_NIB_ID_NetworkBroadcastDeliveryTime, &bcast_timeout, sizeof(bcast_timeout));

    /* Starting application init task */
    UTIL_SEQ_SetTask(1U << CFG_TASK_ZIGBEE_APP_START, CFG_SCH_PRIO_0);
  }
} /* APP_ZIGBEE_NwkForm */

/*************************************************************
 * ZbStartupWait Blocking Call
 *************************************************************/
struct ZbStartupWaitInfo {
  bool active;
  enum ZbStatusCodeT status;
};

static void ZbStartupWaitCb(enum ZbStatusCodeT status, void *cb_arg)
{
  struct ZbStartupWaitInfo *info = cb_arg;

  info->status = status;
  info->active = false;
  UTIL_SEQ_SetEvt(EVENT_ZIGBEE_STARTUP_ENDED);
} /* ZbStartupWaitCb */

enum ZbStatusCodeT ZbStartupWait(struct ZigBeeT *zb, struct ZbStartupT *config)
{
  struct ZbStartupWaitInfo *info;
  enum ZbStatusCodeT status;

  info = malloc(sizeof(struct ZbStartupWaitInfo));
  if (info == NULL) {
    return ZB_STATUS_ALLOC_FAIL;
  }
  memset(info, 0, sizeof(struct ZbStartupWaitInfo));

  info->active = true;
  status = ZbStartup(zb, config, ZbStartupWaitCb, info);
  if (status != ZB_STATUS_SUCCESS) {
    info->active = false;
    return status;
  }
  UTIL_SEQ_WaitEvt(EVENT_ZIGBEE_STARTUP_ENDED);
  status = info->status;
  free(info);
  return status;
} /* ZbStartupWait */

/**
 * @brief  Trace the error or the warning reported.
 * @param  ErrId :
 * @param  ErrCode
 * @retval None
 */
void APP_ZIGBEE_Error(uint32_t ErrId, uint32_t ErrCode)
{
  switch (ErrId) {
  default:
    APP_ZIGBEE_TraceError("ERROR Unknown ", 0);
    break;
  }
} /* APP_ZIGBEE_Error */

/**
 * @brief SW1 button pushed Pausing OTA process
 * @param  None
 * @retval None
 */
static void APP_ZIGBEE_SW1_Process(void)
{
  APP_DBG("SW1 PUSHED : Pausing OTA process");
  APP_ZIGBEE_OTA_Client_PauseDownload();

}

/**
 * @brief SW2 button pushed Clean NVM
 * @param  None
 * @retval None
 */
static void APP_ZIGBEE_SW2_Process(void)
{
  APP_DBG("SW2 PUSHED : Clearing NVM");
  HW_TS_Create(CFG_TIM_WAIT_BEOFRE_READ_ATTR, &TS_ID2, hw_ts_SingleShot, APP_ZIGBEE_persist_delete);
  HW_TS_Start(TS_ID2, 1000);
}

/**
 * @brief SW3 button pushed Resuming OTA process
 * @param  None
 * @retval None
 */
static void APP_ZIGBEE_SW3_Process(void)
{
  APP_DBG("SW3 PUSHED : Resuming OTA process");
  UTIL_SEQ_SetTask(1U << CFG_TASK_ZIGBEE_OTA_RESUME_DOWNLOAD, CFG_SCH_PRIO_0);
}

/*************************************************************
 *
 * LOCAL FUNCTIONS
 *
 *************************************************************/

/**
 * @brief  Warn the user that an error has occurred.In this case,
 *         the LEDs on the Board will start blinking.
 *
 * @param  pMess  : Message associated to the error.
 * @param  ErrCode: Error code associated to the module (Zigbee or other module if any)
 * @retval None
 */
static void APP_ZIGBEE_TraceError(const char *pMess, uint32_t ErrCode)
{
  APP_DBG("**** Fatal error = %s (Err = %d)", pMess, ErrCode);
  while (1U == 1U) {
    BSP_LED_Toggle(LED1);
    HAL_Delay(500U);
    BSP_LED_Toggle(LED2);
    HAL_Delay(500U);
    BSP_LED_Toggle(LED3);
    HAL_Delay(500U);
  }
} /* APP_ZIGBEE_TraceError */

/**
 * @brief Check if the Coprocessor Wireless Firmware loaded supports Zigbee
 *        and display associated information
 * @param  None
 * @retval None
 */
static void APP_ZIGBEE_CheckWirelessFirmwareInfo(void)
{
  WirelessFwInfo_t wireless_info_instance;
  WirelessFwInfo_t *p_wireless_info = &wireless_info_instance;

  if (SHCI_GetWirelessFwInfo(p_wireless_info) != SHCI_Success) {
    APP_ZIGBEE_Error((uint32_t)ERR_ZIGBEE_CHECK_WIRELESS, (uint32_t)ERR_INTERFACE_FATAL);
  }
  else {
    APP_DBG("**********************************************************");
    APP_DBG("WIRELESS COPROCESSOR FW:");
    /* Print version */
    APP_DBG("VERSION ID = %d.%d.%d", p_wireless_info->VersionMajor, p_wireless_info->VersionMinor, p_wireless_info->VersionSub);

    switch (p_wireless_info->StackType) {
    case INFO_STACK_TYPE_ZIGBEE_FFD:
      APP_DBG("FW Type : FFD Zigbee stack");
      break;
   case INFO_STACK_TYPE_ZIGBEE_RFD:
      APP_DBG("FW Type : RFD Zigbee stack");
      break;
    default:
      /* No Zigbee device supported ! */
      APP_ZIGBEE_Error((uint32_t)ERR_ZIGBEE_CHECK_WIRELESS, (uint32_t)ERR_INTERFACE_FATAL);
      break;
    }
    // print the application name
    char* __PathProject__ =(strstr(__FILE__, "Zigbee") ? strstr(__FILE__, "Zigbee") + 7 : __FILE__);
    char *del;
    if ( (strchr(__FILE__, '/')) == NULL)
        {del = strchr(__PathProject__, '\\');}
    else
        {del = strchr(__PathProject__, '/');}

        int index = (int) (del - __PathProject__);
        APP_DBG("Application flashed: %*.*s",index,index,__PathProject__);

    //print channel
    APP_DBG("Channel used: %d", CHANNEL);
    //print Link Key
    APP_DBG("Link Key: %.16s", sec_key_ha);
    //print Link Key value hex
    char Z09_LL_string[ZB_SEC_KEYSIZE*3+1];
    Z09_LL_string[0]=0;
    for(int str_index=0; str_index < ZB_SEC_KEYSIZE; str_index++)
      {
        sprintf(&Z09_LL_string[str_index*3],"%02x ",sec_key_ha[str_index]);
      }

    APP_DBG("Link Key value: %s",Z09_LL_string);
    //print clusters allocated
    APP_DBG("Clusters allocated are:");
    APP_DBG("OTA Client on Endpoint %d",SW1_ENDPOINT);
    APP_DBG("**********************************************************");
  }
} /* APP_ZIGBEE_CheckWirelessFirmwareInfo */


/*************************************************************
 *
 * WRAP FUNCTIONS
 *
 *************************************************************/

void APP_ZIGBEE_RegisterCmdBuffer(TL_CmdPacket_t *p_buffer)
{
  p_ZIGBEE_otcmdbuffer = p_buffer;
} /* APP_ZIGBEE_RegisterCmdBuffer */

Zigbee_Cmd_Request_t * ZIGBEE_Get_OTCmdPayloadBuffer(void)
{
  return (Zigbee_Cmd_Request_t *)p_ZIGBEE_otcmdbuffer->cmdserial.cmd.payload;
} /* ZIGBEE_Get_OTCmdPayloadBuffer */

Zigbee_Cmd_Request_t * ZIGBEE_Get_OTCmdRspPayloadBuffer(void)
{
  return (Zigbee_Cmd_Request_t *)((TL_EvtPacket_t *)p_ZIGBEE_otcmdbuffer)->evtserial.evt.payload;
} /* ZIGBEE_Get_OTCmdRspPayloadBuffer */

Zigbee_Cmd_Request_t * ZIGBEE_Get_NotificationPayloadBuffer(void)
{
  return (Zigbee_Cmd_Request_t *)(p_ZIGBEE_notif_M0_to_M4)->evtserial.evt.payload;
} /* ZIGBEE_Get_NotificationPayloadBuffer */

Zigbee_Cmd_Request_t * ZIGBEE_Get_M0RequestPayloadBuffer(void)
{
  return (Zigbee_Cmd_Request_t *)(p_ZIGBEE_request_M0_to_M4)->evtserial.evt.payload;
}

/**
 * @brief  This function is used to transfer the commands from the M4 to the M0.
 *
 * @param   None
 * @return  None
 */
void ZIGBEE_CmdTransfer(void)
{
  Zigbee_Cmd_Request_t *cmd_req = (Zigbee_Cmd_Request_t *)p_ZIGBEE_otcmdbuffer->cmdserial.cmd.payload;

  /* Zigbee OT command cmdcode range 0x280 .. 0x3DF = 352 */
  p_ZIGBEE_otcmdbuffer->cmdserial.cmd.cmdcode = 0x280U;
  /* Size = otCmdBuffer->Size (Number of OT cmd arguments : 1 arg = 32bits so multiply by 4 to get size in bytes)
   * + ID (4 bytes) + Size (4 bytes) */
  p_ZIGBEE_otcmdbuffer->cmdserial.cmd.plen = 8U + (cmd_req->Size * 4U);

  TL_ZIGBEE_SendM4RequestToM0();

  /* Wait completion of cmd */
  Wait_Getting_Ack_From_M0();
} /* ZIGBEE_CmdTransfer */

/**
 * @brief  This function is called when the M0+ acknowledge  the fact that it has received a Cmd
 *
 *
 * @param   Otbuffer : a pointer to TL_EvtPacket_t
 * @return  None
 */
void TL_ZIGBEE_CmdEvtReceived(TL_EvtPacket_t *Otbuffer)
{
  /* Prevent unused argument(s) compilation warning */
  UNUSED(Otbuffer);

  Receive_Ack_From_M0();
} /* TL_ZIGBEE_CmdEvtReceived */

/**
 * @brief  This function is called when notification from M0+ is received.
 *
 * @param   Notbuffer : a pointer to TL_EvtPacket_t
 * @return  None
 */
void TL_ZIGBEE_NotReceived(TL_EvtPacket_t *Notbuffer)
{
  p_ZIGBEE_notif_M0_to_M4 = Notbuffer;

  Receive_Notification_From_M0();
} /* TL_ZIGBEE_NotReceived */

/**
 * @brief  This function is called before sending any ot command to the M0
 *         core. The purpose of this function is to be able to check if
 *         there are no notifications coming from the M0 core which are
 *         pending before sending a new ot command.
 * @param  None
 * @retval None
 */
void Pre_ZigbeeCmdProcessing(void)
{
  UTIL_SEQ_WaitEvt(EVENT_SYNCHRO_BYPASS_IDLE);
} /* Pre_ZigbeeCmdProcessing */

/**
 * @brief  This function waits for getting an acknowledgment from the M0.
 *
 * @param  None
 * @retval None
 */
static void Wait_Getting_Ack_From_M0(void)
{
  UTIL_SEQ_WaitEvt(EVENT_ACK_FROM_M0_EVT);
} /* Wait_Getting_Ack_From_M0 */

/**
 * @brief  Receive an acknowledgment from the M0+ core.
 *         Each command send by the M4 to the M0 are acknowledged.
 *         This function is called under interrupt.
 * @param  None
 * @retval None
 */
static void Receive_Ack_From_M0(void)
{
  UTIL_SEQ_SetEvt(EVENT_ACK_FROM_M0_EVT);
} /* Receive_Ack_From_M0 */

/**
 * @brief  Receive a notification from the M0+ through the IPCC.
 *         This function is called under interrupt.
 * @param  None
 * @retval None
 */
static void Receive_Notification_From_M0(void)
{
    CptReceiveNotifyFromM0++;
    UTIL_SEQ_SetTask(1U << (uint32_t)CFG_TASK_NOTIFY_FROM_M0_TO_M4, CFG_SCH_PRIO_0);
}

/**
 * @brief  This function is called when a request from M0+ is received.
 *
 * @param   Notbuffer : a pointer to TL_EvtPacket_t
 * @return  None
 */
void TL_ZIGBEE_M0RequestReceived(TL_EvtPacket_t *Reqbuffer)
{
    p_ZIGBEE_request_M0_to_M4 = Reqbuffer;

    CptReceiveRequestFromM0++;
    UTIL_SEQ_SetTask(1U << (uint32_t)CFG_TASK_REQUEST_FROM_M0_TO_M4, CFG_SCH_PRIO_0);
}

/**
 * @brief Perform initialization of TL for Zigbee.
 * @param  None
 * @retval None
 */
void APP_ZIGBEE_TL_INIT(void)
{
    ZigbeeConfigBuffer.p_ZigbeeOtCmdRspBuffer = (uint8_t *)&ZigbeeOtCmdBuffer;
    ZigbeeConfigBuffer.p_ZigbeeNotAckBuffer = (uint8_t *)ZigbeeNotifRspEvtBuffer;
    ZigbeeConfigBuffer.p_ZigbeeNotifRequestBuffer = (uint8_t *)ZigbeeNotifRequestBuffer;
    TL_ZIGBEE_Init(&ZigbeeConfigBuffer);
}

/**
 * @brief Process the messages coming from the M0.
 * @param  None
 * @retval None
 */
void APP_ZIGBEE_ProcessNotifyM0ToM4(void)
{
    if (CptReceiveNotifyFromM0 != 0) {
        /* If CptReceiveNotifyFromM0 is > 1. it means that we did not serve all the events from the radio */
        if (CptReceiveNotifyFromM0 > 1U) {
            APP_ZIGBEE_Error(ERR_REC_MULTI_MSG_FROM_M0, 0);
        }
        else {
            Zigbee_CallBackProcessing();
        }
        /* Reset counter */
        CptReceiveNotifyFromM0 = 0;
    }
}

/**
 * @brief Process the requests coming from the M0.
 * @param
 * @return
 */
void APP_ZIGBEE_ProcessRequestM0ToM4(void)
{
    if (CptReceiveRequestFromM0 != 0) {
        Zigbee_M0RequestProcessing();
        CptReceiveRequestFromM0 = 0;
    }
}

