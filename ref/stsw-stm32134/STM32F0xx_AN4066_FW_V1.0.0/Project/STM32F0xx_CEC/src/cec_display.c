/**
  ******************************************************************************
  * @file    STM32F0xx_CEC/src/cec_display.c
  * @author  MCD Application Team
  * @version V1.0.0
  * @date    02-August-2013
  * @brief   It displyas the CEC demonstration messages on either LCD or Hyperterminal.
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

/* Includes ------------------------------------------------------------------*/
#include "cec_display.h"

/** @addtogroup CEC_Networking
  * @{
  */
  
/** @defgroup CEC_DISPLAY 
  * @brief    It displyas the CEC demonstration messages on either LCD or 
  *           Hyperterminal
  * @{
  */ 

/** @defgroup CEC_DISPLAY_Private_Types
  * @{
  */ 
/**
  * @}
  */ 

/** @defgroup CEC_DISPLAY_Private_Defines
  * @{
  */ 
#define NUMBER_OF_CMD  12

#define LCD_DISPLAY    /*!< Comment this line in case to display the CEC
                        demonstration demo on Hyperterminal */
                       
#define USE_JOYSTICK   /*!< Comment this line in case to use the keyboard keys
                           instead of joystick push-buttons */

/**
  * @}
  */ 

/** @defgroup CEC_DISPLAY_Private_Macros
  * @{
  */ 
/**
  * @}
  */ 

/** @defgroup CEC_DISPLAY_Private_Variables
  * @{
  */ 
HDMI_CEC_Error TransmitStatus = HDMI_CEC_OK;
uint8_t FollowerAddress = 0;
__IO uint8_t KeyValue = 0;
extern __IO uint32_t ReceivedFrame, ReceiveStatus;
extern HDMI_CEC_Message HDMI_CEC_RX_MessageStructPrivate;
extern HDMI_CEC_Message HDMI_CEC_TX_MessageStructure;
extern __IO uint8_t MyLogicalAddress;
extern __IO uint16_t MyPhysicalAddress;
extern __IO uint8_t DeviceType;
extern uint8_t* HDMI_CEC_Follower_String[13][2];

/* CEC device commands */
uint8_t* HDMI_CEC_CMD_String[NUMBER_OF_CMD] =
  {
    (uint8_t*) "   FEATURE_ABORT    ",
    (uint8_t*) " GET CEC VERSION    ",
    (uint8_t*) " GIVE PHYSICAL ADR  ",
    (uint8_t*) "    GIVE OSD NAME   ",
    (uint8_t*) "   STANDBY VALUE    ",
    (uint8_t*) "   IMAGE VIEW ON    ",
    (uint8_t*) "    TEXT VIEW ON    ",
    (uint8_t*) "    RECORD OFF      ",
    (uint8_t*) " GIVE DEV VENDOR ID ",
    (uint8_t*) "     Volume Up      ",
    (uint8_t*) "    Volume Down     ",
    (uint8_t*) "    Volume Mute     "
  };

uint8_t HDMI_CEC_Follower_ADD[13] =
  {
    0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xA, 0xB, 0xF
  };

uint8_t HDMI_CEC_CMD_OP[NUMBER_OF_CMD] =
  {
    HDMI_CEC_OPCODE_FEATURE_ABORT, HDMI_CEC_OPCODE_GET_CEC_VERSION, 
    HDMI_CEC_OPCODE_GIVE_PHYSICAL_ADDRESS, HDMI_CEC_OPCODE_GIVE_OSD_NAME,
    HDMI_CEC_OPCODE_STANDBY, HDMI_CEC_OPCODE_IMAGE_VIEW_ON, 
    HDMI_CEC_OPCODE_TEXT_VIEW_ON, HDMI_CEC_OPCODE_RECORD_OFF,
    HDMI_CEC_OPCODE_GIVE_DEVICE_VENDOR_ID,
    HDMI_CEC_OPCODE_USER_CONTROL_PRESSED, 0x55, 0x66
  }; /* 0x55 = Volume Down, 0x66= Volume Mute*/


