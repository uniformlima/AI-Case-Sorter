#include <Wire.h>
#include <SoftwareSerial.h>



//ARDUINO UNO WITH 4 MOTOR CONTROLLER
//Stepper controller is set to 16 Microsteps (3 jumpers in place)
#define FEED_DIRPIN 5
#define FEED_STEPPIN 2  
#define FEED_Enable 8
#define FEED_MICROSTEPS 16
#define FEED_HOMING_SENSOR 10
#define HOMING_SENSOR_POWER 9

#define SORT_MICROSTEPS 16 
#define SORT_DIRPIN 6
#define SORT_STEPPIN 3
#define SORT_Enable 8

#define SORTER_CHUTE_SEPERATION 20 //number of steps between chutes

//not used but could be if you wanted to specify exact positions. 
//referenced in the commented out code in the runsorter method below
int sorterChutes[] ={0,17, 33, 49, 66, 83, 99, 116, 132}; 

//if your sorter design has a queue, you will set this value to your queue length. 
//example of a queue would be like a rotary wheel where the currently recognized brass isn't going to fall into sorter for 3 more positions
#define QUEUE_LENGTH 2 //usually 1 but it is the positional distance between your camera and the sorter.
#define PRINT_QUEUEINFO false  //used for debugging in serial monitor. prints queue info
int sorterQueue[QUEUE_LENGTH];


bool autoHoming = true; //if true, then homing will be checked and adjusted on each feed cycle. Requires homing sensor.

//inputs which can be set via serial console like:  feedspeed:50 or sortspeed:60
int feedSpeed = 30; //range: 1..100
int feedSteps= 70; //range 1..1000 

int sortSpeed = 10; //range: 1..100
int sortSteps = 20; //range: 1..500 //20 default
int feedPauseTime = 0; //range: 1.2000 //if you feed has two parts (back, forth), this is the pause time between the two parts. 

//used to calculate motor speeds and works in conjunction with microsteps.  
//480 for 8 microsteps, 240 for 16, 120 for 32, 
int motorPulseDelay = 240;

//FEED STEP OVERRIDES
//These settings override the feedSteps&b and oddFeed settings above.
//If feedSteps & B are too coarse, you can directly calculate the microsteps needed between feed positions and use that here.
int feedMicroSteps = 0; // how many microsteps between feeds - 0 to disable.
int feedFactionalStep = 1; //this allows you to add a micro step every [fractionInterval]. 
int feedFractionInterval = 3; //the interval at which micro steps get added. 
//END FEED STEP OVERRIDES

//tracking variables
int sorterMotorCurrentPosition = 0;
bool isHomed=true;
int homingOffset = 0;

void setup() {
  
  Serial.begin(9600);
  
  pinMode(FEED_Enable, OUTPUT);
  pinMode(SORT_Enable, OUTPUT);
  pinMode(FEED_DIRPIN, OUTPUT);
  pinMode(FEED_STEPPIN, OUTPUT);
  pinMode(SORT_DIRPIN, OUTPUT);
  pinMode(SORT_STEPPIN, OUTPUT);
  
  pinMode(FEED_HOMING_SENSOR, INPUT); 
  pinMode(HOMING_SENSOR_POWER, OUTPUT);
  digitalWrite(HOMING_SENSOR_POWER, HIGH);
  
  digitalWrite(FEED_Enable, HIGH);
  digitalWrite(SORT_Enable, HIGH);
  digitalWrite(FEED_DIRPIN, HIGH);
 
}

void loop() {
 digitalWrite(HOMING_SENSOR_POWER, HIGH);

    if(Serial.available() > 0 )  
    {
      
      String input = Serial.readStringUntil('\n');
      
      //normal input would just be the tray # you want to sort to which would be something like "1\n" or "9\n" (\n)if you are using serial console, the \n is already included in the writeline
      //so just the number 1-10 would suffice. 
      //the software on the other end should be listening for  "done\n". 

      //if true was returned, we go to next looping, else we continue down below
      if(parseSerialInput(input) == true){
        return;
      }


      int sortPosition = input.toInt();
      QueueAdd(sortPosition);
      runSorterMotor(QueueFetch());
      runFeedMotorManual();
     
      checkHoming(true);
      delay(100);//allow for vibrations to calm down for clear picture
      Serial.print("done\n");
      PrintQueue();
    }

}

