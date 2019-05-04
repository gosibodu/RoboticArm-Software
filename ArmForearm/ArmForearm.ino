#include "ArmForearm.h"

void setup()
{
  //start our communications
  Serial.begin(9600);
  RoveComm.begin(RC_FOREARM_FOURTHOCTET);

  //initialize motors
  Wrist.LeftMotor.attach(WRIST_LEFT_INA, WRIST_LEFT_INB, WRIST_LEFT_PWM);
  Wrist.RightMotor.attach(WRIST_RIGHT_INA, WRIST_RIGHT_INB, WRIST_RIGHT_PWM);
  Gripper.attach(GRIPPER_INA, GRIPPER_INB, GRIPPER_PWM);
  //set motor speeds to 0, to be safe
  Wrist.LeftMotor.drive(0);
  Wrist.RightMotor.drive(0);

  Wrist.TiltEncoder.attach(WRIST_TILT_ENCODER);
  Wrist.TiltEncoder.start();
  Wrist.TwistEncoder.attach(WRIST_TWIST_ENCODER);
  Wrist.TwistEncoder.start();

  Watchdog.attach(stop);
  Watchdog.start(4000);

  //TODO: For testing maybe have some way of RED tweaking these constants?
  Wrist.TiltPid.attach( -1000.0, 1000.0, 48, 2, 0.5 ); //very much subject to change
  Wrist.TwistPid.attach( -1000.0, 1000.0, 48, 1, 0.1375 ); //very much subject to change

}

uint32_t timer = millis();

void loop()
{
  parsePackets();
  updatePosition();
  ClosedLoop();
}

void OpenLoop()
{
    if(abs(rovecomm_packet.data[0]) < 50 && abs(rovecomm_packet.data[1]) < 50 && abs(rovecomm_packet.data[3]) < 50)
    {
        //if we are getting zeros for everything we stop
        stop();
    }
    if(abs(rovecomm_packet.data[0]) >= 70 || abs(rovecomm_packet.data[1]) >= 70)
      Wrist.tiltTwistDecipercent((rovecomm_packet.data[0]), (rovecomm_packet.data[1]));

    Gripper.drive(rovecomm_packet.data[2]);
    Watchdog.clear();
}

void parsePackets()
{
   rovecomm_packet = RoveComm.read();
   switch(rovecomm_packet.data_id)
   {
    case RC_ARMBOARD_FOREARM_DATAID:
      DO_CLOSED_LOOP = false;
      OpenLoop();
      break;
    case RC_ARMBOARD_FOREARM_ANGLE_DATAID:
      DO_CLOSED_LOOP == true;
      tiltTarget = rovecomm_packet.data[0];
      twistTarget = rovecomm_packet.data[1];
      break;
    default:
      break;
   }
}

void updatePosition()
{
   jointAngles[0] = Wrist.TiltEncoder.readMillidegrees();
   jointAngles[1] = Wrist.TwistEncoder.readMillidegrees();
   
   if (timer > millis())
   {
    timer = millis();
   }

   if (millis() - timer > 100) 
   {
    timer = millis(); 
    RoveComm.writeTo(RC_ARMBOARD_FOREARM_MOTORANGLES_DATAID, 2, jointAngles, 192, 168, 1, RC_ARMBOARD_FOURTHOCTET, 11000);
   }
}

void ClosedLoop()
{
  if(DO_CLOSED_LOOP)
  {
    float outputs[2];
    updatePosition();
    moveToAngle(Wrist, tiltTarget, twistTarget, jointAngles, outputs);
    float tilt = outputs[0];
    float twist = outputs[1];

    if((tilt != 0 && twist!=0))
    { 
      Wrist.tiltTwistDecipercent(tilt, twist);
    }
    else if(tilt == 0 && twist == 0)
    {
      DO_CLOSED_LOOP = false;
      Wrist.LeftMotor.drive(0);
      Wrist.RightMotor.drive(0);
    }  

    Watchdog.clear();
  }

}

void moveToAngle(RoveDifferentialJoint &Joint, float tiltTo, float twistTo, uint32_t Angles[2], float outputs[2])
{
    float tilt;
    float twist;
    int smaller = 0;
    int larger =  0;
    int fakeTilt = 0;
    int fakeTiltAngle = 0;
    int fakeTwist = 0;
    int fakeTwistAngle = 0;
    ///MATH FOR J0
    //check if it's faster to go from 360->0 or 0->360 then the normal way
    smaller = min(tiltTo, Angles[0]);
    larger =  max(tiltTo, Angles[0]);
    //if wrapping around 360 is faster than going normally
    if((smaller+(360000-larger)) < abs(Angles[0]-tiltTo))
    {
      if(Angles[0]-(smaller+(360000-larger))<0)
      {
        fakeTilt  = tiltTo+(smaller+(360000-larger));
        fakeTiltAngle = fakeTilt+(tiltTo - Angles[0]);
      }
      else if(Angles[0]-(smaller+(360000-larger))>0)
      {
        fakeTilt  = tiltTo-(smaller+(360000-larger));
        fakeTiltAngle = fakeTilt-(tiltTo - Angles[0]);
      }
      tilt  = Joint.TiltPid.incrementPid(fakeTilt, fakeTiltAngle,250.00);
    }
    //if the normal way is faster, or equal we want less of a headache
    else if((smaller+(360000-larger)) >= abs(Angles[0]-tiltTo))
    {
       tilt  = -Joint.TiltPid.incrementPid(tiltTo, Angles[0],250.00);
    }

    ///MATH FOR J1
    //check if it's faster to go from 360->0 or 0->360 then the normal way
    smaller = min(twistTo, Angles[1]);
    larger =  max(twistTo, Angles[1]);
    //if wrapping around 360 is faster than going normally
    if((smaller+(360000-larger)) < abs(Angles[1]-twistTo))
    {
      if(Angles[1]-(smaller+(360000-larger))<0)
      {
        fakeTwist  = twistTo+(smaller+(360000-larger));
        fakeTwistAngle = fakeTwist+(twistTo - Angles[1]);
      }
      else if(Angles[0]-(smaller+(360000-larger))>0)
      {
        fakeTwist  = twistTo-(smaller+(360000-larger));
        fakeTwistAngle = fakeTwist-(twistTo - Angles[1]);
      }
      twist  = Joint.TwistPid.incrementPid(fakeTwist, fakeTwistAngle,250.00);
    }
    //if the normal way is faster, or equal we want less of a headache
    else if((smaller+(360000-larger)) >= abs(Angles[1]-twistTo))
    {
       twist  = -Joint.TwistPid.incrementPid(twistTo, Angles[1],250.00);
    }
    outputs[0] = tilt;
    outputs[1] = twist;
}

void stop()
{
  Wrist.LeftMotor.drive(0);
  Wrist.RightMotor.drive(0);
  Gripper.drive(0);
  Watchdog.clear();
}