/**
  * @}
  */ 

/** @defgroup CEC_DISPLAY_Private_Function_Prototypes
  * @{
  */ 
#ifdef __GNUC__
 /* With GCC/RAISONANCE, small printf (option LD Linker->Libraries->Small printf
    set to 'Yes') calls __io_putchar() */
 #define PUTCHAR_PROTOTYPE int __io_putchar(int ch)
#else
  #define PUTCHAR_PROTOTYPE int fputc(int ch, FILE *f)
#endif /* __GNUC__ */

void GPIO_Configuration(void);
void NVIC_Configuration(void);
void USART_Configuration(void);

/**
  * @}
  */ 


/** @defgroup CEC_DISPLAY_Private_Functions
  * @{
  */ 

/**
  * @brief  DeInitialize the CEC Demo.
  * @param  None
  * @retval None
  */
void CEC_Display_DeInit(void)
{
  CEC_DeInit();
  I2C_DeInit(I2C2);
  GPIO_DeInit(GPIOB);
  RCC_DeInit();
}

/**
  * @brief  Initialize the CEC Demo.
  * @param  None
  * @retval None
  */
void CEC_Display_Init(void)
{ 
  /* GPIO configuration */
  GPIO_Configuration();
  
  /* NVIC configuration */
  NVIC_Configuration();

#ifdef LCD_DISPLAY

  /* LCD initialization */
  STM320518_LCD_Init();  
  
  /* Clear the LCD */
  LCD_Clear(LCD_COLOR_BLUE);
  LCD_SetBackColor(LCD_COLOR_BLACK);
  LCD_SetTextColor(LCD_COLOR_WHITE);
  
  CEC_Display(LCD_LINE_1, (uint8_t*) "   Select your CEC  ");
  CEC_Display(LCD_LINE_2, (uint8_t*) "     Device Type    ");
  LCD_SetBackColor(LCD_COLOR_BLUE);
  LCD_SetTextColor(LCD_COLOR_WHITE);
  
  CEC_Display(LCD_LINE_4, (uint8_t*) "RIGHT --> Recording ");
  CEC_Display(LCD_LINE_5, (uint8_t*) "   LEFT --> Tuner   ");
  CEC_Display(LCD_LINE_6, (uint8_t*) "   UP --> Playback  ");
  CEC_Display(LCD_LINE_7, (uint8_t*) "DOWN --> AudioSystem");
#else
  /* USART configuration */
  USART_Configuration();
  
  CEC_Display(LCD_LINE_1, "Select your CEC Device type  ");
  CEC_Display(LCD_LINE_4, "r --> Recording Device");
  CEC_Display(LCD_LINE_5, "l --> Tuner Device   ");
  CEC_Display(LCD_LINE_6, "u --> Playback Device ");
  CEC_Display(LCD_LINE_7, "d --> AudioSystem Device");
#endif
  
  /* Select the device type */  
  CEC_Display_SelectDeviceType();
  
  /* CEC Initialization */
  HDMI_CEC_Init();
  
  /* for Unregistered Device */  
  if (MyLogicalAddress == 0xF)
  {
#ifdef LCD_DISPLAY
    /* Clear the LCD */
    LCD_Clear(LCD_COLOR_BLUE);
    LCD_SetBackColor(LCD_COLOR_BLACK);
    LCD_SetTextColor(LCD_COLOR_WHITE);
#endif
    CEC_Display(LCD_LINE_5,(uint8_t*) "Unregistered Device");
    CEC_Display_DeInit();
    while(1);
  }
  
   /* Display initialization */
#ifdef LCD_DISPLAY
  LCD_Clear(LCD_COLOR_BLUE);
  LCD_SetBackColor(LCD_COLOR_BLUE);
  LCD_SetTextColor(LCD_COLOR_WHITE);
  CEC_Display(LCD_LINE_0, (uint8_t*) "   CEC device is   ");
  CEC_Display(LCD_LINE_1, (uint8_t*) "   initialized     ");
  CEC_Display(LCD_LINE_2, (uint8_t*) "   correctly   ");  
  /* Display physical and logical addesses */
  CEC_Display(LCD_LINE_3, (uint8_t*) "   Your Logical ");
  CEC_Display(LCD_LINE_4, (uint8_t*) " Address is :");
  LCD_SetTextColor(LCD_COLOR_RED);
  CEC_Display_LCD_DisplayString(LCD_LINE_4, LCD_COLUMN_14, (uint8_t*)"0x");
  CEC_Display_DisplayHexToChar (MyLogicalAddress, LCD_LINE_4, LCD_COLUMN_16);  
  LCD_SetTextColor(LCD_COLOR_WHITE);
  CEC_Display(LCD_LINE_5, (uint8_t*) "   Your Physical ");
  CEC_Display(LCD_LINE_6, (uint8_t*) " Address is :");
  LCD_SetTextColor(LCD_COLOR_RED);
  CEC_Display_LCD_DisplayString(LCD_LINE_6, LCD_COLUMN_14, (uint8_t*)"0x");  
  CEC_Display_DisplayHexToChar (MyPhysicalAddress >> 8, LCD_LINE_6, LCD_COLUMN_16);
  CEC_Display_DisplayHexToChar (MyPhysicalAddress & 0xFF, LCD_LINE_6, LCD_COLUMN_18);
  
  LCD_SetTextColor(LCD_COLOR_WHITE);
  LCD_SetBackColor(LCD_COLOR_BLACK);
  CEC_Display(LCD_LINE_8, (uint8_t*) "Press SEL button to ");
  CEC_Display(LCD_LINE_9, (uint8_t*) "  enter CEC menu    ");
#else
  CEC_Display(LCD_LINE_0, "CEC device is initialized correctly");
  /* Display physical and logical addesses */
  CEC_Display(LCD_LINE_3, "Your Logical Address is: ");
  printf("0x");  
  printf ("%x",MyLogicalAddress);
  CEC_Display(LCD_LINE_5, "Your Physical Address is :");
  printf("0x");  
  printf ("%x",MyPhysicalAddress);
  CEC_Display(LCD_LINE_8, "Press s key to enter CEC menu");
#endif
  
  /* Press SEL button to enter CEC menu */
  while(CEC_Display_ReadKey() != SEL)
  {} 
  
  /* Display CEC menu */
#ifdef LCD_DISPLAY
  LCD_Clear(LCD_COLOR_BLUE);
  LCD_SetTextColor(LCD_COLOR_WHITE);
  LCD_SetBackColor(LCD_COLOR_BLACK);
  CEC_Display(LCD_LINE_0, (uint8_t*) "Receive:            ");
  CEC_Display(LCD_LINE_7, (uint8_t*) "Send Status:        ");
  LCD_SetBackColor(LCD_COLOR_BLUE);
  LCD_SetBackColor(LCD_COLOR_RED);
#endif
}

