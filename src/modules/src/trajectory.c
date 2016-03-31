/**
 *    ||          ____  _ __
 * +------+      / __ )(_) /_______________ _____  ___
 * | 0xBC |     / __  / / __/ ___/ ___/ __ `/_  / / _ \
 * +------+    / /_/ / / /_/ /__/ /  / /_/ / / /_/  __/
 *  ||  ||    /_____/_/\__/\___/_/   \__,_/ /___/\___/
 *
 * Crazyflie control firmware
 *
 * Copyright (C) 2012 BitCraze AB
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
 * trajectory.c: Module to receive trajectory from the host
 */


// #include <string.h>
#include <errno.h>
// #include <stdint.h>
// #include <stdbool.h>

/* FreeRtos includes */
#include "FreeRTOS.h"
#include "task.h"
// #include "timers.h"
// #include "semphr.h"

// #include "config.h"
#include "crtp.h"
#include "trajectory.h"

//#include "console.h"
//#include "cfassert.h"

// Private types
enum TrajectoryCommand_e {
  COMMAND_RESET = 0,
  COMMAND_ADD   = 1,
  COMMAND_START = 2,
};

// struct data_reset {
// } __attribute__((packed));

struct data_add {
  uint16_t time_from_start; // ms; 0 marks the beginning of the execution
  float x; // m
  float y; // m
  float z; // m
  float velocity_x; // m/s
  float velocity_y; // m/s
  float velocity_z; // m/s
} __attribute__((packed));


#define MAX_TRAJECTORY_ENTRIES 100

struct trajectoryEntry {
  uint16_t time_from_start;
  struct trajectoryPoint_s point;
};

// Global variables
static bool isInit = false;
static CRTPPacket p;
static struct trajectoryEntry trajectory[MAX_TRAJECTORY_ENTRIES];
static uint16_t numEntries = 0;
static uint16_t currentEntry = 0;
static struct trajectoryEntry lastValidTrajectoryEntry;
static uint64_t startTime = 0;

//Private functions
static void trajectoryTask(void * prm);
static int trajectoryReset(void);
static int trajectoryAdd(struct data_add* data);
static int trajectoryStart(void);

void trajectoryInit(void)
{
  if(isInit) {
    return;
  }

  //Start the trajectory task
  xTaskCreate(trajectoryTask, TRAJECTORY_TASK_NAME,
              TRAJECTORY_TASK_STACKSIZE, NULL, TRAJECTORY_TASK_PRI, NULL);

  isInit = true;
}

bool trajectoryTest(void)
{
  return isInit;
}

void trajectoryGetCurrentGoal(struct trajectoryPoint_s* goal)
{
  uint64_t t = xTaskGetTickCount();

  // First, find the correct entry
  while (currentEntry < numEntries
         && trajectory[currentEntry].time_from_start + startTime < t) {
    ++currentEntry;
  }

  // linearely interpolate for current time
  if (currentEntry < numEntries
      && currentEntry > 0) {


    uint16_t timeSpan = trajectory[currentEntry].time_from_start - trajectory[currentEntry-1].time_from_start;
    uint16_t dt = t - trajectory[currentEntry-1].time_from_start;
    float factor = dt / timeSpan;

    const struct trajectoryPoint_s* last = &trajectory[currentEntry-1].point;
    const struct trajectoryPoint_s* now = &trajectory[currentEntry].point;

    goal->x = last->x + (now->x - last->x) * factor;
    goal->y = last->y + (now->y - last->y) * factor;
    goal->z = last->z + (now->z - last->z) * factor;
    goal->velocity_x = last->velocity_x + (now->velocity_x - last->velocity_x) * factor;
    goal->velocity_y = last->velocity_y + (now->velocity_y - last->velocity_y) * factor;
    goal->velocity_z = last->velocity_z + (now->velocity_z - last->velocity_z) * factor;
  } else {
    // set to last valid entry
    goal->x = lastValidTrajectoryEntry.point.x;
    goal->y = lastValidTrajectoryEntry.point.y;
    goal->z = lastValidTrajectoryEntry.point.z;
    goal->velocity_x = lastValidTrajectoryEntry.point.x;
    goal->velocity_y = lastValidTrajectoryEntry.point.velocity_y;
    goal->velocity_z = lastValidTrajectoryEntry.point.velocity_z;
  }
}

void trajectoryTask(void * prm)
{
  int ret;
  crtpInitTaskQueue(CRTP_PORT_TRAJECTORY);

  while(1) {
    crtpReceivePacketBlock(CRTP_PORT_TRAJECTORY, &p);

    switch(p.data[0])
    {
      case COMMAND_RESET:
        ret = trajectoryReset();
        break;
      case COMMAND_ADD:
        ret = trajectoryAdd((struct data_add*)&p.data[1]);
        break;
      case COMMAND_START:
        ret = trajectoryStart();
        break;
      default:
        ret = ENOEXEC;
        break;
    }

    //answer
    p.data[1] = ret;
    p.size = 2;
    crtpSendPacket(&p);
  }
}

int trajectoryReset(void)
{
  numEntries = 0;

  return 0;
}

int trajectoryAdd(struct data_add* data)
{
  if (numEntries < MAX_TRAJECTORY_ENTRIES) {
    trajectory[numEntries].time_from_start = data->time_from_start;
    trajectory[numEntries].point.x = data->x;
    trajectory[numEntries].point.y = data->y;
    trajectory[numEntries].point.z = data->z;
    trajectory[numEntries].point.velocity_x = data->velocity_x;
    trajectory[numEntries].point.velocity_y = data->velocity_y;
    trajectory[numEntries].point.velocity_z = data->velocity_z;
    ++numEntries;
    return 0;
  }

  return ENOMEM;
}

int trajectoryStart(void)
{
  if (numEntries > 0) {
    lastValidTrajectoryEntry = trajectory[numEntries - 1];
  }

  startTime = xTaskGetTickCount();
  currentEntry = 0;

  return 0;
}