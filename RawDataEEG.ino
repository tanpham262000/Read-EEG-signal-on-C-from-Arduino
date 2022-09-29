
/*

 This example uses the ADS1299 Arduino Library, a software bridge between the ADS1299 TI chip and 
 Arduino. See http://www.ti.com/product/ads1299 for more information about the device and the README
 folder in the ADS1299 directory for more information about the library.
 
 */
typedef long int int32;

#define N_CHANNELS_PER_OPENBCI (8)  //number of channels on a single OpenBCI board

//for using a single OpenBCI board
#include <ADS1299Manager.h>  //for a single OpenBCI board
ADS1299Manager ADSManager; //Uses SPI bus and pins to say data is ready.  Uses Pins 13,12,11,10,9,8,4
#define MAX_N_CHANNELS (N_CHANNELS_PER_OPENBCI)   //how many channels are available in hardware
//#define MAX_N_CHANNELS (2*N_CHANNELS_PER_OPENBCI)   //how many channels are available in hardware...use this for daisy-chained board
int nActiveChannels = MAX_N_CHANNELS;   //how many active channels would I like?


//other settings for OpenBCI
byte gainCode = ADS_GAIN24;   //how much gain do I want
byte inputType = ADSINPUT_NORMAL;   //here's the normal way to setup the channels
//byte inputType = ADSINPUT_SHORTED;  //here's another way to setup the channels
//byte inputType = ADSINPUT_TESTSIG;  //here's a third way to setup the channels

//other variables
long sampleCounter = 0;      // used to time the tesing loop
boolean is_running = false;    // this flag is set in serialEvent on reciept of prompt
#define PIN_STARTBINARY (7)  //pull this pin to ground to start binary transfer
//define PIN_STARTBINARY_OPENEEG (6)
boolean startBecauseOfPin = false;
boolean startBecauseOfSerial = false;

//analog input
#define PIN_ANALOGINPUT (A0)
int analogVal = 0;

#define OUTPUT_NOTHING (0)
#define OUTPUT_TEXT (1)
#define OUTPUT_TEXT_1CHAN (9)
#define OUTPUT_BINARY (2)
#define OUTPUT_BINARY_SYNTHETIC (3)
#define OUTPUT_BINARY_4CHAN (4)
#define OUTPUT_BINARY_OPENEEG (6)
#define OUTPUT_BINARY_OPENEEG_SYNTHETIC (7)
#define OUTPUT_BINARY_WITH_AUX (8)
int outputType;

//Design filters  (This BIQUAD class requires ~6K of program space!  Ouch.)
//For frequency response of these filters: http://www.earlevel.com/main/2010/12/20/biquad-calculator/
#include <Biquad_multiChan.h>   //modified from this source code:  http://www.earlevel.com/main/2012/11/26/biquad-c-source-code/
#define SAMPLE_RATE_HZ (250.0)  //default setting for OpenBCI
#define FILTER_Q (0.5)        //critically damped is 0.707 (Butterworth)
#define FILTER_PEAK_GAIN_DB (0.0) //we don't want any gain in the passband
#define HP_CUTOFF_HZ (0.5)  //set the desired cutoff for the highpass filter
//Biquad_multiChan stopDC_filter(MAX_N_CHANNELS,bq_type_highpass,HP_CUTOFF_HZ / SAMPLE_RATE_HZ, FILTER_Q, FILTER_PEAK_GAIN_DB); //one for each channel because the object maintains the filter states
Biquad_multiChan stopDC_filter(MAX_N_CHANNELS,bq_type_bandpass,10.0 / SAMPLE_RATE_HZ, 6.0, FILTER_PEAK_GAIN_DB); //one for each channel because the object maintains the filter states

