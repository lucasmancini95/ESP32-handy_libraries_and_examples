/**********************************************************************************************
*Library adapted to C (and millis() funcion to FreeRTOS)
*by Lucas Mancini
*lucasmancini95@gmail.com
*
************************************************************************************************/

//Original Library:
/**********************************************************************************************
 * Arduino PID Library - Version 1.2.1
 * by Brett Beauregard <br3ttb@gmail.com> brettbeauregard.com
 *
 * This Library is licensed under the MIT License
 **********************************************************************************************/



//Standard libraries
#include <stdio.h>
#include <stdbool.h>
//ESP libraries
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/task.h"
#include "sdkconfig.h" // generated by "make menuconfig"

#include "PID_v1.h"

// function millis() adapted for freertos
//returns the amount of ms since the scheduler started
unsigned long millis(){
  return portTICK_PERIOD_MS*xTaskGetTickCount();
}

/*Constructor (...)*********************************************************
 *    The parameters specified here are those for for which we can't set up
 *    reliable defaults, so we need to have the user set them.
 ***************************************************************************/
void PID_constructor(PID_t* p_PID, double* Input, double* Output, double* Setpoint,
        double Kp, double Ki, double Kd, int POn, int ControllerDirection){
    p_PID->myOutput = Output;
    p_PID->myInput = Input;
    p_PID->mySetpoint = Setpoint;
    p_PID->inAuto = false;

    PID_SetOutputLimits(p_PID, 0, 255);				//default output limit corresponds to
												//the arduino pwm limits

    p_PID->SampleTime = 100;							//default Controller Sample Time is 0.1 seconds

    PID_SetControllerDirection(p_PID,ControllerDirection);
    PID_SetTunings(p_PID, Kp, Ki, Kd, POn);

    p_PID->lastTime =  millis()- p_PID->SampleTime;
}

/*Constructor (...)*********************************************************
 *    To allow backwards compatability for v1.1, or for people that just want
 *    to use Proportional on Error without explicitly saying so
 ***************************************************************************/

// void PID_constructor_simple(PID_t* p_PID, double* Input, double* Output, double* Setpoint,
//         double Kp, double Ki, double Kd, int ControllerDirection){
//
//     PID_constructor(p_PID, Input, Output, Setpoint, Kp, Ki, Kd, P_ON_E, ControllerDirection);
// {


/* Compute() **********************************************************************
 *     This, as they say, is where the magic happens.  this function should be called
 *   every time "void loop()" executes.  the function will decide for itself whether a new
 *   pid Output needs to be computed.  returns true when the output is computed,
 *   false when nothing has been done.
 **********************************************************************************/
bool PID_Compute(PID_t* p_PID){

   if(!(p_PID->inAuto)) return false;
   unsigned long now = millis();
   unsigned long timeChange = (now - p_PID->lastTime);

   if(timeChange >= p_PID->SampleTime)   {
      /*Compute all the working error variables*/
      double input = *(p_PID->myInput);
      double error = *(p_PID->mySetpoint) - input;
      double dInput = (input - p_PID->lastInput);
      p_PID->outputSum += (p_PID->ki * error);

      /*Add Proportional on Measurement, if P_ON_M is specified*/
      if(!(p_PID->pOnE)) p_PID->outputSum-= p_PID->kp * dInput;

      if(p_PID->outputSum > p_PID->outMax) p_PID->outputSum= p_PID->outMax;
      else if(p_PID->outputSum < p_PID->outMin) p_PID->outputSum= p_PID->outMin;

      /*Add Proportional on Error, if P_ON_E is specified*/
	   double output;
      if(p_PID->pOnE) output = p_PID->kp * error;
      else output = 0;

      /*Compute Rest of PID Output*/
      output += p_PID->outputSum - p_PID->kd * dInput;

	    if(output > p_PID->outMax) output = p_PID->outMax;
      else if(output < p_PID->outMin) output = p_PID->outMin;
	    *(p_PID->myOutput) = output;

      /*Remember some variables for next time*/
      p_PID->lastInput = input;
      p_PID->lastTime = now;
	    return true;
   }
   else return false;
}

/* SetTunings(...)*************************************************************
 * This function allows the controller's dynamic performance to be adjusted.
 * it's called automatically from the constructor, but tunings can also
 * be adjusted on the fly during normal operation
 ******************************************************************************/
void PID_SetTunings(PID_t* p_PID, double Kp, double Ki, double Kd, int POn){
   if (Kp<0 || Ki<0 || Kd<0) return;

   p_PID->pOn = POn;
   p_PID->pOnE = POn == P_ON_E;

   p_PID->dispKp = Kp; p_PID->dispKi = Ki; p_PID->dispKd = Kd;

   double SampleTimeInSec = ((double)p_PID->SampleTime)/1000;
   p_PID->kp = Kp;
   p_PID->ki = Ki * SampleTimeInSec;
   p_PID->kd = Kd / SampleTimeInSec;

  if(p_PID->controllerDirection ==REVERSE)
   {
      p_PID->kp = (0 - p_PID->kp);
      p_PID->ki = (0 - p_PID->ki);
      p_PID->kd = (0 - p_PID->kd);
   }
}

