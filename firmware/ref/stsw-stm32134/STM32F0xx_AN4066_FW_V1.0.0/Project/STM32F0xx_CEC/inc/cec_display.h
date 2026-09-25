/**
  ******************************************************************************
  * @file    STM32F0xx_CEC/inc/cec_display.h
  * @author  MCD Application Team
  * @version V1.0.0
  * @date    02-August-2013
  * @brief   Header file for cec_display.c file.
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; COPYRIGHT 2013 STMicroelectronics</center></h2>
  *
  * Licensed under MCD-ST Liberty SW License Agreement V2, (the "License");
  * You may not use this file except in compliance with the License.
  * You may obtain a copy of the License at:
  *
  *        http://www.st.com/software_license_agreement_liberty_v2
  *
  * Unless required by applicable law or agreed to in writing, software 
  * distributed under the License is distributed on an "AS IS" BASIS, 
  * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  * See the License for the specific language governing permissions and
  * limitations under the License.
  *
  ******************************************************************************
  */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __CEC_DISPLAY_H
#define __CEC_DISPLAY_H

#ifdef __cplusplus
 extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/** @addtogroup CEC_Networking
  * @{
  */
  
/** @defgroup CEC_DISPLAY 
  * @{
  */

   
/** @defgroup CEC_DISPLAY_Exported_Types
  * @{
  */
/**
  * @}
  */

/** @defgroup CEC_DISPLAY_Exported_Constants
  * @{
  */
/** 
  * @brief Joystick keys  
  */

#define  NOKEY  0
#define  SEL    1
#define  RIGHT  2
#define  LEFT   3
#define  UP     4
#define  DOWN   5


/**
  * @brief LCD COLUMNs
  */   
#define LCD_COLUMN_0        320
#define LCD_COLUMN_1        304
#define LCD_COLUMN_2        288
#define LCD_COLUMN_3        272
#define LCD_COLUMN_4        256
#define LCD_COLUMN_5        240
#define LCD_COLUMN_6        224
#define LCD_COLUMN_7        208
#define LCD_COLUMN_8        192
#define LCD_COLUMN_9        176
#define LCD_COLUMN_10       160
#define LCD_COLUMN_11       144
#define LCD_COLUMN_12       128
#define LCD_COLUMN_13       112
#define LCD_COLUMN_14        96
#define LCD_COLUMN_15        80
#define LCD_COLUMN_16        64
#define LCD_COLUMN_17        48
#define LCD_COLUMN_18        32
#define LCD_COLUMN_19        16
/**
  * @}
  */

/** @defgroup CEC_DISPLAY_Exported_Macros
  * @{
  */
/**
  * @}
  */

/** @defgroup CEC_DISPLAY_Exported_Functions
  * @{
  */
void CEC_Display_DeInit(void);
void CEC_Display_Init(void);
void CEC_Display_SelectDeviceType(void);
void CEC_Display_CECAvailableCommands(void);
uint8_t CEC_Display_SelectFollowerAddress(void);
void CEC_Display_ReceiveInfoDisplay(void);
void CEC_Display_DisplayHexToChar(uint8_t Data, uint16_t Line, uint16_t column);
void CEC_Display(uint8_t Line, uint8_t *ptr);
uint8_t CEC_Display_HexToChar(uint8_t Data);
void CEC_Display_DisplayBuffer(uint8_t *DataBuffer, uint8_t BufferSize, uint16_t Line);
void CEC_Display_LCD_DisplayString(uint8_t Row, uint16_t Column, uint8_t *ptr);
uint8_t CEC_Display_ReadKey(void);

#ifdef __cplusplus
}
#endif

#endif /* __CEC_DISPLAY_H */

/**
  * @}
  */
  
/**
  * @}
  */

/**
  * @}
  */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