Biquad_multiChan khong_filter(MAX_N_CHANNELS, bq_type_highpass, 0.5 / SAMPLE_RATE_HZ, FILTER_Q,FILTER_PEAK_GAIN_DB);
Biquad_multiChan saumuoi_filter(MAX_N_CHANNELS, bq_type_lowpass, 50 / SAMPLE_RATE_HZ, FILTER_Q,FILTER_PEAK_GAIN_DB);

#define NOTCH_FREQ_HZ (50.0)
#define NOTCH_Q (4.0)              //pretty sharp notch
#define NOTCH_PEAK_GAIN_DB (0.0)  //doesn't matter for this filter type
Biquad_multiChan notch_filter1(MAX_N_CHANNELS,bq_type_notch,NOTCH_FREQ_HZ / SAMPLE_RATE_HZ, NOTCH_Q, NOTCH_PEAK_GAIN_DB); //one for each channel because the object maintains the filter states
Biquad_multiChan notch_filter2(MAX_N_CHANNELS,bq_type_notch,NOTCH_FREQ_HZ / SAMPLE_RATE_HZ, NOTCH_Q, NOTCH_PEAK_GAIN_DB); //one for each channel because the object maintains the filter states
boolean useFilters = false;  //enable or disable as you'd like...turn off if you're daisy chaining!
// setup recieve nhan data


void setup() {
  //detect which version of OpenBCI we're using (is Pin2 jumped to Pin3?)
  int OpenBCI_version = OPENBCI_V2;  //assume V2
  pinMode(2,INPUT);  digitalWrite(2,HIGH); //activate pullup...for detecting which version of OpenBCI PCB
  pinMode(3,OUTPUT); digitalWrite(3,LOW);  //act as a ground pin...for detecting which version of OpenBCI PCB
  if (digitalRead(2) == LOW) OpenBCI_version = OPENBCI_V1; //check pins to see if there is a jumper.  if so, it is the older board
  boolean isDaisy = false; if (MAX_N_CHANNELS > 8) isDaisy = true;
  ADSManager.initialize(OpenBCI_version,isDaisy);  //must do this VERY early in the setup...preferably first

  // setup the serial link to the PC
  if (MAX_N_CHANNELS > 8) {
    Serial.begin(115200*2);  //Need 115200 for 16-channels, only need 115200 for 8-channels but let's do 115200*2 for consistency (nhat quan)
  } else {
    Serial.begin(115200);
  }
  ///Serial.println(F("ADS1299-Arduino UNO - Stream Raw Data")); //read the string from Flash to save RAM
  //Serial.print(F("Configured as OpenBCI_Version code = "));Serial.print(OpenBCI_version); Serial.print(F(", isDaisy = "));Serial.println(ADSManager.isDaisy);
  Serial.print(F("Configured for "));
  Serial.print(MAX_N_CHANNELS);// Serial.println(F(" Channels"));
  Serial.flush();
  
  // setup the channels as desired on the ADS1299..set gain, input type, referece (SRB1), and patient bias signal
  for (int chan=1; chan <= nActiveChannels; chan++) {
    ADSManager.activateChannel(chan, gainCode, inputType);
  }

  //setup the lead-off detection parameters
  ADSManager.configureLeadOffDetection(LOFF_MAG_6NA, LOFF_FREQ_31p2HZ);

  //print state of all registers
  ADSManager.printAllRegisters();Serial.flush();

  // setup hardware to allow a jumper or button to start the digitaltransfer
  pinMode(PIN_STARTBINARY,INPUT); digitalWrite(PIN_STARTBINARY,HIGH); //activate pullup
  //pinMode(PIN_STARTBINARY_OPENEEG,INPUT); digitalWrite(PIN_STARTBINARY_OPENEEG,HIGH);  //activate pullup
  
  //look out for daisy chaining and disable filtering because it'll likely take too much computation
  if (nActiveChannels > 8) useFilters = false;
  if (useFilters) Serial.print(F("Configured to do some filtering here on the Arduino."));
  
  
  // tell the controlling program that we're ready to start!
  Serial.println(F("Press '?' to query and print ADS1299 register settings again")); //read it straight from flash
  Serial.println(F("Press 1-8 to disable EEG Channels, q-i to enable (all enabled by default)"));
  Serial.println(F("Press 'f' to enable filters.  'g' to disable filters"));
  Serial.println(F("Press 'x' (text) or 'b' (binary) to begin streaming data..."));    
 
} // end of setup

