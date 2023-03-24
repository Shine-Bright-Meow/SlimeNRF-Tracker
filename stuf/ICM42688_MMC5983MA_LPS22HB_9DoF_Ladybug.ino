#include "ICM42688.h"
#include "MMC5983MA.h"

// global constants for 9 DoF fusion and AHRS (Attitude and Heading Reference System)
#define pi 3.141592653589793238462643383279502884f
#define GyroMeasError pi * (40.0f / 180.0f)     // gyroscope measurement error in rads/s (start at 40 deg/s)
#define GyroMeasDrift pi * (0.0f / 180.0f)      // gyroscope measurement drift in rad/s/s (start at 0.0 deg/s/s)
#define beta sqrtf(3.0f / 4.0f) * GyroMeasError // compute beta
#define zeta sqrtf(3.0f / 4.0f) * GyroMeasDrift // compute zeta, the other free parameter in the Madgwick scheme usually set to a small or zero value
uint32_t delt_t = 0;                            // used to control display output rate
uint32_t sumCount = 0;                          // used to control display output rate
float pitch, yaw, roll;                         // absolute orientation
float a12, a22, a31, a32, a33;                  // rotation matrix coefficients for Euler angles and gravity components
float deltat = 0.0f, sum = 0.0f;                // integration interval for both filter schemes
uint32_t lastUpdate = 0, firstUpdate = 0;       // used to calculate integration interval
uint32_t Now = 0;                               // used to calculate integration interval
float lin_ax, lin_ay, lin_az;                   // linear acceleration (acceleration with gravity component subtracted)
float q[4] = {1.0f, 0.0f, 0.0f, 0.0f};          // vector to hold quaternion
float eInt[3] = {0.0f, 0.0f, 0.0f};             // vector to hold integral error for Mahony method

// ICM42688 definitions
#define ICM42688_intPin1 8 // interrupt1 pin definitions, data ready
#define ICM42688_intPin2 9 // interrupt2 pin definitions, clock in

/* Specify sensor parameters (sample rate is twice the bandwidth)
 * choices are:
      AFS_2G, AFS_4G, AFS_8G, AFS_16G
      GFS_15_625DPS, GFS_31_25DPS, GFS_62_5DPS, GFS_125DPS, GFS_250DPS, GFS_500DPS, GFS_1000DPS, GFS_2000DPS
      AODR_1_5625Hz, AODR_3_125Hz, AODR_6_25Hz, AODR_50AODR_12_5Hz, AODR_25Hz, AODR_50Hz, AODR_100Hz, AODR_200Hz, AODR_500Hz,
      AODR_1kHz, AODR_2kHz, AODR_4kHz, AODR_8kHz, AODR_16kHz, AODR_32kHz
      GODR_12_5Hz, GODR_25Hz, GODR_50Hz, GODR_100Hz, GODR_200Hz, GODR_500Hz, GODR_1kHz, GODR_2kHz, GODR_4kHz, GODR_8kHz, GODR_16kHz, GODR_32kHz
*/
uint8_t Ascale = AFS_2G, Gscale = GFS_250DPS, AODR = AODR_200Hz, GODR = GODR_200Hz, aMode = aMode_LN, gMode = gMode_LN;

float aRes, gRes;                                                          // scale resolutions per LSB for the accel and gyro sensor2
float accelBias[3] = {0.0f, 0.0f, 0.0f}, gyroBias[3] = {0.0f, 0.0f, 0.0f}; // offset biases for the accel and gyro
int16_t accelDiff[3] = {0, 0, 0}, gyroDiff[3] = {0, 0, 0};                 // difference betwee ST and normal values
float STratio[7] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};             // self-test results for the accel and gyro
int16_t ICM42688Data[7];                                                   // Stores the 16-bit signed sensor output
float Gtemperature;                                                        // Stores the real internal gyro temperature in degrees Celsius
float ax, ay, az, gx, gy, gz;                                              // variables to hold latest accel/gyro data values

bool newICM42688Data = false;

// MMC5983MA definitions
#define MMC5983MA_intPin 5 // interrupt for magnetometer data ready

/* Specify sensor parameters (continuous mode sample rate is dependent on bandwidth)
 * choices are: MODR_ONESHOT, MODR_1Hz, MODR_10Hz, MODR_20Hz, MODR_50 Hz, MODR_100Hz, MODR_200Hz (BW = 0x01), MODR_1000Hz (BW = 0x03)
 * Bandwidth choices are: MBW_100Hz, MBW_200Hz, MBW_400Hz, MBW_800Hz
 * Set/Reset choices are: MSET_1, MSET_25, MSET_75, MSET_100, MSET_250, MSET_500, MSET_1000, MSET_2000, so MSET_100 set/reset occurs every 100th measurement, etc.
 */
