/*  Laboratório de Sistemas Embarcados - EA076C - FEEC/Unicamp
    Projeto 1 - Semáforo
    Autoria: Igor Maronni   155755
             Leandro Silva  156176
    Descrição: Prova de conceito para um semáforo que fecha para os carros sob demanda de pedestre
               e é desativado, piscando amarelo, durante todo o período da noite.
*/

#include <TimerOne.h>

#define T_CAR_MIN 5000                       // Tempo mínimo para fechamento do sinal do carro
#define T_YELLOW 2000                        // Tempo do sinal amarelo antes do sinal do carro fechar
#define T_PED 5000                           // Tempo de passagem do pedestre (led ved do pedestre aceso)
#define T_PED_BLINK (T_PED/5)                // Tempo total de piscagem do led vermelho do pedestre
#define T_PED_BLINK_PERIOD (T_PED_BLINK/12)  // Período da piscagem do led vermelho do pedestre
#define T_NIGHT_BLINK_PERIOD 1000            // Período de piscagem do led amarelo à noite
#define LIGHT_THRESHOLD 350                  // Limiar entre dia e noite do LDR
#define T_VERIFY 10000                       // Tempo de verificação do estado do LDR (dia/noite)

// Definição dos pinos no Arduino
int led_ped_g = 3; // Led do sinal de pedestre > green
int led_ped_r = 4; // Led do sinal de pedestre > red
int led_car_g = 5; // Led do sinal do carro > green
int led_car_y = 6; // Led do sinal do carro > yellow
int led_car_r = 7; // Led do sinal do carro > red
int button    = 2; // Botão do pedestre (pino de interrupção externa INT0)

int sensorValue;         // Leitura do LDR
long t_car = 0;          // Contador do tempo mínimo para fechamento do sinal do carro
char led_ped_now = 0;    // Estado atual do led vermelho do pedestre enquanto está piscando
char led_yellow_now = 0; // Estado atual do led amarelo enquanto está piscando à noite

// Nomenclatura dos estados
enum {car_green, 
      car_green_wait,
      car_red,
      car_red_wait,
      car_yellow,
      car_yellow_wait,
      ped_blink,
      night,
      verify_night,
      verify_day} state;

/*----------------------------------------------------------------------------
 -----------------------           CONTADOR            -----------------------
 -----------------------------------------------------------------------------
 Autoria principal:   Tiago Fernandes Tavares
 Adaptações:          Igor Maronni & Leandro Silva
 Descrição: Contagem independente de número de eventos discretos.
 -----------------------------------------------------------------------------*/
 
//Estrutura que armazena todos os dados de um contador
typedef struct Counter_{
  unsigned int current_counter;
  unsigned int max_counter;
} Counter;

// Configura a contagem máxima de um contador
void counter_config(struct Counter_ *c, unsigned int new_max_counter){
  c->max_counter = new_max_counter;
}

void counter_reset(struct Counter_ *c){
  c->current_counter = 0;
}

// Realiza contagem em um contador
void counter_count(struct Counter_ *c){
  (c->current_counter)++;
}

// Retorna 1 caso o contador tenha chegado ao máximo e 0 caso contrário
int counter_end(struct Counter_ *c){
  if ((c->current_counter) >= (c->max_counter)) return 1;
  else return 0; 
}

/*----------------------------------------------------------------------------*/

// Declaração dos contadores
Counter t_yellow;
Counter t_ped;   
Counter t_ped_blink;
Counter t_ped_blink_period;
Counter t_night_blink_period;
Counter t_verify;

// Chamada de interrupção periódica
void ISR_timer() {
  t_car++;
  counter_count(&t_yellow);
  counter_count(&t_ped);
  counter_count(&t_ped_blink);
  counter_count(&t_ped_blink_period);
  counter_count(&t_night_blink_period);
  counter_count(&t_verify);
}

// Chamada de interrupção do botão do pedestre
void ISR_button() {
  // Apenas é considerado o pedido de passagem do pedestre quando os carros estiverem passando
  if(state == car_green || state == verify_night) state = car_green_wait;
}

// Alterna o estado do led amarelo
void blink_yellow() {
  // Espera terminar o período de piscagem
  if(counter_end(&t_night_blink_period)){
    digitalWrite(led_car_y, !led_yellow_now);
    led_yellow_now = !led_yellow_now;
    counter_reset(&t_night_blink_period);
  }
}