boolean firstReport = true;
unsigned long totalMicrosBusy = 0;  //use this to count time
void loop(){


//  Serial.print(-400000); // To freeze the lower limit
//  Serial.print(" ");
//  Serial.print(400000); // To freeze the upper limit
//  Serial.print(" ");

  if (digitalRead(PIN_STARTBINARY)==LOW) {
    //button is pressed (or pin is jumpered to ground)
    startBecauseOfPin = true;
    //startRunning(OUTPUT_BINARY_OPENEEG_SYNTHETIC);
    startRunning(OUTPUT_BINARY_OPENEEG);
    //if (firstReport) { Serial.println(F("Starting Binary_OpenEEG Based on Pin")); firstReport=false;}
  } else {
    if (startBecauseOfPin) {
      startBecauseOfPin = false;
      stopRunning();
      //if (firstReport == false) { Serial.println(F("Stopping Binary Based on Pin")); firstReport=true;}
    }
  }
  
  if (is_running) {
 
    //is data ready?      
    while(!(ADSManager.isDataAvailable())){            // watch the DRDY pin
      delayMicroseconds(100);
    }
    unsigned long start_micros = micros();
  
    //get the data
    analogVal = analogRead(PIN_ANALOGINPUT);   // get analog value
    ADSManager.updateChannelData();            // update the channelData array 
    //sampleCounter++;                           // bo index trong counter
    
    //Apply  filers to the data
    if (useFilters) applyFilters();

    //print the data
    switch (outputType) {
      case OUTPUT_NOTHING:
        //don't output anything...the Arduino is still collecting data from the OpenBCI board...just nothing is being done with it
        //if ((sampleCounter % 250) == 1) { Serial.print(F("Free RAM = ")); Serial.println(freeRam()); }; //print memory status
        break;
      case OUTPUT_BINARY:
        ADSManager.writeChannelDataAsBinary(MAX_N_CHANNELS,sampleCounter);  //print all channels, whether active or not
        break;
      case OUTPUT_BINARY_WITH_AUX:
        ADSManager.writeChannelDataAsBinary(MAX_N_CHANNELS,sampleCounter,(long int)analogVal);  //print all channels, whether active or not
        break;
      case OUTPUT_BINARY_SYNTHETIC:
        ADSManager.writeChannelDataAsBinary(MAX_N_CHANNELS,sampleCounter,(boolean)true);  //print all channels, whether active or not
        break; 
      case OUTPUT_BINARY_4CHAN:
        ADSManager.writeChannelDataAsBinary(4,sampleCounter);  //print 4 channels, whether active or not
        break; 
      case OUTPUT_BINARY_OPENEEG:
        ADSManager.writeChannelDataAsOpenEEG_P2(sampleCounter);  //this format accepts 6 channels, so that's what it does
        break; 
      case OUTPUT_BINARY_OPENEEG_SYNTHETIC:
        ADSManager.writeChannelDataAsOpenEEG_P2(sampleCounter,true);  //his format accepts 6 channels, so that's what it does
        break;  
      case OUTPUT_TEXT_1CHAN:  
        ADSManager.printChannelDataAsText(1,sampleCounter);  //print all channels, whether active or not
        break;      
      default:
        ADSManager.printChannelDataAsText(MAX_N_CHANNELS,sampleCounter);  //print all channels, whether active or not
    }

    
//    totalMicrosBusy += (micros()-start_micros); //accumulate
//    if (sampleCounter==250) totalMicrosBusy = 0;  //start from 250th sample
//    if (sampleCounter==500) {
//      stopRunning();
//      Serial.println();
//      Serial.print(F("Was busy for "));
//      Serial.print(totalMicrosBusy);
//      Serial.println(F(" microseconds across 250 samples"));
//      Serial.print(F("Assuming a 250Hz Sample Rate, it was busy for "));
//      unsigned long micros_per_250samples = 1000000UL;
//      Serial.print(((float)totalMicrosBusy/(float)micros_per_250samples)*100.0);
//      Serial.println(F("% of the available time"));
//    }
      
  }

} // end of loop



