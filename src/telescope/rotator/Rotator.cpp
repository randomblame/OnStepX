//--------------------------------------------------------------------------------------------------
// telescope rotator control

#include "Rotator.h"

#ifdef ROTATOR_PRESENT

#include "../../lib/tasks/OnTask.h"

#include "../Telescope.h"
#include "../mount/Mount.h"
#include "../mount/site/Site.h"

void rotWrapper() { rotator.monitor(); }

// initialize rotator
void Rotator::init() {
  // wait a moment for any background processing that may be needed
  delay(1000);

  // confirm the data structure size
  if (RotatorSettingsSize < sizeof(RotatorSettings)) { nv.initError = true; DL("ERR: Rotator::init(), RotatorSettingsSize error"); }

  // get settings stored in NV ready
  if (!nv.hasValidKey()) {
    VLF("MSG: Rotator, writing defaults to NV");
    writeSettings();
  }
  readSettings();

  VLF("MSG: Rotator, init (Axis3)");
  if (!axis3.init(&motor3, pollAxis3)) { initError.driver = true; DLF("ERR: Axis3, no motion controller exiting!"); return; }
  axis3.resetPositionSteps(0);
  axis3.setBacklashSteps(settings.backlash);
  axis3.setFrequencyMax(AXIS3_SLEW_RATE_DESIRED);
  axis3.setFrequencyMin(0.01F);
  axis3.setFrequencySlew(AXIS3_SLEW_RATE_DESIRED);
  axis3.setSlewAccelerationTime(AXIS3_ACCELERATION_TIME);
  axis3.setSlewAccelerationTimeAbort(AXIS3_RAPID_STOP_TIME);
  if (AXIS3_POWER_DOWN == ON) axis3.setPowerDownTime(DEFAULT_POWER_DOWN_TIME);

  // start monitor task
  VF("MSG: Rotator, start derotation task (rate 1s priority 6)... ");
  if (tasks.add(1000, 0, true, 6, rotWrapper, "RotMon")) { VLF("success"); } else { VLF("FAILED!"); }

  unpark();
}

// get backlash in steps
int Rotator::getBacklash() {
  return settings.backlash;
}

// set backlash in steps
CommandError Rotator::setBacklash(int value) {
  if (value < 0 || value > 10000) return CE_PARAM_RANGE;
  if (settings.parkState >= PS_PARKED) return CE_PARKED;

  settings.backlash = value;
  writeSettings();
  axis3.setBacklashSteps(settings.backlash);

  return CE_NONE;
}

#ifdef MOUNT_PRESENT
  // returns parallactic angle in degrees
  double Rotator::parallacticAngle(Coordinate *coord) {
    return radToDeg(atan2(sin(coord->h), cos(coord->d)*tan(site.location.latitude) - sin(coord->d)*cos(coord->h)));
  }

  // returns parallactic rate in degrees per second
  double Rotator::parallacticRate(Coordinate *coord) {
    // one minute of hour angle in degrees = 15/60 = 0.25
    Coordinate ahead = *coord;
    ahead.h += degToRad(0.125);
    Coordinate behind = *coord;
    behind.h -= degToRad(0.125);
    double b = parallacticAngle(&behind);
    double a = parallacticAngle(&ahead);
    if (b >  90.0 && a < -90.0) a += 360.0;
    if (b < -90.0 && a >  90.0) b += 360.0;
    return (a - b)/60.0;
  }
#endif

// start slew in the specified direction
CommandError Rotator::slew(Direction dir) {
  if (settings.parkState >= PS_PARKED) return CE_PARKED;

  axis3.setFrequencyBase(0.0F);

  return axis3.autoSlew(dir, slewRate);
}

// move rotator to a specific location
CommandError Rotator::gotoTarget(float target) {
  if (settings.parkState >= PS_PARKED) return CE_PARKED;

  VF("MSG: Rotator, goto target coordinate set ("); V(target); VL("°)");
  VLF("MSG: Rotator, starting goto");

  axis3.setFrequencyBase(0.0F);
  axis3.setTargetCoordinate(target);

  return axis3.autoGoto(AXIS3_SLEW_RATE_DESIRED*AXIS3_ACCELERATION_TIME, AXIS3_SLEW_RATE_DESIRED);
}

// parks rotator at current position
CommandError Rotator::park() {
  if (settings.parkState == PS_PARKED)      return CE_NONE;
  if (settings.parkState == PS_PARKING)     return CE_PARK_FAILED;
  if (settings.parkState == PS_UNPARKING)   return CE_PARK_FAILED;
  if (settings.parkState == PS_PARK_FAILED) return CE_PARK_FAILED;

  derotatorEnabled = false;

  VLF("MSG: Rotator, parking");
  axis3.setBacklash(0.0F);
  settings.position = axis3.getInstrumentCoordinate();
  axis3.setTargetCoordinatePark(settings.position);

  CommandError e = axis3.autoGoto(AXIS3_SLEW_RATE_DESIRED*AXIS3_ACCELERATION_TIME, AXIS3_SLEW_RATE_DESIRED);

  if (e == CE_NONE) {
    settings.parkState = PS_PARKING;
    writeSettings();
  }

  return e;
}