/**
  * @brief  Select the device type
  * @param  None
  * @retval None
  */
void CEC_Display_SelectDeviceType(void)
{    
  do 
  {
    KeyValue = CEC_Display_ReadKey();
  } while((!KeyValue)|| (KeyValue == SEL));
  
  switch(KeyValue)
  {
  case RIGHT: 
    DeviceType = HDMI_CEC_RECORDING;
    break;
  case LEFT: 
    DeviceType = HDMI_CEC_TUNER;
    break;
  case UP: 
    DeviceType = HDMI_CEC_PLAYBACK;
    break;
  case DOWN: 
    DeviceType = HDMI_CEC_AUDIOSYSTEM;
    break;
  }
}

/**
  * @brief  Display the available CEC commands.
  * @param  None
  * @retval None
  */
void CEC_Display_CECAvailableCommands(void)
{
  uint32_t KeyState = 0;
  uint8_t sendcount = 0;
  int8_t i = 0, opcode = 0;
  
  /* select the follower address */
#ifdef LCD_DISPLAY
  /* Set the Back Color */
  LCD_SetBackColor(LCD_COLOR_WHITE);
  /* Set the Text Color */
  LCD_SetTextColor(LCD_COLOR_BLACK);
#endif
  
  CEC_Display(LCD_LINE_8, (uint8_t*) "Select Follower ADDR");
    
  FollowerAddress = CEC_Display_SelectFollowerAddress();
 
  while(CEC_Display_ReadKey() != NOKEY)
  {}
  
#ifdef LCD_DISPLAY
  /* Set the Back Color */
  LCD_SetBackColor(LCD_COLOR_WHITE);
  /* Set the Text Color */
  LCD_SetTextColor(LCD_COLOR_BLACK);
#endif
  CEC_Display(LCD_LINE_8, (uint8_t*) " Select CEC Command ");
  
#ifdef LCD_DISPLAY  
  /* Set the Back Color */
  LCD_SetBackColor(LCD_COLOR_BLUE);
  /* Set the Text Color */
  LCD_SetTextColor(LCD_COLOR_WHITE);
#endif
  CEC_Display(LCD_LINE_9, HDMI_CEC_CMD_String[i]); 
  opcode = HDMI_CEC_CMD_OP[i];  
  
  while(KeyState != SEL)
  {
    KeyState = CEC_Display_ReadKey();
    
    if(KeyState == LEFT)
    {
      if(i == 0)
      {
        i = NUMBER_OF_CMD;
      }
      i--;      
      CEC_Display(LCD_LINE_9, HDMI_CEC_CMD_String[i]);       
      opcode = HDMI_CEC_CMD_OP[i];      
    }
    else if (KeyState == RIGHT)
    {
      if(i == (NUMBER_OF_CMD - 1))
      { 
        i = -1;
      }
      i++;           
      CEC_Display(LCD_LINE_9, HDMI_CEC_CMD_String[i]); 
      opcode = HDMI_CEC_CMD_OP[i];      
    }
    else if (KeyState == SEL)
    {
      /* Send CEC Message */
      HDMI_CEC_TX_MessageStructure.Header = ((MyLogicalAddress << 4) | FollowerAddress);
      HDMI_CEC_TX_MessageStructure.Opcode = opcode;
      HDMI_CEC_TX_MessageStructure.TxMessageLength = 0x0;
      if (opcode == HDMI_CEC_OPCODE_USER_CONTROL_PRESSED)
      {
        HDMI_CEC_TX_MessageStructure.Operande[0] = 0x41;
        HDMI_CEC_TX_MessageStructure.TxMessageLength = 0x1;  
      }
      if (opcode == 0x55)
      {
        HDMI_CEC_TX_MessageStructure.Opcode = HDMI_CEC_OPCODE_USER_CONTROL_PRESSED;
        HDMI_CEC_TX_MessageStructure.Operande[0] = 0x42;
        HDMI_CEC_TX_MessageStructure.TxMessageLength = 0x1;  
      }
      if (opcode == 0x66)
      {
        HDMI_CEC_TX_MessageStructure.Opcode = HDMI_CEC_OPCODE_USER_CONTROL_PRESSED;
        HDMI_CEC_TX_MessageStructure.Operande[0] = 0x43;
        HDMI_CEC_TX_MessageStructure.TxMessageLength = 0x1;  
      }
      TransmitStatus = HDMI_CEC_TransmitMessage(&HDMI_CEC_TX_MessageStructure);
      
      /* retransmit message until 5 time */
      while((TransmitStatus != HDMI_CEC_OK)&& sendcount <0x5)
      {
        sendcount++;
        TransmitStatus = HDMI_CEC_TransmitMessage(&HDMI_CEC_TX_MessageStructure);
      }
     
      if (TransmitStatus == HDMI_CEC_OK)      
      { 
#ifdef LCD_DISPLAY
        LCD_SetBackColor(LCD_COLOR_GREEN);
#endif
        CEC_Display(LCD_LINE_7, (uint8_t*) "Send Status: Succeed");
      }
      else
      {
#ifdef LCD_DISPLAY
        LCD_SetBackColor(LCD_COLOR_RED);
#endif
        CEC_Display(LCD_LINE_7, (uint8_t*) "Send Status: Fail   ");
      }   
    }
    else if (KeyState == NOKEY)
    {}
        
    /* Display the received messages */
    while(ReceivedFrame)
    {
      CEC_Display_ReceiveInfoDisplay();
      /* Get the physical and logical address of new connected devices */
      HDMI_CEC_CommandCallBack();
    }          
  }	
}

