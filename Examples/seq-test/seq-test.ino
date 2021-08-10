#include "M32.h"
#include "Seq.h"

Seq sequences[8];
int kSeqVals [] = {28, 0, 0, 0, 0, 0, 0, 0, 0, 0, 28, 0, 0, 0, 0, 0};

void setup() {
  sequences[0].setSequence(kSeqVals, 16);
}

void loop() {
  // put your main code here, to run repeatedly:

}