#define ACTIVATE_SHORTED (2)
#define ACTIVATE (1)
#define DEACTIVATE (0)
void serialEvent(){            // send an 'x' on the serial line to trigger ADStest()
  while(Serial.available()){      
    char inChar = (char)Serial.read();
    switch (inChar)
    {
      //turn channels on and off
      case '1':
        changeChannelState_maintainRunningState(1,DEACTIVATE); break;
      case '2':
        changeChannelState_maintainRunningState(2,DEACTIVATE); break;
      case '3':
        changeChannelState_maintainRunningState(3,DEACTIVATE); break;
      case '4':
        changeChannelState_maintainRunningState(4,DEACTIVATE); break;
      case '5':
        changeChannelState_maintainRunningState(5,DEACTIVATE); break;
      case '6':
        changeChannelState_maintainRunningState(6,DEACTIVATE); break;
      case '7':
        changeChannelState_maintainRunningState(7,DEACTIVATE); break;
      case '8':
        changeChannelState_maintainRunningState(8,DEACTIVATE); break;
      case 'q':
        changeChannelState_maintainRunningState(1,ACTIVATE); break;
      case 'w':
        changeChannelState_maintainRunningState(2,ACTIVATE); break;
      case 'e':
        changeChannelState_maintainRunningState(3,ACTIVATE); break;
      case 'r':
        changeChannelState_maintainRunningState(4,ACTIVATE); break;
      case 't':
        changeChannelState_maintainRunningState(5,ACTIVATE); break;
      case 'y':
        changeChannelState_maintainRunningState(6,ACTIVATE); break;
      case 'u':
        changeChannelState_maintainRunningState(7,ACTIVATE); break;
      case 'i':
        changeChannelState_maintainRunningState(8,ACTIVATE); break;
        
      //turn lead-off detection on and off
      case '!':
        changeChannelLeadOffDetection_maintainRunningState(1,ACTIVATE,PCHAN); break;
      case '@':
        changeChannelLeadOffDetection_maintainRunningState(2,ACTIVATE,PCHAN); break;
      case '#':
        changeChannelLeadOffDetection_maintainRunningState(3,ACTIVATE,PCHAN); break;
      case '$':
        changeChannelLeadOffDetection_maintainRunningState(4,ACTIVATE,PCHAN); break;
      case '%':
        changeChannelLeadOffDetection_maintainRunningState(5,ACTIVATE,PCHAN); break;
      case '^':
        changeChannelLeadOffDetection_maintainRunningState(6,ACTIVATE,PCHAN); break;
      case '&':
        changeChannelLeadOffDetection_maintainRunningState(7,ACTIVATE,PCHAN); break;
      case '*':
        changeChannelLeadOffDetection_maintainRunningState(8,ACTIVATE,PCHAN); break;
      case 'Q':
        changeChannelLeadOffDetection_maintainRunningState(1,DEACTIVATE,PCHAN); break;
      case 'W':
        changeChannelLeadOffDetection_maintainRunningState(2,DEACTIVATE,PCHAN); break;
      case 'E':
        changeChannelLeadOffDetection_maintainRunningState(3,DEACTIVATE,PCHAN); break;
      case 'R':
        changeChannelLeadOffDetection_maintainRunningState(4,DEACTIVATE,PCHAN); break;
      case 'T':
        changeChannelLeadOffDetection_maintainRunningState(5,DEACTIVATE,PCHAN); break;
      case 'Y':
        changeChannelLeadOffDetection_maintainRunningState(6,DEACTIVATE,PCHAN); break;
      case 'U':
        changeChannelLeadOffDetection_maintainRunningState(7,DEACTIVATE,PCHAN); break;
      case 'I':
        changeChannelLeadOffDetection_maintainRunningState(8,DEACTIVATE,PCHAN); break;
       case 'A':
        changeChannelLeadOffDetection_maintainRunningState(1,ACTIVATE,NCHAN); break;
      case 'S':
        changeChannelLeadOffDetection_maintainRunningState(2,ACTIVATE,NCHAN); break;
      case 'D':
        changeChannelLeadOffDetection_maintainRunningState(3,ACTIVATE,NCHAN); break;
      case 'F':
        changeChannelLeadOffDetection_maintainRunningState(4,ACTIVATE,NCHAN); break;
      case 'G':
        changeChannelLeadOffDetection_maintainRunningState(5,ACTIVATE,NCHAN); break;
      case 'H':
        changeChannelLeadOffDetection_maintainRunningState(6,ACTIVATE,NCHAN); break;
      case 'J':
        changeChannelLeadOffDetection_maintainRunningState(7,ACTIVATE,NCHAN); break;
      case 'K':
        changeChannelLeadOffDetection_maintainRunningState(8,ACTIVATE,NCHAN); break;
      case 'Z':
        changeChannelLeadOffDetection_maintainRunningState(1,DEACTIVATE,NCHAN); break;
      case 'X':
        changeChannelLeadOffDetection_maintainRunningState(2,DEACTIVATE,NCHAN); break;
      case 'C':
        changeChannelLeadOffDetection_maintainRunningState(3,DEACTIVATE,NCHAN); break;
      case 'V':
        changeChannelLeadOffDetection_maintainRunningState(4,DEACTIVATE,NCHAN); break;
      case 'B':
        changeChannelLeadOffDetection_maintainRunningState(5,DEACTIVATE,NCHAN); break;
      case 'N':
        changeChannelLeadOffDetection_maintainRunningState(6,DEACTIVATE,NCHAN); break;
      case 'M':
        changeChannelLeadOffDetection_maintainRunningState(7,DEACTIVATE,NCHAN); break;
      case '<':
        changeChannelLeadOffDetection_maintainRunningState(8,DEACTIVATE,NCHAN); break; 
        
      //control the bias generation
      case '`':
        ADSManager.setAutoBiasGeneration(true); break;
      case '~': 
        ADSManager.setAutoBiasGeneration(false); break; 
        
      //control test signals
      case '0':
        activateAllChannelsToTestCondition(ADSINPUT_SHORTED,ADSTESTSIG_NOCHANGE,ADSTESTSIG_NOCHANGE); break;
      case '-':
        activateAllChannelsToTestCondition(ADSINPUT_TESTSIG,ADSTESTSIG_AMP_1X,ADSTESTSIG_PULSE_SLOW); break;
      case '+':
        activateAllChannelsToTestCondition(ADSINPUT_TESTSIG,ADSTESTSIG_AMP_1X,ADSTESTSIG_PULSE_FAST); break;
      case '=':
        //repeat the line above...just for human convenience
        activateAllChannelsToTestCondition(ADSINPUT_TESTSIG,ADSTESTSIG_AMP_1X,ADSTESTSIG_PULSE_FAST); break;
      case 'p':
        activateAllChannelsToTestCondition(ADSINPUT_TESTSIG,ADSTESTSIG_AMP_2X,ADSTESTSIG_DCSIG); break;
      case '[':
        activateAllChannelsToTestCondition(ADSINPUT_TESTSIG,ADSTESTSIG_AMP_2X,ADSTESTSIG_PULSE_SLOW); break;
      case ']':
        activateAllChannelsToTestCondition(ADSINPUT_TESTSIG,ADSTESTSIG_AMP_2X,ADSTESTSIG_PULSE_FAST); break;
        
      //other commands
      case 'n':
        toggleRunState(OUTPUT_BINARY_WITH_AUX);
        startBecauseOfSerial = is_running;
        if (is_running) Serial.println(F("Arduino: Starting binary (including AUX value)..."));
        break;
      case 'b':
        toggleRunState(OUTPUT_BINARY);
        //toggleRunState(OUTPUT_BINARY_SYNTHETIC);
        startBecauseOfSerial = is_running;
        if (is_running) Serial.println(F("Arduino: Starting binary..."));
        break;
      case 'v':
        toggleRunState(OUTPUT_BINARY_4CHAN);
        startBecauseOfSerial = is_running;
        if (is_running) Serial.println(F("Arduino: Starting binary 4-chan..."));
        break;
     case 'c':
        toggleRunState(OUTPUT_BINARY_SYNTHETIC);
        startBecauseOfSerial = is_running;
        if (is_running) Serial.println(F("Arduino: Starting binary (synthetic)..."));
        break;
     case 's':
        stopRunning();
        startBecauseOfSerial = is_running;
        break;
     case 'x':
        toggleRunState(OUTPUT_TEXT);
        startBecauseOfSerial = is_running;
        if (is_running) Serial.println(F("Arduino: Starting text..."));
        break;
     case 'z':
       toggleRunState(OUTPUT_TEXT_1CHAN);
       startBecauseOfSerial = is_running;
       //if (is_running) Serial.println(F("Arduino: Starting text, 1 channel..."));
       break;       
     case 'f':
        useFilters = true;
        //Serial.println(F("Arduino: enabaling filters"));
        break;
     case 'g':
        useFilters = false;
        Serial.println(F("Arduino: disabling filters"));
        break;
     case '?':
        //print state of all registers
        ADSManager.printAllRegisters();
        break;
      default:
        break;
    }
  }
}