//polls the homing sensor for about 10 seconds
void testHomingSensor(){
  for(int i=0; i < 500;i++){

    int value=digitalRead(FEED_HOMING_SENSOR);
    Serial.print(value);
    Serial.print("\n");
   
    delay(50);
    }
  
  }
void checkHoming(bool autoHome){

    if(autoHome ==true && autoHoming==false)
      return;

   int homingSensorVal = digitalRead(FEED_HOMING_SENSOR);
   // Serial.print(homingSensorVal);
   if(homingSensorVal ==1){
    return; //we are homed! Continue
   }

   int i=0; //safety valve..
   int offset = homingOffset * FEED_MICROSTEPS;
   while((homingSensorVal == 0 && i<12000) || offset >0){
      runFeedMotor(1);
      //delay(2);
      homingSensorVal = digitalRead(FEED_HOMING_SENSOR);
      i++;
      if(homingSensorVal==1){
        offset--;
       // delay(10);
        
      }
   }
  
}

//moves the sorter arm. Blocking operation until complete
void runSorterMotor(int chute){

   //you can uncomment to use the array sorterChutes for exact positions of each slot
   //int newStepsPos = sorterChutes[chute]; //the steps position from zero of the "slot"

   //rather than use exact positions, we are assuming uniform seperation betweeen slots specified by sorter_chute_seperation constant
   int newStepsPos = chute * SORTER_CHUTE_SEPERATION;

   //calculate the amount of movement and move.
   int nextmovement = newStepsPos - sorterMotorCurrentPosition; //the number of +-steps between current position and next position
   int movement = nextmovement * SORT_MICROSTEPS; //calculate the number of microsteps required
   
   runSortMotorManual(movement);
   sorterMotorCurrentPosition = newStepsPos;
}

int fractionIntervalIndex =0;

void runFeedMotorManual(){
  int steps=0;

  //if the feedMicroSteps override is used, we do that and return.
  if(feedMicroSteps>0){
    steps = feedMicroSteps;
    if(fractionIntervalIndex == feedFractionInterval){
      steps= steps + feedFactionalStep;
      fractionIntervalIndex=0;
    }else{
       fractionIntervalIndex++;
    }  
    runFeedMotor(steps);
    return;
  }

   digitalWrite(FEED_DIRPIN, LOW);

  int curFeedSteps = abs(feedSteps);

   //calculate the steps based on microsteps. 
  steps = curFeedSteps * FEED_MICROSTEPS;
 // Serial.print(steps);
  int delayTime = motorPulseDelay - feedSpeed; //assuming a feedspeed variable between 0 and 100. a delay of less than 20ms is too fast so 20mcs should be minimum delay for fastest speed.
  
  for(int i=0;i<steps;i++){
      digitalWrite(FEED_STEPPIN, HIGH);
      delayMicroseconds(60);   //pulse. i have found 60 to be very consistent with tb6600. Noticed that faster pulses tend to drop steps. 
      digitalWrite(FEED_STEPPIN, LOW);
      delayMicroseconds(delayTime); //speed 156 = 1 second per revolution
  }
 
}

void runFeedMotor(int steps){

  digitalWrite(FEED_DIRPIN, LOW);
 
  int delayTime = motorPulseDelay - feedSpeed; //assuming a feedspeed variable between 0 and 100. a delay of less than 20ms is too fast so 20mcs should be minimum delay for fastest speed.
  
  for(int i=0;i<steps;i++){
      digitalWrite(FEED_STEPPIN, HIGH);
      delayMicroseconds(60);   //pulse. i have found 60 to be very consistent with tb6600. Noticed that faster pulses tend to drop steps. 
      digitalWrite(FEED_STEPPIN, LOW);
      delayMicroseconds(delayTime); //speed 156 = 1 second per revolution
  }
  
 
}