/**
  * @brief  Select follower address.
  * @param  None
  * @retval The follower address
  */
uint8_t CEC_Display_SelectFollowerAddress(void)
{
  uint32_t KeyState = 0;
  static uint8_t address = 0;
  int8_t i = 0;
  while(CEC_Display_ReadKey() != NOKEY)
  {}
  
#ifdef LCD_DISPLAY  
  /* Set the Back Color */
  LCD_SetBackColor(LCD_COLOR_BLUE);
  /* Set the Text Color */
  LCD_SetTextColor(LCD_COLOR_WHITE);
#endif
  
  do
  {
    i++;     
  }while(*(uint8_t *)(HDMI_CEC_Follower_String[i][1]) == '0'); 
  
  CEC_Display(LCD_LINE_9, HDMI_CEC_Follower_String[i][0]); 
  address = HDMI_CEC_Follower_ADD[i];  
  
  while(KeyState != SEL)
  {
    KeyState = CEC_Display_ReadKey();
    
    if(KeyState == LEFT)
    {    
      do
      {
        if(i == 0)
        {
          i = 13;
        }
        i--; 
      }while(*(uint8_t *)(HDMI_CEC_Follower_String[i][1]) == '0');
      
      CEC_Display(LCD_LINE_9, HDMI_CEC_Follower_String[i][0]);       
      address = HDMI_CEC_Follower_ADD[i]; 
    }
    else if (KeyState == RIGHT)
    {
    
      do
      {
        if(i == 12)
        { 
          i = -1;
        }
        i++; 
      }while(*(uint8_t *)(HDMI_CEC_Follower_String[i][1]) == '0');
      
      CEC_Display(LCD_LINE_9, HDMI_CEC_Follower_String[i][0]); 
      address = HDMI_CEC_Follower_ADD[i];
    }
    else if (KeyState == SEL)
    {
      address = HDMI_CEC_Follower_ADD[i];
    }
    else if (KeyState == NOKEY)
    {
    }
    /* Display the received messages */
    while(ReceivedFrame)
    {
      CEC_Display_ReceiveInfoDisplay();
      /* Get the physical and logical address  of new connected devices*/
      HDMI_CEC_CommandCallBack();
    }   
  }
  return address;
}