boolean toggleRunState(int OUT_TYPE)
{
  if (is_running) {
    return stopRunning();
  } else {
    return startRunning(OUT_TYPE);
  }
}

boolean stopRunning(void) {
  ADSManager.stop();                    // stop the data acquisition
  is_running = false;
  return is_running;
}

boolean startRunning(int OUT_TYPE) {
    outputType = OUT_TYPE;
    ADSManager.start();    //start the data acquisition
    is_running = true;
    return is_running;
}

int changeChannelState_maintainRunningState(int chan, int start)
{
  boolean is_running_when_called = is_running;
  int cur_outputType = outputType;
  
  //must stop running to change channel settings
  stopRunning();
  if (start == true) {
    Serial.print(F("Activating channel "));
    Serial.println(chan);
    ADSManager.activateChannel(chan,gainCode,inputType);
  } else {
    Serial.print(F("Deactivating channel "));
    Serial.println(chan);
    ADSManager.deactivateChannel(chan);
  }
  
  //restart, if it was running before
  if (is_running_when_called == true) {
    startRunning(cur_outputType);
  }
}

int changeChannelLeadOffDetection_maintainRunningState(int chan, int start, int code_P_N_Both)
{
  boolean is_running_when_called = is_running;
  int cur_outputType = outputType;
  
  //must stop running to change channel settings
  stopRunning();
  if (start == true) {
    //Serial.print(F("Activating channel "));
    //Serial.print(chan);
    //Serial.println(F(" Lead-Off Detection"));
    ADSManager.changeChannelLeadOffDetection(chan,ON,code_P_N_Both);
  } else {
    Serial.print(F("Deactivating channel "));
    Serial.print(chan);
    Serial.println(F(" Lead-Off Detection"));
    ADSManager.changeChannelLeadOffDetection(chan,OFF,code_P_N_Both);
  }
  
  //restart, if it was running before
  if (is_running_when_called == true) {
    startRunning(cur_outputType);
  }
}


