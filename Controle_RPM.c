/* https://www.google.com.br/url?sa=i&rct=j&q=&esrc=s&source=images&cd=&cad=rja&uact=8&ved=0ahUKEwinmOCt2ZXUAhVBjJAKHe5WAYcQjRwIBw&url=http%3A%2F%2Fwww.baudaeletronica.com.br%2Fkit-leds-ir-emissor-e-receptor.html&psig=AFQjCNFFSDk6L1ZYLKaWPKYNiaI9Rxdo_g&ust=1496167754181950
*/
#include <TimerOne.h>

#define COUNT_PERIOD 1000000 // 1s
#define MOTOR_PIN 9 // PWM do motor

int count_IR = 0;
int counts_per_period;
int motor_duty_cycle = 100;
float rpm;

void Receptor(){
  count_IR++;  
  //Serial.println(count_IR);
}

void ISR_timer(){
   counts_per_period = count_IR/2;
   count_IR = 0;
}

void setup() {                
  Serial.begin(9600);
   
  attachInterrupt(0, Receptor, FALLING);
  
  Timer1.initialize(COUNT_PERIOD); // Interrupcao a cada 1ms
  Timer1.attachInterrupt(ISR_timer); // Associa a interrupcao periodica a funcao ISR_timer
  
  analogWrite(MOTOR_PIN, motor_duty_cycle*255/100);  
}

// the loop routine runs over and over again forever:
void loop() {
  
  rpm = counts_per_period*60/(1000000*COUNT_PERIOD);
  Serial.println(rpm);
  delay(1000);
  
}
