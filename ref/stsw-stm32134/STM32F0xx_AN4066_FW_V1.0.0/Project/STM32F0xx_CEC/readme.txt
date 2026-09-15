/**
  @page HDMI_CEC_Networking  AN4066 HDMI CEC Networking Readme file
  
  @verbatim
  ******************** (C) COPYRIGHT 2013 STMicroelectronics *******************
  * @file    STM32F0xx_CEC/readme.txt 
  * @author  MCD Application Team
  * @version V1.0.0
  * @date    02-August-2013
  * @brief   Description of the AN4066 "CEC networking using STM32F0xx microcontrollers".
  ******************************************************************************
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
   @endverbatim

@par Description

This directory contains a set of sources files and pre-configured projects for 
the AN4066 "Developing an HDMI-CEC network using an STM32F0xx microcontroller". 


The CEC demonstration contains two different configurations:
1. Use the joystick push-buttons and display the CEC demo messages on the
   STM320518-EVAL board LCD.
2. Use the keyboard keys (r:right, l:left, u: up, d: down and s:select) and display
   the CEC demo messages on the HyperTerminal.
   
  To use the second configuration, user must comment the following defines in
  cec_display.c file:
     #define LCD_DISPLAY
     #define USE_JOYSTICK


@par Directory contents 

 - "STM32F0xx_CEC\inc": contains the CEC firmware header files 
    - STM32F0xx_CEC/inc/main.h              Main header file
    - STM32F0xx_CEC/inc/cec_display.h       Header for cec_display.c    
    - STM32F0xx_CEC/inc/stm32f0xx_conf.h    Library Configuration file
    - STM32F0xx_CEC/inc/stm32f0xx_it.h      Header for stm32f0xx_it.c    
 - "STM32F0xx_CEC\src": contains the CEC firmware source files
    - STM32F0xx_CEC/src/main.c              Main program
    - STM32F0xx_CEC/src/stm32f0xx_it.c      Interrupt handlers
    - STM32F0xx_CEC/src/system_stm32f0xx.c  STM32F0xx system source file    
    - STM32F0xx_CEC/src/cec_display.c       Provides the firmware which allows to display all CEC messages. 
 - "STM32F0xx_CEC\MDK-ARM": contains pre-configured project for MDK-ARM toolchain
 - "STM32F0xx_CEC\EWARM": contains pre-configured project for EWARM toolchain
 - "STM32F0xx_CEC\TASKING": contains pre-configured project for TASKING toolchain
 - "STM32F0xx_CEC\TrueSTUDIO": contains pre-configured project for TrueSTUDIO toolchain 


@par Hardware and Software environment  

  - This example runs on STM32F0xx Devices.
  
  - This example has been tested with STMicroelectronics STM32F0518-EVAL board RevB 
    and can be easily tailored to any other supported device and development board.
    
  - STM32F0518-EVAL :
     - Connect 2 or more (max 10) boards using HDMI Cables on HDMI-CEC connectors (CN3 or CN4)

   
@par How to use it ?       
      
In order to load the CEC Demo code, you have do the following:
 - EWARM
    - Open the Project.eww workspace
    - Rebuild all files: Project->Rebuild all
    - Load project image: Project->Debug
    - Run program: Debug->Go(F5)

 - MDK-ARM 
    - Open the Project.uvproj project
    - Rebuild all files: Project->Rebuild all target files
    - Load project image: Debug->Start/Stop Debug Session
    - Run program: Debug->Run (F5)

 - TASKING
    - Open TASKING toolchain.
    - Click on File->Import, select General->'Existing Projects into Workspace' 
      and then click "Next". 
    - Browse to the TASKING workspace directory and select the project: STM320518-EVAL
     - Rebuild all project files: Select the project in the "Project explorer" 
       window then click on Project->build project menu.
    - Run program: Select the project in the "Project explorer" window then click 
      Run->Debug (F11)

 - TrueSTUDIO 
    - Open the TrueSTUDIO toolchain.
    - Click on File->Switch Workspace->Other and browse to TrueSTUDIO workspace 
      directory.
    - Click on File->Import, select General->'Existing Projects into Workspace' 
      and then click "Next". 
    - Browse to the TrueSTUDIO workspace directory and select the project: STM320518-EVAL
      window then click on Project->build project menu.
    - Run program: Select the project in the "Project explorer" window then click 
      Run->Debug (F11)  


 * <h3><center>&copy; COPYRIGHT STMicroelectronics</center></h3>
 */
