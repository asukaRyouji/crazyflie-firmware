/**
 *    ||          ____  _ __
 * +------+      / __ )(_) /_______________ _____  ___
 * | 0xBC |     / __  / / __/ ___/ ___/ __ `/_  / / _ \
 * +------+    / /_/ / / /_/ /__/ /  / /_/ / / /_/  __/
 *  ||  ||    /_____/_/\__/\___/_/   \__,_/ /___/\___/
 *
 * Crazyflie control firmware
 *
 * Copyright (C) 2011-2013 Bitcraze AB
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, in version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * cppm.c - Combined PPM / PPM-Sum driver
 */

/* FreeRtos includes */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "stm32fxxx.h"
#include "cppm.h"
#include "nvicconf.h"
#include "commander.h"
#include "static_mem.h"

#define DEBUG_MODULE  "CPPM"
#include "debug.h"
#include "log.h"

#define CPPM_USE_PB4


#ifdef CPPM_USE_PB8 // For now, PB8 does not work...
  #define CPPM_TIMER                   TIM10
  #define CPPM_TIMER_RCC               RCC_APB2Periph_TIM10
  #define CPPM_TIMER_CH_Init           TIM_OC1Init
  #define CPPM_TIMER_CH_PreloadConfig  TIM_OC1PreloadConfig
  #define CPPM_TIMER_CH_SetCompare     TIM_SetCompare1
  #define CPPM_GPIO_RCC                RCC_AHB1Periph_GPIOB
  #define CPPM_GPIO_PORT               GPIOB
  #define CPPM_GPIO_PIN                GPIO_Pin_8
  #define CPPM_GPIO_SOURCE             GPIO_PinSource8
  #define CPPM_GPIO_AF                 GPIO_AF_TIM10

  #define CPPM_TIM_PRESCALER           (168 - 1) // TIM10 clock running at sysclk (168 MHz). Prescaler of 168 will give 1MHz --> 1us tick.
#elif defined(CPPM_USE_PB4)
  #define CPPM_TIMER                   TIM3
  #define CPPM_TIMER_RCC               RCC_APB1Periph_TIM3
  #define CPPM_TIMER_CH_Init           TIM_OC1Init
  #define CPPM_TIMER_CH_PreloadConfig  TIM_OC1PreloadConfig
  #define CPPM_TIMER_CH_SetCompare     TIM_SetCompare1
  #define CPPM_GPIO_RCC                RCC_AHB1Periph_GPIOB
  #define CPPM_GPIO_PORT               GPIOB
  #define CPPM_GPIO_PIN                GPIO_Pin_4
  #define CPPM_GPIO_SOURCE             GPIO_PinSource4
  #define CPPM_GPIO_AF                 GPIO_AF_TIM3

  #define CPPM_TIM_PRESCALER           (84 - 1) // TIM3 clock running at sysclk/2 (84 MHz). Prescaler of 84 will give 1MHz --> 1us tick.
#elif defined(CPPM_USE_PB5)
  #define CPPM_TIMER                   TIM3
  #define CPPM_TIMER_RCC               RCC_APB1Periph_TIM3
  #define CPPM_TIMER_CH_Init           TIM_OC2Init
  #define CPPM_TIMER_CH_PreloadConfig  TIM_OC2PreloadConfig
  #define CPPM_TIMER_CH_SetCompare     TIM_SetCompare2
  #define CPPM_GPIO_RCC                RCC_AHB1Periph_GPIOB
  #define CPPM_GPIO_PORT               GPIOB
  #define CPPM_GPIO_PIN                GPIO_Pin_5
  #define CPPM_GPIO_SOURCE             GPIO_PinSource5
  #define CPPM_GPIO_AF                 GPIO_AF_TIM3

  #define CPPM_TIM_PRESCALER           (84 - 1) // TIM3 clock running at sysclk/2 (84 MHz). Prescaler of 84 will give 1MHz --> 1us tick.
#else
  #define CPPM_TIMER                   TIM14
  #define CPPM_TIMER_RCC               RCC_APB1Periph_TIM14
  #define CPPM_TIMER_CH_Init           TIM_OC1Init
  #define CPPM_TIMER_CH_PreloadConfig  TIM_OC1PreloadConfig
  #define CPPM_TIMER_CH_SetCompare     TIM_SetCompare1
  #define CPPM_GPIO_RCC                RCC_AHB1Periph_GPIOA
  #define CPPM_GPIO_PORT               GPIOA
  #define CPPM_GPIO_PIN                GPIO_Pin_7
  #define CPPM_GPIO_SOURCE             GPIO_PinSource7
  #define CPPM_GPIO_AF                 GPIO_AF_TIM14

  #define CPPM_TIM_PRESCALER           (84 - 1) // TIM14 clock running at sysclk/2. Will give 1us tick.
#endif