/* SetTunings(...)*************************************************************
 * Set Tunings using the last-rembered POn setting
 ******************************************************************************/
void PID_SetTunings_simple(PID_t* p_PID,double Kp, double Ki, double Kd){
    PID_SetTunings(p_PID,Kp, Ki, Kd, p_PID->pOn);
}

/* SetSampleTime(...) *********************************************************
 * sets the period, in Milliseconds, at which the calculation is performed
 ******************************************************************************/
void PID_SetSampleTime(PID_t* p_PID,int NewSampleTime){
   if (NewSampleTime > 0)
   {
      double ratio  = (double)NewSampleTime
                      / (double)p_PID->SampleTime;
      p_PID->ki *= ratio;
      p_PID->kd /= ratio;
      p_PID->SampleTime = (unsigned long)NewSampleTime;
   }
}

/* SetOutputLimits(...)****************************************************
 *     This function will be used far more often than SetInputLimits.  while
 *  the input to the controller will generally be in the 0-1023 range (which is
 *  the default already,)  the output will be a little different.  maybe they'll
 *  be doing a time window and will need 0-8000 or something.  or maybe they'll
 *  want to clamp it from 0-125.  who knows.  at any rate, that can all be done
 *  here.
 **************************************************************************/
void PID_SetOutputLimits(PID_t* p_PID, double Min, double Max){
   if(Min >= Max) return;
   p_PID->outMin = Min;
   p_PID->outMax = Max;

   if(p_PID->inAuto)
   {
	   if(*(p_PID->myOutput) > p_PID->outMax) *(p_PID->myOutput) = p_PID->outMax;
	   else if(*(p_PID->myOutput) < p_PID->outMin) *(p_PID->myOutput) = p_PID->outMin;

	   if(p_PID->outputSum > p_PID->outMax) p_PID->outputSum= p_PID->outMax;
	   else if(p_PID->outputSum < p_PID->outMin) p_PID->outputSum= p_PID->outMin;
   }
}

/* SetMode(...)****************************************************************
 * Allows the controller Mode to be set to manual (0) or Automatic (non-zero)
 * when the transition from manual to auto occurs, the controller is
 * automatically initialized
 ******************************************************************************/
void PID_SetMode(PID_t* p_PID, int Mode)
{
    bool newAuto = (Mode == AUTOMATIC);
    if(newAuto && !(p_PID->inAuto))
    {  /*we just went from manual to auto*/
        PID_Initialize(p_PID);
    }
    p_PID->inAuto = newAuto;
}

/* Initialize()****************************************************************
 *	does all the things that need to happen to ensure a bumpless transfer
 *  from manual to automatic mode.
 ******************************************************************************/
void PID_Initialize(PID_t* p_PID){
   p_PID->outputSum = *(p_PID->myOutput);
   p_PID->lastInput = *(p_PID->myInput);
   if(p_PID->outputSum > p_PID->outMax) p_PID->outputSum = p_PID->outMax;
   else if(p_PID->outputSum < p_PID->outMin) p_PID->outputSum = p_PID->outMin;
}

/* SetControllerDirection(...)*************************************************
 * The PID will either be connected to a DIRECT acting process (+Output leads
 * to +Input) or a REVERSE acting process(+Output leads to -Input.)  we need to
 * know which one, because otherwise we may increase the output when we should
 * be decreasing.  This is called from the constructor.
 ******************************************************************************/
void PID_SetControllerDirection(PID_t* p_PID,int Direction){
   if(p_PID->inAuto && Direction != p_PID->controllerDirection)
   {
	    p_PID->kp = (0 - p_PID->kp);
      p_PID->ki = (0 - p_PID->ki);
      p_PID->kd = (0 - p_PID->kd);
   }
   p_PID->controllerDirection = Direction;
}

/* Status Funcions*************************************************************
 * Just because you set the Kp=-1 doesn't mean it actually happened.  these
 * functions query the internal state of the PID.  they're here for display
 * purposes.  this are the functions the PID Front-end uses for example
 ******************************************************************************/
double PID_GetKp(PID_t* p_PID){
   return p_PID->dispKp;
 }

double PID_GetKi(PID_t* p_PID){
   return p_PID->dispKi;
 }

double PID_GetKd(PID_t* p_PID){
   return p_PID->dispKd;
 }

int PID_GetMode(PID_t* p_PID){
  return ((p_PID->inAuto) ? AUTOMATIC : MANUAL);
}

int PID_GetDirection(PID_t* p_PID){
   return (p_PID->controllerDirection);
 }