int activateAllChannelsToTestCondition(int testInputCode, byte amplitudeCode, byte freqCode)
{
  boolean is_running_when_called = is_running;
  int cur_outputType = outputType;
  
  //set the test signal to the desired state
  ADSManager.configureInternalTestSignal(amplitudeCode,freqCode);
  
  //must stop running to change channel settings
  stopRunning();
    
  //loop over all channels to change their state
  for (int Ichan=1; Ichan <= 8; Ichan++) {
    ADSManager.activateChannel(Ichan,gainCode,testInputCode);  //Ichan must be [1 8]...it does not start counting from zero
  }
      
  //restart, if it was running before
  if (is_running_when_called == true) {
    startRunning(cur_outputType);
  }
}

long int runningAve[MAX_N_CHANNELS];
int applyFilters(void) {
  //scale factor for these coefficients was 32768 = 2^15
  const static long int a0 = 32360L; //16 bit shift?
  const static long int a1 = -2L*a0;
  const static long int a2 = a0;
  const static long int b1 = -64718L; //this is a shift of 17 bits!
  const static long int b2 = 31955L;
  static long int z1[MAX_N_CHANNELS], z2[MAX_N_CHANNELS];
  long int val_int, val_in_down9, val_out, val_out_down9;
  float val;
  for (int Ichan=0; Ichan < MAX_N_CHANNELS; Ichan++) {
    switch (1) {
      case 1:
        //use BiQuad
        val = (float) ADSManager.channelData[Ichan]; //get the stored value for this sample
        val = stopDC_filter.process(val,Ichan);    //apply DC-blocking filter
        break;
      case 2:
        //do fixed point, 1st order running ave
        val_int = ADSManager.channelData[Ichan]; //get the stored value for this sample
        //runningAve[Ichan]=( ((512-1)*(runningAve[Ichan]>>2)) + (val_int>>2) )>>7;  // fs/0.5Hz = ~512 points..9 bits
        //runningAve[Ichan]=( ((256-1)*(runningAve[Ichan]>>2)) + (val_int>>2) )>>6;  // fs/1.0Hz = ~256 points...8 bits
        //runningAve[Ichan]=( ((128-1)*(runningAve[Ichan]>>1)) + (val_int>>1) )>>6;  // fs/2.0Hz = ~128 points...7 bits
        val = (float)(val_int - runningAve[Ichan]);  //remove the DC
        break;
//      case 3:
//        val_in_down9 = ADSManager.channelData[Ichan] >> 9; //get the stored value for this sample...bring 24-bit value down to 16-bit
//        val_out = (val_in_down9 * a0  + (z1[Ichan]>>9)) >> (16-9);  //8bits were already removed...results in 24-bit value
//        val_out_down9 = val_out >> 9;  //remove eight bits to go from 24-bit down to 16 bit
//        z1[Ichan] = (val_in_down9 * a1 + (z2[Ichan] >> 9) - b1 * val_out_down9  ) >> (16-9);  //8-bits were pre-removed..end in 24 bit number
//        z2[Ichan] = (val_in_down9 * a2  - b2 * val_out_down9) >> (16-9); //8-bits were pre-removed...end in 24-bit number
//        val = (float)val_out;
//        break;
    }
    val = notch_filter1.process(val,Ichan);     //apply 50Hz notch filter
    val = notch_filter2.process(val,Ichan);     //apply it again
    
    val = khong_filter.process(val, Ichan);     // bandpass top
    val = saumuoi_filter.process(val, Ichan);   // bandpass bot
    
    ADSManager.channelData[Ichan] = (long) val;  //save the value back into the main data-holding object
  }
  return 0;
}

int freeRam() 
{
  extern int __heap_start, *__brkval; 
  int v; 
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval); 
}