#define CPPM_MIN_PPM_USEC            1100
#define CPPM_MAX_PPM_USEC            1900

static xQueueHandle captureQueue;
STATIC_MEM_QUEUE_ALLOC(captureQueue, 64, sizeof(uint16_t));
static uint16_t prevCapureVal;
static bool captureFlag;
static bool isAvailible;

void cppmInit(void)
{
  TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
  TIM_ICInitTypeDef  TIM_ICInitStructure;
  GPIO_InitTypeDef GPIO_InitStructure;
  NVIC_InitTypeDef NVIC_InitStructure;

  RCC_AHB1PeriphClockCmd(CPPM_GPIO_RCC, ENABLE);
  RCC_APB1PeriphClockCmd(CPPM_TIMER_RCC, ENABLE);

  // Configure the GPIO for the timer input
  GPIO_StructInit(&GPIO_InitStructure);
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
  GPIO_InitStructure.GPIO_Pin = CPPM_GPIO_PIN;
  GPIO_Init(CPPM_GPIO_PORT, &GPIO_InitStructure);

  GPIO_PinAFConfig(CPPM_GPIO_PORT, CPPM_GPIO_SOURCE, CPPM_GPIO_AF);

  // Time base configuration. 1us tick.
  TIM_TimeBaseStructInit(&TIM_TimeBaseStructure);
  TIM_TimeBaseStructure.TIM_Prescaler = CPPM_TIM_PRESCALER;
  TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
  TIM_TimeBaseInit(CPPM_TIMER, &TIM_TimeBaseStructure);

  // Setup input capture using default config.
  TIM_ICStructInit(&TIM_ICInitStructure);
  TIM_ICInit(CPPM_TIMER, &TIM_ICInitStructure);

  #ifdef CPPM_USE_PB8
    NVIC_InitStructure.NVIC_IRQChannel = TIM1_UP_TIM10_IRQn;
  #elif defined(CPPM_USE_PB4) || defined(CPPM_USE_PB5)
    NVIC_InitStructure.NVIC_IRQChannel = TIM3_IRQn;
  #else 
    NVIC_InitStructure.NVIC_IRQChannel = TIM8_TRG_COM_TIM14_IRQn;
  #endif
  NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = NVIC_CPPM_PRI;
  NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
  NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
  NVIC_Init(&NVIC_InitStructure);

  captureQueue = STATIC_MEM_QUEUE_CREATE(captureQueue);

  #ifdef CPPM_USE_PB5
    TIM_ITConfig(CPPM_TIMER, TIM_IT_Update | TIM_IT_CC2, ENABLE);
  #else
    TIM_ITConfig(CPPM_TIMER, TIM_IT_Update | TIM_IT_CC1, ENABLE);
  #endif
  TIM_Cmd(CPPM_TIMER, ENABLE);
}

bool cppmIsAvailible(void)
{
  return isAvailible;
}

int cppmGetTimestamp(uint16_t *timestamp)
{
  ASSERT(timestamp);

  return xQueueReceive(captureQueue, timestamp, portMAX_DELAY);
}

void cppmClearQueue(void)
{
  xQueueReset(captureQueue);
}

float cppmConvert2Float(uint16_t timestamp, float min, float max, float deadband)
{
  if (timestamp < CPPM_MIN_PPM_USEC)
  {
    timestamp = CPPM_MIN_PPM_USEC;
  }
  if (timestamp > CPPM_MAX_PPM_USEC)
  {
    timestamp = CPPM_MAX_PPM_USEC;
  }

  float scale_raw = (float)(timestamp - CPPM_MIN_PPM_USEC) / (float)(CPPM_MAX_PPM_USEC - CPPM_MIN_PPM_USEC);
  float scale;
  if (deadband == 0)
  {
    scale = scale_raw;
  }
  else
  {
    if (scale_raw < (0.5f - deadband/2) )
    {
      scale = scale_raw / (1.f - deadband);
    }
    else if (scale_raw > (0.5f + deadband/2) )
    {
      scale = (scale_raw - deadband) / (1.f - deadband);
    }
    else
    {
      scale = 0.5f;
    }
    
  }
  
  return min + ((max - min) * scale);
}

uint16_t cppmConvert2uint16(uint16_t timestamp)
{
  if (timestamp < CPPM_MIN_PPM_USEC)
  {
    timestamp = CPPM_MIN_PPM_USEC;
  }
  if (timestamp > CPPM_MAX_PPM_USEC)
  {
    timestamp = CPPM_MAX_PPM_USEC;
  }

  uint16_t base = (timestamp - CPPM_MIN_PPM_USEC);

  return base * (65535 / (CPPM_MAX_PPM_USEC - CPPM_MIN_PPM_USEC));
}