uint8_t MODR = MODR_100Hz, MBW = MBW_100Hz, MSET = MSET_2000;

float mRes = 1.0f / 16384.0f;                                              // mag sensitivity if using 18 bit data
float magBias[3] = {0, 0, 0}, magScale[3] = {1, 1, 1}, magOffset[3] = {0}; // Bias corrections for magnetometer
uint32_t MMC5983MAData[3];                                                 // Stores the 18-bit unsigned magnetometer sensor output
uint8_t MMC5983MAtemperature;                                              // Stores the magnetometer temperature register data
float Mtemperature;                                                        // Stores the real internal chip temperature in degrees Celsius
float mx, my, mz;                                                          // variables to hold latest mag data values
uint8_t MMC5983MAstatus;
float MMC5983MA_offset = 131072.0f;

bool newMMC5983MAData = false;

// Implementation of Sebastian Madgwick's "...efficient orientation filter for... inertial/magnetic sensor arrays"
// (see http://www.x-io.co.uk/category/open-source/ for examples and more details)
// which fuses acceleration, rotation rate, and magnetic moments to produce a quaternion-based estimate of absolute
// device orientation -- which can be converted to yaw, pitch, and roll. Useful for stabilizing quadcopters, etc.
// The performance of the orientation filter is at least as good as conventional Kalman-based filtering algorithms
// but is much less computationally intensive---it can be performed on a 3.3 V Pro Mini operating at 8 MHz!
__attribute__((optimize("O3"))) void MadgwickQuaternionUpdate(float ax, float ay, float az, float gx, float gy, float gz, float mx, float my, float mz)
{
  float q1 = q[0], q2 = q[1], q3 = q[2], q4 = q[3]; // short name local variable for readability
  float norm;
  float hx, hy, _2bx, _2bz;
  float s1, s2, s3, s4;
  float qDot1, qDot2, qDot3, qDot4;

  // Auxiliary variables to avoid repeated arithmetic
  float _2q1mx;
  float _2q1my;
  float _2q1mz;
  float _2q2mx;
  float _4bx;
  float _4bz;
  float _2q1 = 2.0f * q1;
  float _2q2 = 2.0f * q2;
  float _2q3 = 2.0f * q3;
  float _2q4 = 2.0f * q4;
  float _2q1q3 = 2.0f * q1 * q3;
  float _2q3q4 = 2.0f * q3 * q4;
  float q1q1 = q1 * q1;
  float q1q2 = q1 * q2;
  float q1q3 = q1 * q3;
  float q1q4 = q1 * q4;
  float q2q2 = q2 * q2;
  float q2q3 = q2 * q3;
  float q2q4 = q2 * q4;
  float q3q3 = q3 * q3;
  float q3q4 = q3 * q4;
  float q4q4 = q4 * q4;

  // Normalise accelerometer measurement
  norm = sqrtf(ax * ax + ay * ay + az * az);
  if (norm == 0.0f)
    return; // handle NaN
  norm = 1.0f / norm;
  ax *= norm;
  ay *= norm;
  az *= norm;

  // Normalise magnetometer measurement
  norm = sqrtf(mx * mx + my * my + mz * mz);
  if (norm == 0.0f)
    return; // handle NaN
  norm = 1.0f / norm;
  mx *= norm;
  my *= norm;
  mz *= norm;

  // Reference direction of Earth's magnetic field
  _2q1mx = 2.0f * q1 * mx;
  _2q1my = 2.0f * q1 * my;
  _2q1mz = 2.0f * q1 * mz;
  _2q2mx = 2.0f * q2 * mx;
  hx = mx * q1q1 - _2q1my * q4 + _2q1mz * q3 + mx * q2q2 + _2q2 * my * q3 + _2q2 * mz * q4 - mx * q3q3 - mx * q4q4;
  hy = _2q1mx * q4 + my * q1q1 - _2q1mz * q2 + _2q2mx * q3 - my * q2q2 + my * q3q3 + _2q3 * mz * q4 - my * q4q4;
  _2bx = sqrtf(hx * hx + hy * hy);
  _2bz = -_2q1mx * q3 + _2q1my * q2 + mz * q1q1 + _2q2mx * q4 - mz * q2q2 + _2q3 * my * q4 - mz * q3q3 + mz * q4q4;
  _4bx = 2.0f * _2bx;
  _4bz = 2.0f * _2bz;

  // Gradient decent algorithm corrective step
  s1 = -_2q3 * (2.0f * q2q4 - _2q1q3 - ax) + _2q2 * (2.0f * q1q2 + _2q3q4 - ay) - _2bz * q3 * (_2bx * (0.5f - q3q3 - q4q4) + _2bz * (q2q4 - q1q3) - mx) + (-_2bx * q4 + _2bz * q2) * (_2bx * (q2q3 - q1q4) + _2bz * (q1q2 + q3q4) - my) + _2bx * q3 * (_2bx * (q1q3 + q2q4) + _2bz * (0.5f - q2q2 - q3q3) - mz);
  s2 = _2q4 * (2.0f * q2q4 - _2q1q3 - ax) + _2q1 * (2.0f * q1q2 + _2q3q4 - ay) - 4.0f * q2 * (1.0f - 2.0f * q2q2 - 2.0f * q3q3 - az) + _2bz * q4 * (_2bx * (0.5f - q3q3 - q4q4) + _2bz * (q2q4 - q1q3) - mx) + (_2bx * q3 + _2bz * q1) * (_2bx * (q2q3 - q1q4) + _2bz * (q1q2 + q3q4) - my) + (_2bx * q4 - _4bz * q2) * (_2bx * (q1q3 + q2q4) + _2bz * (0.5f - q2q2 - q3q3) - mz);
  s3 = -_2q1 * (2.0f * q2q4 - _2q1q3 - ax) + _2q4 * (2.0f * q1q2 + _2q3q4 - ay) - 4.0f * q3 * (1.0f - 2.0f * q2q2 - 2.0f * q3q3 - az) + (-_4bx * q3 - _2bz * q1) * (_2bx * (0.5f - q3q3 - q4q4) + _2bz * (q2q4 - q1q3) - mx) + (_2bx * q2 + _2bz * q4) * (_2bx * (q2q3 - q1q4) + _2bz * (q1q2 + q3q4) - my) + (_2bx * q1 - _4bz * q3) * (_2bx * (q1q3 + q2q4) + _2bz * (0.5f - q2q2 - q3q3) - mz);
  s4 = _2q2 * (2.0f * q2q4 - _2q1q3 - ax) + _2q3 * (2.0f * q1q2 + _2q3q4 - ay) + (-_4bx * q4 + _2bz * q2) * (_2bx * (0.5f - q3q3 - q4q4) + _2bz * (q2q4 - q1q3) - mx) + (-_2bx * q1 + _2bz * q3) * (_2bx * (q2q3 - q1q4) + _2bz * (q1q2 + q3q4) - my) + _2bx * q2 * (_2bx * (q1q3 + q2q4) + _2bz * (0.5f - q2q2 - q3q3) - mz);
  norm = sqrtf(s1 * s1 + s2 * s2 + s3 * s3 + s4 * s4); // normalise step magnitude
  norm = 1.0f / norm;
  s1 *= norm;
  s2 *= norm;
  s3 *= norm;
  s4 *= norm;

  // Compute rate of change of quaternion
  qDot1 = 0.5f * (-q2 * gx - q3 * gy - q4 * gz) - beta * s1;
  qDot2 = 0.5f * (q1 * gx + q3 * gz - q4 * gy) - beta * s2;
  qDot3 = 0.5f * (q1 * gy - q2 * gz + q4 * gx) - beta * s3;
  qDot4 = 0.5f * (q1 * gz + q2 * gy - q3 * gx) - beta * s4;

  // Integrate to yield quaternion
  q1 += qDot1 * deltat;
  q2 += qDot2 * deltat;
  q3 += qDot3 * deltat;
  q4 += qDot4 * deltat;
  norm = sqrtf(q1 * q1 + q2 * q2 + q3 * q3 + q4 * q4); // normalise quaternion
  norm = 1.0f / norm;
  q[0] = q1 * norm;
  q[1] = q2 * norm;
  q[2] = q3 * norm;
  q[3] = q4 * norm;
}