/**
  * @brief  Displays the string on the required row and COLUMN
  * @param  Row: the Line where to display the character shape.
  * @param  Column: start Column address.
  * @param  *ptr: pointer to string to display on LCD.
  * @retval None
  */
void CEC_Display_LCD_DisplayString(uint8_t Row, uint16_t Column, uint8_t *ptr)
{
  uint32_t i = 0;
  uint16_t refcolumn = Column;

  /* Send the string character by character on lCD */
  while ( (*ptr !=0) && (i < 20))
  {
    /* Display one character on LCD */
    LCD_DisplayChar(Row, refcolumn, *ptr);
    /* Decrement the Column position by 16 */
    refcolumn -= 16;
    /* Point on the next character */
    ptr++;
    /* Increment the character counter */
    i++;
  }
}

/**
  * @brief  Displays a data byte on the LCD in Hex format.
  * @param  Data: the data to be displayed.
  * @param  Line: the line where the data will be displayed.
  * @param  Column: the Column where the data will be displayed.
  * @retval None
  */
void CEC_Display_DisplayHexToChar (uint8_t Data, uint16_t Line, uint16_t Column)
{
  LCD_DisplayChar(Line, (Column - 16), CEC_Display_HexToChar(Data));
  LCD_DisplayChar(Line, Column, CEC_Display_HexToChar(Data >> 4));
}