// unparks rotator
CommandError Rotator::unpark() {
  if (settings.parkState == PS_PARKING)     return CE_PARK_FAILED;
  if (settings.parkState == PS_UNPARKING)   return CE_PARK_FAILED;
  if (settings.parkState == PS_PARK_FAILED) return CE_PARK_FAILED;

  // setting write delay to 0 disables on-the-fly position writes and forces strict parking
  if (ROTATOR_WRITE_DELAY == 0) {
    if (settings.parkState != PS_PARKED) return CE_NOT_PARKED;
  }

  axis3.enable(true);
  VF("MSG: Rotator, unpark position "); V(settings.position); VL("°");

  // simple unpark if we didn't actually park
  if (settings.parkState == PS_UNPARKED) {
    axis3.setInstrumentCoordinate(settings.position);
    writeSettings();
    return CE_NONE;
  }

  axis3.setBacklash(0.0F);
  axis3.setInstrumentCoordinatePark(settings.position);

  axis3.setBacklash(settings.backlash);
  axis3.setTargetCoordinate(settings.position);

  CommandError e = axis3.autoGoto(AXIS3_SLEW_RATE_DESIRED*AXIS3_ACCELERATION_TIME, AXIS3_SLEW_RATE_DESIRED);

  if (e == CE_NONE) {
    settings.parkState = PS_UNPARKING;
    writeSettings();
  }

  return e;
}

void Rotator::readSettings() {
  nv.readBytes(NV_ROTATOR_SETTINGS_BASE + RotatorSettingsSize, &settings, sizeof(RotatorSettings));

  if (settings.backlash < 0)     { settings.backlash = 0; initError.value = true; DLF("ERR: Rotator.init(), bad NV backlash < 0 steps (set to 0)"); }
  if (settings.backlash > 10000) { settings.backlash = 0; initError.value = true; DLF("ERR: Rotator.init(), bad NV backlash > 10000 steps (set to 0)"); }

  if (settings.position < AXIS3_LIMIT_MIN) { settings.position = 0.0F; initError.value = true; DLF("ERR: Rotator.init(), bad NV park pos < AXIS3_LIMIT_MIN (set to 0.0)"); }
  if (settings.position > AXIS3_LIMIT_MAX) { settings.position = 0.0F; initError.value = true; DLF("ERR: Rotator.init(), bad NV park pos > AXIS3_LIMIT_MAX (set to 0.0)"); }
}

void Rotator::writeSettings() {
  nv.updateBytes(NV_ROTATOR_SETTINGS_BASE + RotatorSettingsSize, &settings, sizeof(RotatorSettings));
}

// poll rotator to handle parking and derotation
void Rotator::monitor() {
  secs++;

  if (axis3.isSlewing() || settings.position == axis3.getInstrumentCoordinate()) writeTime = secs + ROTATOR_WRITE_DELAY;

  if (!axis3.isSlewing()) {

    if (settings.parkState == PS_PARKING) {
      if (axis3.atTarget()) {
        axis3.enable(false);
        settings.parkState = PS_PARKED;
        writeSettings();
        #if DEBUG == VERBOSE
          long offset = axis3.getInstrumentCoordinateSteps() - axis3.getMotorPositionSteps();
          VF("MSG: Rotator, park motor target   "); VL(axis3.getTargetCoordinateSteps() - offset);
          VF("MSG: Rotator, park motor position "); VL(axis3.getMotorPositionSteps());
        #endif
      }
    } else

    if (settings.parkState == PS_UNPARKING) {
      if (axis3.atTarget()) {
        settings.parkState = PS_UNPARKED;
        writeSettings();
      }
    } else

    if (settings.parkState == PS_UNPARKED) {
      #ifdef MOUNT_PRESENT
        if (derotatorEnabled && transform.mountType == ALTAZM) {
          float pr = 0.0F;
          if (!axis3.getSynchronized()) axis3.setSynchronized(true);
          Coordinate current = mount.getPosition();
          pr = parallacticRate(&current);
          if (derotatorReverse) pr = -pr;
          axis3.setSynchronized(true);
          axis3.setFrequencyBase(pr);
        }
      #endif

      // delayed write of focuser position
      if (ROTATOR_WRITE_DELAY != 0) {
        if (secs > writeTime) {
          settings.position = axis3.getInstrumentCoordinate();
          writeSettings();
          VF("MSG: Rotator, writing position ("); V(settings.position); VL("°) to NV"); 
        }
      }
    }
  }
}

Rotator rotator;

#endif