#ifdef CPPM_USE_PB8
void __attribute__((used)) TIM1_UP_TIM10_IRQHandler()
{
  uint16_t capureVal;
  uint16_t capureValDiff;
  portBASE_TYPE xHigherPriorityTaskWoken = pdFALSE;

  if (TIM_GetITStatus(CPPM_TIMER, TIM_IT_CC1) != RESET)
  {
    if (TIM_GetFlagStatus(CPPM_TIMER, TIM_FLAG_CC1OF) != RESET)
    {
      //TODO: Handle overflow error
    }

    capureVal = TIM_GetCapture1(CPPM_TIMER);
    capureValDiff = capureVal - prevCapureVal;
    prevCapureVal = capureVal;

    xQueueSendFromISR(captureQueue, &capureValDiff, &xHigherPriorityTaskWoken);

    captureFlag = true;
    TIM_ClearITPendingBit(CPPM_TIMER, TIM_IT_CC1);
  }

  if (TIM_GetITStatus(CPPM_TIMER, TIM_IT_Update) != RESET)
  {
    // Update input status
    isAvailible = (captureFlag == true);
    captureFlag = false;
    TIM_ClearITPendingBit(CPPM_TIMER, TIM_IT_Update);
  }
}
#elif defined(CPPM_USE_PB4)
void __attribute__((used)) TIM3_IRQHandler()
{
  uint16_t capureVal;
  uint16_t capureValDiff;
  portBASE_TYPE xHigherPriorityTaskWoken = pdFALSE;

  if (TIM_GetITStatus(CPPM_TIMER, TIM_IT_CC1) != RESET)
  {
    if (TIM_GetFlagStatus(CPPM_TIMER, TIM_FLAG_CC1OF) != RESET)
    {
      //TODO: Handle overflow error
    }

    capureVal = TIM_GetCapture1(CPPM_TIMER);
    capureValDiff = capureVal - prevCapureVal;
    prevCapureVal = capureVal;

    xQueueSendFromISR(captureQueue, &capureValDiff, &xHigherPriorityTaskWoken);

    captureFlag = true;
    TIM_ClearITPendingBit(CPPM_TIMER, TIM_IT_CC1);
  }

  if (TIM_GetITStatus(CPPM_TIMER, TIM_IT_Update) != RESET)
  {
    // Update input status
    isAvailible = (captureFlag == true);
    captureFlag = false;
    TIM_ClearITPendingBit(CPPM_TIMER, TIM_IT_Update);
  }
}
#elif defined(CPPM_USE_PB5)
void __attribute__((used)) TIM3_IRQHandler()
{
  uint16_t capureVal;
  uint16_t capureValDiff;
  portBASE_TYPE xHigherPriorityTaskWoken = pdFALSE;

  if (TIM_GetITStatus(CPPM_TIMER, TIM_IT_CC2) != RESET)
  {
    if (TIM_GetFlagStatus(CPPM_TIMER, TIM_FLAG_CC2OF) != RESET)
    {
      //TODO: Handle overflow error
    }

    capureVal = TIM_GetCapture1(CPPM_TIMER);
    capureValDiff = capureVal - prevCapureVal;
    prevCapureVal = capureVal;

    xQueueSendFromISR(captureQueue, &capureValDiff, &xHigherPriorityTaskWoken);

    captureFlag = true;
    TIM_ClearITPendingBit(CPPM_TIMER, TIM_IT_CC2);
  }

  if (TIM_GetITStatus(CPPM_TIMER, TIM_IT_Update) != RESET)
  {
    // Update input status
    isAvailible = (captureFlag == true);
    captureFlag = false;
    TIM_ClearITPendingBit(CPPM_TIMER, TIM_IT_Update);
  }
}
#else
void __attribute__((used)) TIM8_TRG_COM_TIM14_IRQHandler()
{
  uint16_t capureVal;
  uint16_t capureValDiff;
  portBASE_TYPE xHigherPriorityTaskWoken = pdFALSE;

  if (TIM_GetITStatus(CPPM_TIMER, TIM_IT_CC1) != RESET)
  {
    if (TIM_GetFlagStatus(CPPM_TIMER, TIM_FLAG_CC1OF) != RESET)
    {
      //TODO: Handle overflow error
    }

    capureVal = TIM_GetCapture1(CPPM_TIMER);
    capureValDiff = capureVal - prevCapureVal;
    prevCapureVal = capureVal;

    xQueueSendFromISR(captureQueue, &capureValDiff, &xHigherPriorityTaskWoken);

    captureFlag = true;
    TIM_ClearITPendingBit(CPPM_TIMER, TIM_IT_CC1);
  }

  if (TIM_GetITStatus(CPPM_TIMER, TIM_IT_Update) != RESET)
  {
    // Update input status
    isAvailible = (captureFlag == true);
    captureFlag = false;
    TIM_ClearITPendingBit(CPPM_TIMER, TIM_IT_Update);
  }
}
#endif