/**
  * @brief  Converts a data (four bits) to a character to be displayed  in Hex format.
  * @param  Data: byte to be converted.
  * @retval None
  */
uint8_t CEC_Display_HexToChar(uint8_t Data)
{
  Data &= 0xF;

  switch(Data)
  {
    case 0xA: return 0x41;
    case 0xB: return 0x42;
    case 0xC: return 0x43;
    case 0xD: return 0x44;
    case 0xE: return 0x45;
    case 0xF: return 0x46;
    default: return (Data + 0x30);
  }
}

/**
  * @brief  Displays the content of a buffer on the LCD in Hex format.
  * @param  DataBuffer: a pointer to the buffer to be displayed.
  * @param  BufferSize: the size of the buffer in byte.
  * @param  Line: the line where the data will be displayed.
  * @retval None
  */
void CEC_Display_DisplayBuffer(uint8_t *DataBuffer, uint8_t BufferSize, uint16_t Line)
{
  uint8_t i = 0;
  uint16_t Column = LCD_COLUMN_6;
  uint16_t line = Line;
 
  CEC_Display_LCD_DisplayString(Line, LCD_COLUMN_0,(uint8_t*) "                    ");
  CEC_Display_LCD_DisplayString(Line+24, LCD_COLUMN_0, (uint8_t*) "                    ");
  CEC_Display_LCD_DisplayString(Line+48, LCD_COLUMN_0, (uint8_t*) "                    ");
  CEC_Display_LCD_DisplayString(Line, LCD_COLUMN_0, (uint8_t*) " Data:");

  for (i = 0; i < BufferSize; i++)
  { 
    CEC_Display_DisplayHexToChar(*(DataBuffer++), line, Column);
    Column -= 32;
    LCD_DisplayChar(line, Column, ' ');
    if ((i == 4) || (i == 0xA))
    {
      line += 24;
      Column = LCD_COLUMN_0;
    }
    Column -= 16;
  }
}

/**
  * @brief  Displays the CEC messages on either LCD or Hyperterminal.
  * @param  Line: the Line where to display the character shape .
  *   This parameter can be one of the following values:
  *     @arg Linex: where x can be 0..9
  * @param  *ptr: pointer to string to display on LCD or Hyperterminal.
  * @retval None
  */
void CEC_Display (uint8_t Line, uint8_t *ptr)
{
uint8_t *pointer = ptr;
#ifdef LCD_DISPLAY
  uint8_t line = Line;
  LCD_DisplayStringLine(line,  pointer);
#else
  printf("\r\n");
  printf((char const*)pointer); 
#endif
}

/**
  * @brief  Displays the CEC received messages on the required row and COLUMN
  * @param  None
  * @retval None
  */