void setup()
{
  delay(100);

  // Read the ICM42688 Chip ID register, this is a good test of communication
  byte ICM42688ID = ICM42688.getChipID(); // Read CHIP_ID register for ICM42688
  delay(1000);

  // Read the MMC5983MA Chip ID register, this is a good test of communication
  byte MMC5983ID = MMC5983MA.getChipID(); // Read CHIP_ID register for MMC5983MA
  delay(1000);

  if (ICM42688ID == 0x47 && MMC5983ID == 0x30) // check if all I2C sensors have acknowledged
  {
    ICM42688.reset(); // software reset ICM42688 to default registers

    // set sensor resolutions for self test
    aRes = 4.0f / 32768.0f;
    gRes = 250.0f / 32768.0f;

    ICM42688.selfTest(accelDiff, gyroDiff, STratio);
    delay(2000);

    // get sensor resolutions for user settings, only need to do this once
    aRes = ICM42688.getAres(Ascale);
    gRes = ICM42688.getGres(Gscale);

    ICM42688.init(Ascale, Gscale, AODR, GODR, aMode, gMode, false); // configure for basic accel/gyro data output
    delay(4000);

    ICM42688.offsetBias(accelBias, gyroBias);
    delay(1000);

    //   MMC5983MA.powerDown(); // if don't need the mag for ICM42688 APEX demo
    MMC5983MA.selfTest();
    MMC5983MA.getOffset(magOffset);

    MMC5983MA.reset(); // software reset MMC5983MA to default registers

    MMC5983MA.SET(); // "deGauss" magnetometer
    MMC5983MA.init(MODR, MBW, MSET);
    MMC5983MA.offsetBias(magBias, magScale);
    delay(2000); // add delay to see results before serial spew of data
  }

  MMC5983MA.clearInt(); //  clear MMC5983MA interrupts before main loop
  ICM42688.DRStatus();  // clear ICM42688 data ready interrupt
}
/* End of setup */

