int analogin = 4; //A0, 35
int prev = 0;
const int bufSize = 4;
int aveBuf [bufSize];
int aveBufIndex = 0;
int prevAve = 0;

int aveVal() {
  int maxVal = 0;
  int maxIndex = 0;
  int minVal = 10000;
  int minIndex = 0;
  int sum = 0;
  for (int i=0; i<bufSize; i++) {
//    if (i != maxIndex && i != minIndex) sum += aveBuf[i];
    sum += aveBuf[i];
  }
  return sum/(bufSize);
}

void controlInit() {
  analogSetPinAttenuation(analogin, ADC_6db); // ESP32, 
  analogReadResolution(10); // ESP32, bits
}

int readDial(int pin) {
  int val = analogRead(pin);
//  if (abs(val - prev) > 100) analogRead(pin);
  prev = (val + val + val + prev)/4;
  aveBuf[aveBufIndex] = prev;
  aveBufIndex = (aveBufIndex + 1)%bufSize;
  int ave = aveVal();
  if (abs(ave - prevAve) > 1 || ave == 0 || ave <= 1022)
  {
    prevAve = ave;
    return ave;
  } else {
    prevAve = ave;
    return prevAve;
  }
}