void CEC_Display_ReceiveInfoDisplay(void)
{
#ifdef LCD_DISPLAY 
  /* If a frame has been received */
  if(ReceivedFrame)
  {
    ReceivedFrame = 0;
    LCD_SetBackColor(LCD_COLOR_BLUE);
    
    /* Check if the frame has been received correctly */
    if(ReceiveStatus)
    {
      LCD_SetBackColor(LCD_COLOR_GREEN);
      CEC_Display_LCD_DisplayString(LCD_LINE_0, LCD_COLUMN_10, (uint8_t*) "Succeeded ");
    }
    else /* The receive was failed */
    {
      LCD_SetBackColor(LCD_COLOR_RED);
      CEC_Display_LCD_DisplayString(LCD_LINE_0, LCD_COLUMN_10, (uint8_t*) " Failed   ");
    }
    /* Display the receive status, the initiator address, the received data 
    frame and  the number of received bytes  */
    LCD_SetBackColor(LCD_COLOR_BLUE);
    CEC_Display_LCD_DisplayString(LCD_LINE_1, LCD_COLUMN_0, (uint8_t*) " Sender Address = ");
    CEC_Display_DisplayHexToChar ((uint8_t)((HDMI_CEC_RX_MessageStructPrivate.Header >> 0x4) & 0x0F), LCD_LINE_1, LCD_COLUMN_17);
    CEC_Display_LCD_DisplayString(LCD_LINE_2, LCD_COLUMN_0, (uint8_t*) " Number of bytes:    ");
    CEC_Display_DisplayHexToChar ((uint8_t)(HDMI_CEC_RX_MessageStructPrivate.RxMessageLength + 2), LCD_LINE_2, LCD_COLUMN_17);
    CEC_Display_LCD_DisplayString(LCD_LINE_3, LCD_COLUMN_0, (uint8_t*) " Message Opcode:     ");
    CEC_Display_DisplayHexToChar ((uint8_t)HDMI_CEC_RX_MessageStructPrivate.Opcode, LCD_LINE_3, LCD_COLUMN_17);    
    CEC_Display_DisplayBuffer((uint8_t*)HDMI_CEC_RX_MessageStructPrivate.Operande, (HDMI_CEC_RX_MessageStructPrivate.RxMessageLength), LCD_LINE_4);
  }
#else
  /* If a frame has been received */
  uint8_t k = 0;
  if(ReceivedFrame)
  {
    ReceivedFrame = 0;
    
    /* Check if the frame has been received correctly */
    if(ReceiveStatus)
    {
       printf("\n\r"); 
       printf("Receive status : Succeeded");
    }
    else /* The receive was failed */
    {
       printf("\n\r"); 
       printf("Receive status : Failed");
    }
    /* Display the receive status, the initiator address, the received data 
    frame and  the number of received bytes  */
    printf("\n\r"); 
    printf(" Sender Address =  0x");
    printf ("%x",(uint8_t)((HDMI_CEC_RX_MessageStructPrivate.Header >> 0x4) & 0x0F));
    printf("\n\r"); 
    printf(" Number of bytes:  0x");
    printf ("%x",(uint8_t)(HDMI_CEC_RX_MessageStructPrivate.RxMessageLength + 2));
    printf("\n\r");
    printf(" Message Opcode:  0x");
    printf ("%x",(uint8_t)HDMI_CEC_RX_MessageStructPrivate.Opcode);
    printf("\n\r");

    for (k = 0; k < HDMI_CEC_RX_MessageStructPrivate.RxMessageLength; k++)
    { 
      printf("operande %d:   0x%x",k,(uint8_t*)HDMI_CEC_RX_MessageStructPrivate.Operande[k]);
      printf("\n\r");
    }
  }
#endif
}

/**
  * @brief  Reads key from demoboard or from th hyperterminal.
  * @param  None
  * @retval Return RIGHT, LEFT, SEL, UP, DOWN or NOKEY
  */