void setup() {
 
  // Inicializar a rotina de interrupcao periodica
  Timer1.initialize(1000); // Interrupcao a cada 1ms
  Timer1.attachInterrupt(ISR_timer); // Associa a interrupcao periodica a funcao ISR_timer  

  // Associa a função ISR_button ao pino INT0 do Arduino e a chama a cada sinal de subida
  attachInterrupt(0, ISR_button, RISING);
 
  // Define a entrada ou saída dos GPIO
  for(int led = 3; led <= 7; led++) pinMode(led, OUTPUT);
  pinMode(button, INPUT);
  
  // Estado inicial
  state = car_green;
  
  // Inicialização dos contadores
  counter_config(&t_yellow, T_YELLOW);
  counter_config(&t_ped, T_PED);
  counter_config(&t_ped_blink, T_PED_BLINK);
  counter_config(&t_ped_blink_period, T_PED_BLINK_PERIOD);
  counter_config(&t_night_blink_period, T_NIGHT_BLINK_PERIOD);
  counter_config(&t_verify, T_VERIFY);
}

void loop() {
  // Máquina de estados do semáforo
  switch(state){
    // Passagem dos carros e verificação do LDR
    case car_green: 
      digitalWrite(led_car_y, LOW);
      digitalWrite(led_car_r, LOW);
      digitalWrite(led_ped_g, LOW);
      digitalWrite(led_car_g, HIGH);
      digitalWrite(led_ped_r, HIGH);

      // Verificação do LDR
      Serial.println(analogRead(A0));
      if(analogRead(A0) < LIGHT_THRESHOLD){
        state = verify_night;
        counter_reset(&t_verify);
      }
      break;      

    // Momento de espera do tempo mínimo de passagem dos carros após o pedestre apertar o botão
    case car_green_wait:
      if(t_car >= T_CAR_MIN) state = car_yellow;
      break;

    // Início da transição entre o sinal verde e vermelho do carro  
    case car_yellow:
      counter_reset(&t_yellow);
      digitalWrite(led_car_g, LOW);
      digitalWrite(led_car_y, HIGH);
      state = car_yellow_wait;
      break;

    // Sinal amarelo para o carro  
    case car_yellow_wait:
      if(counter_end(&t_yellow)) state = car_red;
      break;

    // Início da passagem dos pedestres
    case car_red:      
      digitalWrite(led_car_y, LOW);
      digitalWrite(led_car_r, HIGH);
      digitalWrite(led_ped_r, LOW);
      digitalWrite(led_ped_g, HIGH);
      counter_reset(&t_ped);
      state = car_red_wait;
      break;

    // Passagem dos pedestres
    case car_red_wait:
      // Caso o tempo de passagem dos pedestre esteja em seu fim
      if(counter_end(&t_ped)){
        t_car = 0;
        state = ped_blink;
        counter_reset(&t_ped_blink);
        digitalWrite(led_ped_g, LOW);
        counter_reset(&t_ped_blink_period);
      }
      break;

    // Momentos finais antes de fechar o sinal do pedestre (pisca led vermelho)  
    case ped_blink:
      // Espera terminar o perído de piscagem
      if(counter_end(&t_ped_blink_period)){
        digitalWrite(led_ped_r, !led_ped_now);
        led_ped_now = !led_ped_now;
        counter_reset(&t_ped_blink_period);
      }
      // Espera terminar o tempo total de piscagem
      if(counter_end(&t_ped_blink)) state = car_green;
      break;

    // Verifica se o estado de noite se mantém por 10s  
    case verify_night:
      // Verifica a leitura do LDR a todo o momento até dar os 10s
      if(!counter_end(&t_verify)){
        if(analogRead(A0) > LIGHT_THRESHOLD){
          state = car_green; // Não é realmente de noite
        }
      } else { // É realmente de noite
        digitalWrite(led_car_g, LOW);
        digitalWrite(led_ped_r, LOW);
        state = night;
      }
      break;

    // Verifica se o estado de dia se mantém por 10s      
    case verify_day:
      // Verifica a leitura do LDR a todo o momento até dar os 10s
      if(!counter_end(&t_verify)){
        if(analogRead(A0) < LIGHT_THRESHOLD){
          state = night; // Não é realmente de dia
        }
      } else { // É realmente de dia
        digitalWrite(led_car_g, LOW);
        digitalWrite(led_ped_r, LOW);
        state = car_green;
      }
      blink_yellow();
      break;

    // Estado noturno (pisca o led amarelo)  
    case night:
      blink_yellow();
      // Verifica o estado do LDR
      if(analogRead(A0) >= LIGHT_THRESHOLD){
        state = verify_day;
        counter_reset(&t_verify);
      }
      break;
  }  
}