void loop()
{
  /*  Strategy here is to have all operations in the main loop be interrupt driven to allow the MCU to sleep when
   *  not servicing an interrupt. The barometer is read once per second or whenever the RTC alarm fires.
   */

  // If intPin goes high, ICM42688 data registers have new data
  if (newICM42688Data == true)
  {                          // On interrupt, read data
    newICM42688Data = false; // reset newData flag

    //   ICM42688.DRStatus(); // clear data ready interrupt if using latched interrupt

    ICM42688.readData(ICM42688Data); // INT1 cleared on any read

    // Now we'll calculate the accleration value into actual g's
    ax = (float)ICM42688Data[1] * aRes - accelBias[0]; // get actual g value, this depends on scale being set
    ay = (float)ICM42688Data[2] * aRes - accelBias[1];
    az = (float)ICM42688Data[3] * aRes - accelBias[2];

    // Calculate the gyro value into actual degrees per second
    gx = (float)ICM42688Data[4] * gRes - gyroBias[0]; // get actual gyro value, this depends on scale being set
    gy = (float)ICM42688Data[5] * gRes - gyroBias[1];
    gz = (float)ICM42688Data[6] * gRes - gyroBias[2];

    for (uint8_t i = 0; i < 20; i++)
    { // iterate a fixed number of times per data read cycle
      Now = micros();
      deltat = ((Now - lastUpdate) / 1000000.0f); // set integration time by time elapsed since last filter update
      lastUpdate = Now;

      sum += deltat; // sum for averaging filter update rate
      sumCount++;

      MadgwickQuaternionUpdate(ay, ax, -az, gy * pi / 180.0f, gx * pi / 180.0f, -gz * pi / 180.0f, mx, -my, -mz);
    }
  }

  // If intPin goes high, MMC5983MA magnetometer has new data
  if (newMMC5983MAData == true)
  {                           // On interrupt, read data
    newMMC5983MAData = false; // reset newData flag

    MMC5983MAstatus = MMC5983MA.status();
    if (MMC5983MAstatus & 0x01) // if mag measurement is done
    {
      MMC5983MA.clearInt(); //  clear MMC5983MA interrupts
      MMC5983MA.readData(MMC5983MAData);

      // Now we'll calculate the magnetometer value into actual Gauss
      mx = ((float)MMC5983MAData[0] - MMC5983MA_offset) * mRes - magBias[0]; // get actual mag value
      my = ((float)MMC5983MAData[1] - MMC5983MA_offset) * mRes - magBias[1];
      mz = ((float)MMC5983MAData[2] - MMC5983MA_offset) * mRes - magBias[2];
      mx *= magScale[0];
      my *= magScale[1];
      mz *= magScale[2];
    }
  }
  // end sensor interrupt handling

  a12 = 2.0f * (q[1] * q[2] + q[0] * q[3]);
  a22 = q[0] * q[0] + q[1] * q[1] - q[2] * q[2] - q[3] * q[3];
  a31 = 2.0f * (q[0] * q[1] + q[2] * q[3]);
  a32 = 2.0f * (q[1] * q[3] - q[0] * q[2]);
  a33 = q[0] * q[0] - q[1] * q[1] - q[2] * q[2] + q[3] * q[3];
  pitch = -asinf(a32);
  roll = atan2f(a31, a33);
  yaw = atan2f(a12, a22);
  pitch *= 180.0f / pi;
  yaw *= 180.0f / pi;
  // yaw += 13.8f; // Declination at Danville, California is 13 degrees 48 minutes and 47 seconds on 2014-04-04
  if (yaw < 0)
    yaw += 360.0f; // Ensure yaw stays between 0 and 360
  roll *= 180.0f / pi;
  lin_ax = ax + a31;
  lin_ay = ay + a32;
  lin_az = az - a33;

  sumCount = 0;
  sum = 0;
} /*  End of main loop */