uint8_t CEC_Display_ReadKey(void)
{
#ifdef USE_JOYSTICK  
/* "right" key is pressed */
  if(STM_EVAL_PBGetState(BUTTON_RIGHT))
  {
    while(STM_EVAL_PBGetState(BUTTON_RIGHT) != Bit_RESET);
    return RIGHT;
  }
  /* "left" key is pressed */
  if(STM_EVAL_PBGetState(BUTTON_LEFT))
  {
    while(STM_EVAL_PBGetState(BUTTON_LEFT) != Bit_RESET); 
    return LEFT;     
  }
  /* "up" key is pressed */
  if(STM_EVAL_PBGetState(BUTTON_UP))
  {
    while(STM_EVAL_PBGetState(BUTTON_UP) != Bit_RESET);
    return UP; 
  }
  /* "down" key is pressed */
  if(STM_EVAL_PBGetState(BUTTON_DOWN))
  {
    while(STM_EVAL_PBGetState(BUTTON_DOWN) != Bit_RESET);
    return DOWN; 
  }
  /* "sel" key is pressed */
  if(STM_EVAL_PBGetState(BUTTON_SEL))
  {
    while(STM_EVAL_PBGetState(BUTTON_SEL) != Bit_RESET);
    return SEL; 
  }
  /* No key is pressed */
  else 
  {
    return NOKEY;
  }
#else
  if(USART_GetFlagStatus(EVAL_COM1, USART_FLAG_RXNE) != RESET)
  { 
    switch(USART_ReceiveData(EVAL_COM1))
    {
    case 0x6C:  /* 'l' ascii code */
      return LEFT;
      
    case 0x72: /* 'r' ascii code */
      return RIGHT;
      
    case 0x75: /* 'u' ascii code */
      return UP;
      
    case 0x64: /* 'd' ascii code */
      return DOWN;
      
    case 0x73: /* 's' ascii code */
      return SEL;
    default:
      return NOKEY; 
    }
  }
  else
  {
    return NOKEY;
  }
#endif  
}

/**
  * @brief  Configures the different GPIO ports.
  * @param  None
  * @retval None
  */
void GPIO_Configuration(void)
{
  /* Configure the joystick buttons in moade GPIO */
  STM_EVAL_PBInit(BUTTON_SEL,   BUTTON_MODE_GPIO);
  STM_EVAL_PBInit(BUTTON_UP,    BUTTON_MODE_GPIO);
  STM_EVAL_PBInit(BUTTON_DOWN,  BUTTON_MODE_GPIO);
  STM_EVAL_PBInit(BUTTON_LEFT,  BUTTON_MODE_GPIO);
  STM_EVAL_PBInit(BUTTON_RIGHT, BUTTON_MODE_GPIO);
}

/**
  * @brief  Configures the different NVIC interrupts.
  * @param  None
  * @retval None
  */
void NVIC_Configuration(void)
{
  NVIC_InitTypeDef NVIC_InitStructure;

  /* Enable the CEC global Interrupt (with higher priority) */
  NVIC_InitStructure.NVIC_IRQChannel = CEC_IRQn;
  NVIC_InitStructure.NVIC_IRQChannelPriority = 1;
  NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
  NVIC_Init(&NVIC_InitStructure);  
}

/**
  * @brief  Configures the USART.
  * @param  None
  * @retval None
  */
void USART_Configuration(void)
{
  USART_InitTypeDef USART_InitStructure;
  
  /* USARTx configured as follow:
  - BaudRate = 115200 baud  
  - Word Length = 8 Bits
  - One Stop Bit
  - No parity
  - Hardware flow control disabled (RTS and CTS signals)
  - Receive and transmit enabled
  */
  USART_InitStructure.USART_BaudRate = 115200;
  USART_InitStructure.USART_WordLength = USART_WordLength_8b;
  USART_InitStructure.USART_StopBits = USART_StopBits_1;
  USART_InitStructure.USART_Parity = USART_Parity_No;
  USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
  USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
  
  STM_EVAL_COMInit(COM1, &USART_InitStructure);
}

/**
  * @brief  Retargets the C library printf function to the USART.
  * @param  None
  * @retval None
  */
PUTCHAR_PROTOTYPE
{
  /* Place your implementation of fputc here */
  /* e.g. write a character to the USART */
  USART_SendData(EVAL_COM1, (uint8_t) ch);

  /* Loop until the end of transmission */
  while (USART_GetFlagStatus(EVAL_COM1, USART_FLAG_TC) == RESET)
  {}

  return ch;
}

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
