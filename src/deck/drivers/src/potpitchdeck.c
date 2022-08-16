/**
 *    ||          ____  _ __
 * +------+      / __ )(_) /_______________ _____  ___
 * | 0xBC |     / __  / / __/ ___/ ___/ __ `/_  / / _ \
 * +------+    / /_/ / / /_/ /__/ /  / /_/ / / /_/  __/
 *  ||  ||    /_____/_/\__/\___/_/   \__,_/ /___/\___/
 *
 * Crazyflie control firmware
 *
 * Copyright (C) 2021 BitCraze AB
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
 * potpitch.c: dihedral POT driver
 */

#define DEBUG_MODULE "POTPITCHDECK"

#include "FreeRTOS.h"
#include "task.h"

#include "deck.h"
#include "system.h"
#include "debug.h"
#include "log.h"
#include "param.h"
// #include "range.h"
// #include "static_mem.h"

#include "potpitchdeck.h"

// #include "cf_math.h"

static float volts_pitch = 0;

static bool isInit;

void potPitchDeckInit(DeckInfo* info)
{
  if (isInit)
    return;

  xTaskCreate(potPitchDeckTask, POTPITCHDECK_TASK_NAME, POTPITCHDECK_TASK_STACKSIZE, NULL, POTPITCHDECK_TASK_PRI, NULL);

  isInit = true;
}

bool potPitchDeckTest(void)
{
  bool testStatus;
  testStatus = true;

  if (!isInit)
    return false;

  return testStatus;
}

void potPitchDeckTask(void* arg)
{
  systemWaitStart();
  TickType_t xLastWakeTime;

  xLastWakeTime = xTaskGetTickCount();

  while (1) {
    vTaskDelayUntil(&xLastWakeTime, M2T(1));

    volts_pitch = analogReadVoltage(DECK_GPIO_MOSI);
    // airspeed_last = 25.8666354823914*pow(wind_volts_last,4)+4.030483719450837e2*pow(wind_volts_last,3)-1.664910993036515e2*pow(wind_volts_last,2)-4.325309182694595e2*pow(wind_volts_last,1)+1.730907713055474e2;
  }
}

static const DeckDriver potpitch_deck = {
  .vid = 0xBC,
  .pid = 0x0A,
  .name = "bcPotPitchDeck",
  .usedGpio = DECK_USING_PA7,

  .init = potPitchDeckInit,
  .test = potPitchDeckTest,
};

DECK_DRIVER(potpitch_deck);



PARAM_GROUP_START(deck)

PARAM_ADD_CORE(PARAM_UINT8 | PARAM_RONLY, potPitchDeck, &isInit)
PARAM_GROUP_STOP(deck)

LOG_GROUP_START(potPitch)
LOG_ADD(LOG_FLOAT, volt_pitch, &volts_pitch)
// LOG_ADD(LOG_FLOAT, airspeed_ext, &airspeed_last)
LOG_GROUP_STOP(potPitch)
