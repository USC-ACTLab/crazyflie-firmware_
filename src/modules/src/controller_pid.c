
#include "stabilizer.h"
#include "stabilizer_types.h"

#include "attitude_controller.h"
#include "sensfusion6.h"
#include "position_controller.h"
#include "trajectory.h"

#include "log.h"

#define ATTITUDE_RATE RATE_500_HZ
#define POSITION_RATE RATE_100_HZ

static attitude_t attitudeDesired;
static attitude_t rateDesired;
static float actuatorThrust;

void stateControllerInit(void)
{
  attitudeControllerInit();
}

bool stateControllerTest(void)
{
  bool pass = true;

  pass &= attitudeControllerTest();

  return pass;
}

void stateController(control_t *control, const sensorData_t *sensors,
                                         const state_t *state,
                                         const setpoint_t *setpoint,
                                         const uint32_t tick)
{
  if (RATE_DO_EXECUTE(ATTITUDE_RATE, tick)) {
    // Rate-controled YAW is moving YAW angle setpoint
    if (setpoint->mode.yaw == modeVelocity) {
       attitudeDesired.yaw -= setpoint->attitudeRate.yaw/500.0;
      while (attitudeDesired.yaw > 180.0)
        attitudeDesired.yaw -= 360.0;
      while (attitudeDesired.yaw < -180.0)
        attitudeDesired.yaw += 360.0;
    } else {
      attitudeDesired.yaw = setpoint->attitude.yaw;
    }
  }

  if (setpoint->enablePosCtrl) {
    if (RATE_DO_EXECUTE(POSITION_RATE, tick)) {
      positionController(&actuatorThrust, &attitudeDesired, state, setpoint);
    }
  }

  if (RATE_DO_EXECUTE(ATTITUDE_RATE, tick)) {
    // Switch between manual and automatic position control
    if (setpoint->mode.z == modeDisable) {
      actuatorThrust = setpoint->thrust;
    }
    if (setpoint->mode.x == modeDisable || setpoint->mode.y == modeDisable) {
      attitudeDesired.roll = setpoint->attitude.roll;
      attitudeDesired.pitch = setpoint->attitude.pitch;
    }

    attitudeControllerCorrectAttitudePID(state->attitude.roll, state->attitude.pitch, state->attitude.yaw,
                                attitudeDesired.roll, attitudeDesired.pitch, attitudeDesired.yaw,
                                &rateDesired.roll, &rateDesired.pitch, &rateDesired.yaw);

    if (setpoint->mode.roll == modeVelocity) {
      rateDesired.roll = setpoint->attitudeRate.roll;
    }
    if (setpoint->mode.pitch == modeVelocity) {
      rateDesired.pitch = setpoint->attitudeRate.pitch;
    }

    // TODO: Investigate possibility to subtract gyro drift.
    attitudeControllerCorrectRatePID(sensors->gyro.x, -sensors->gyro.y, sensors->gyro.z,
                             rateDesired.roll, rateDesired.pitch, rateDesired.yaw);

    attitudeControllerGetActuatorOutput(&control->roll,
                                        &control->pitch,
                                        &control->yaw);

    control->yaw = -control->yaw;
  }

  // Disable thrust compensation in all cases; this only works if the complementary state
  //  estimator is enabled.
  // if (!setpoint->enablePosCtrl) {
    // control->thrust = actuatorThrust / sensfusion6GetInvThrustCompensationForTilt();
  // } else {
    control->thrust = actuatorThrust;
  // }

  trajectoryState_t trajectoryState;
  trajectoryGetState(&trajectoryState);

  if (control->thrust == 0
      || ( setpoint->enablePosCtrl &&
         ( !sensors->valid
          || trajectoryState == TRAJECTORY_STATE_IDLE)))
  {
    control->thrust = 0;
    control->roll = 0;
    control->pitch = 0;
    control->yaw = 0;

    attitudeControllerResetAllPID();
    positionControllerReset();
    trajectorySetState(TRAJECTORY_STATE_IDLE);

    // Reset the calculated YAW angle for rate control
    attitudeDesired.yaw = state->attitude.yaw;
  }
}


LOG_GROUP_START(controller)
LOG_ADD(LOG_FLOAT, actuatorThrust, &actuatorThrust)
LOG_ADD(LOG_FLOAT, roll, &attitudeDesired.roll)
LOG_ADD(LOG_FLOAT, pitch, &attitudeDesired.pitch)
LOG_ADD(LOG_FLOAT, yaw, &attitudeDesired.yaw)
LOG_GROUP_STOP(controller)