void runSortMotorManual(int steps){
  // Serial.print(steps);
  int delayTime = motorPulseDelay - sortSpeed;
     
  if(steps>0){
    digitalWrite(SORT_DIRPIN, LOW);
  }else{
    digitalWrite(SORT_DIRPIN, HIGH);
  }
  steps = abs(steps) ;
  for(int i=0;i<steps;i++){
      digitalWrite(SORT_STEPPIN, HIGH);
      delayMicroseconds(60);   //pulse //def 60
      digitalWrite(SORT_STEPPIN, LOW);
      delayMicroseconds(delayTime); //speed 156 = 1 second per revolution //def 20
  }

}


//used for debugging queue issues. 
void PrintQueue(){
    if(PRINT_QUEUEINFO == false)
      return;
    Serial.print("-------\n");
    Serial.print("QueueStatus:\n");
    for (int i=0;i < QUEUE_LENGTH;i++){
      if(i>0)
        Serial.print(',');
      Serial.print(sorterQueue[i]);
    }
    Serial.print("\nSorting to: ");
    Serial.print(QueueFetch());
    Serial.print("\n-------\n");
}

//adds a position item into the front of the sorting queue. Queue is used in FIFO mode. PUSH
void QueueAdd(int pos){
  for(int i = QUEUE_LENGTH;i>0;i--){
    sorterQueue[i] = sorterQueue[i-1];
  }
  sorterQueue[0]=pos;
}

//fetches items from back of queue. POP
int QueueFetch(){
  return sorterQueue[QUEUE_LENGTH -1];
}

//return true if you wish to stop processing and continue to next loop
bool parseSerialInput(String input)
{
      //set feed speed. Values 1-100. Def 60
      if(input.startsWith("feedspeed:")){
         input.replace("feedspeed:","");
         feedSpeed= input.toInt();
          Serial.print("ok\n");
         return true;
      }
      
      //used to test tracking on steppers for feed motor. specify the number of feeds to perform: eg:  "test:60" will feed 60 times. 
      if(input.startsWith("test:")){
         input.replace("test:","");
         int testcount = input.toInt();
         for(int a=0;a<testcount;a++)
         {
           runFeedMotorManual();
           checkHoming(true);
            Serial.print(a);
            Serial.print("\n");
          delay(60);
         }
         Serial.print("ok\n");
         return true;
      }
      
      //set sort speed. Values 1-100. Def 60
      if(input.startsWith("sortspeed:")){
         input.replace("sortspeed:","");
         sortSpeed= input.toInt();
          Serial.print("ok\n");
         return true;
      }

      //set sort steps. Values 1-100. Def 20
      if(input.startsWith("sortsteps:")){
         input.replace("sortsteps:","");
         sortSteps= input.toInt();
          Serial.print("ok\n");
         return true;
      }

      //set feed steps. Values 1-1000. Def 100
      if(input.startsWith("feedsteps:")){
         input.replace("feedsteps:","");
         feedSteps= input.toInt();
          Serial.print("ok\n");
         return true;
      }

      //set feed steps. Values 1-1000. Def 100
      if(input.startsWith("feedpausetime:")){
         input.replace("feedpausetime:","");
         feedPauseTime= input.toInt();
          Serial.print("ok\n");
         return true;
      }

       //set feed steps. Values 1-1000. Def 100
      if(input.startsWith("autohome:")){
         input.replace("autohome:","");
         input.replace(" ", "");
          autoHoming = input == "1";
          Serial.print("ok\n");
         return true;
      }

       //to change sorter arm position by slot number
        if(input.startsWith("sortto:")){
         input.replace("sortto:","");
         int msortsteps= input.toInt();
         runSorterMotor(msortsteps);
         Serial.print("done\n");
         return true;
      }
      
      //to change sorter arm position by steps.
      if(input.startsWith("sorttosteps:")){
         input.replace("sorttosteps:","");
         int msortsteps= input.toInt();
         runSortMotorManual(msortsteps);
         Serial.print("done\n");
         return true;
      }
       //home the feeder
       if(input.startsWith("home")){
         checkHoming(false);
         Serial.print("done\n");
         return true;
      }
        if(input.startsWith("testhome")){
         testHomingSensor();
         Serial.print("done\n");
         return true;
      }
      
      //to run feeder, send  any string starting with f:
       if(input.startsWith("f")){
         input.replace("f","");
         int fs=input.toInt();
         runFeedMotor(fs);
         Serial.print("done\n");
         return true;
      }

      return false; //nothing matched, continue processing the loop at normal
}
