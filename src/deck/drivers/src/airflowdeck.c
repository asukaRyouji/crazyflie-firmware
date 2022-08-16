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
 * airflowdeck.c: Airflow sensor driver
 */

#define DEBUG_MODULE "AIRFLOW"

#include "FreeRTOS.h"
#include "task.h"
// added by Chenyao
#include "stabilizer_types.h"

#include "deck.h"
#include "system.h"
#include "debug.h"
#include "log.h"
#include "param.h"
// #include "range.h"
// #include "static_mem.h"

#include "airflowdeck.h"

// #include "cf_math.h"


static float wind_volts_last = 0;
static float airspeed_last = 0;

static bool isInit;

void airflowDeckInit(DeckInfo* info)
{
  if (isInit)
    return;

  xTaskCreate(airflowDeckTask, AIRFLOWDECK_TASK_NAME, AIRFLOWDECK_TASK_STACKSIZE, NULL, AIRFLOWDECK_TASK_PRI, NULL);

  isInit = true;
}

bool airflowDeckTest()
{
  bool testStatus;
  testStatus = true;

  if (!isInit)
    return false;

  return testStatus;
}

void airflowDeckTask(void* arg)
{
  systemWaitStart();
  TickType_t xLastWakeTime;

  xLastWakeTime = xTaskGetTickCount();

  while (1) {
    vTaskDelayUntil(&xLastWakeTime, M2T(1));

    wind_volts_last = analogReadVoltage(DECK_GPIO_MISO);
    flowvolt.volt = wind_volts_last;
    airspeed_last = 25.8666354823914f*wind_volts_last*wind_volts_last*wind_volts_last*wind_volts_last-1.664910993036515e2f*wind_volts_last*wind_volts_last*wind_volts_last+4.030483719450837e2f*wind_volts_last*wind_volts_last-4.325309182694595e2f*wind_volts_last+1.730907713055474e2f;
  }
}

static const DeckDriver airflow_deck = {
  .vid = 0xBC,
  .pid = 0x02,
  .name = "bcAirflowDeck",
  .usedGpio = DECK_USING_PA6,

  .init = airflowDeckInit,
  .test = airflowDeckTest,
};

DECK_DRIVER(airflow_deck);



PARAM_GROUP_START(deck)

PARAM_ADD_CORE(PARAM_UINT8 | PARAM_RONLY, airflowDeck, &isInit)
PARAM_GROUP_STOP(deck)

LOG_GROUP_START(airflow)
LOG_ADD(LOG_FLOAT, v_wind_ext, &wind_volts_last)
LOG_ADD(LOG_FLOAT, airspeed_ext, &airspeed_last)
// LOG_ADD(LOG_FLOAT, v_temp_ext, &temp_volts_last)
// LOG_ADD(LOG_FLOAT, tempC_ext, &tempC_last)
LOG_GROUP_STOP(airflow)
