/****************************************************
 *        CONTROLE RPM DE UM MOTOR DC               *
 *       ------------------------------             *
 *  FEEC/Unicamp                                    *
 *  Laboratório de Sistema Embarcados/EA076         *
 *  Junho de 2017                                   *
 *                                                  *
 * Autores: Leandro Silva   RA 156176               *
 *          Igor Maronni    RA 155755               *
 ****************************************************/

#include <TimerOne.h>

#define COUNT_PERIOD 500000             // Intervalo entre medidas de velocidade = 0.5s
#define MOTOR_PIN 11                    // Pino PWM de controle do motor
#define MAX_BUFFER_SIZE 15              // Tamanho máximo da string recebida por serial

int count_IR = 0,           // Contagem da passagem das hélices pelo sensor IR
    counts_per_period,      // Número de rotações ocorridas no intervalo de tempo COUNT_PERIOD
    speed_ref = 0,          // Velocidade desejada
    prev_speed_ref = -1;    // Última velocidade desejada

float rpm, rpm_norm,              // Velocidade atual do motor em RPM (e normalizado)
      error, error_norm,          // Erro dinâmico atual de velocidade (e normalizado)
      prev_error_norm = 0,        // Último erro
      sum = 0, diff = 0,          // Acúmulo e variação do erro
      kp = 2, kd = 0, ki = 0.01,  // Constantes do PID
      command;                    // Comando de controle do PWM do motor

/*************************************************
 *              COMUNICAÇÃO SERIAL               *
 *         ----------------------------          *
 *            Autor: Tiago Tavares               *
 *************************************************/
volatile int flag_check_speed = 0;

typedef struct {
  char data[MAX_BUFFER_SIZE];
  unsigned int tam_buffer;
} serial_buffer;

volatile serial_buffer Buffer;

/* Limpa buffer */
void buffer_clean() {
  Buffer.tam_buffer = 0;
}

/* Adiciona caractere ao buffer */
int buffer_add(char c_in) {
  if (Buffer.tam_buffer < MAX_BUFFER_SIZE) {
    Buffer.data[Buffer.tam_buffer++] = c_in;
    return 1;
  }
  return 0;
}

/* Ao receber evento da UART */
void serialEvent() {
  char c;
  while (Serial.available()) {
    c = Serial.read();
    if ((c=='\n') || (c=='\r') || (c==0)) {
      buffer_add('\0'); /* Se recebeu um fim de linha, coloca um terminador de string no buffer */
      flag_check_speed = 1;
    } else {
     buffer_add(c);
    }
  }
}

/*************************************************/

/* Converte número ascii em integer
 * @param s string numérica
 * @return inteiro  */
int getInt(char *s){
  unsigned int number=0;
  byte i;
  for(i=0; s[i] != 0; i++){
    number += s[i]-'0';
    number *= 10;
  }
  return number/10;
}

/*************************************************
 *            ROTINAS DE INTERRUPÇÃO             *
 *************************************************/

// Rotina chamada a cada passagem da hélice pelo sensor IR
void Receptor(){
  count_IR++;  
}

// Rotina chamada a cada COUNT_PERIOD microsegundos
// para calcular o número de rotações ocorridas num intervalo de tempo
void ISR_timer(){
   counts_per_period = count_IR/2;    // São duas pás da hélice que passam em cada rotação completa
   count_IR = 0;
}

/*************************************************/

void setup() {                
  Serial.begin(9600);

  // Interrupção pelas hélices no sensor IR (pino 2 do Arduino)
  attachInterrupt(0, Receptor, FALLING);

  // Interrupção periódica para cálculo da velocidade
  Timer1.initialize(COUNT_PERIOD);
  Timer1.attachInterrupt(ISR_timer); // Associa a interrupcao periodica a funcao ISR_timer  
}

void loop() {

  // Caso uma nova velocidade desejada tenha sido recebida
  if(flag_check_speed){
    speed_ref = getInt((char*)Buffer.data);
    buffer_clean();
    flag_check_speed = 0;    
  }
  
  rpm = counts_per_period*60*1000000/COUNT_PERIOD;  // Velocidade atual em RPM
  rpm_norm = rpm*100/6300;  // Porcentagem da velocidade máxima medida (6300 RPM)

  // Erro dinâmico
  error = rpm - speed_ref;
  error_norm = error*100/6300;

  // Acúmulo do erro
  if(speed_ref != prev_speed_ref) sum = 0; // Reseta a contagem quando há uma nova velocidade desejada
  sum += error_norm;

  // Variação do erro
  diff = (error_norm - prev_error_norm);

  // Comando do motor usand PID
  command = (rpm_norm - kp*error_norm - ki*sum - kd*diff) * 255/100;
  if (command > 255) command = 255; // 255 equivale a 100% de duty cycle
  analogWrite(MOTOR_PIN, command);
  
  prev_speed_ref = speed_ref;
  prev_error_norm = error_norm;
  
  Serial.println(rpm);  
}
