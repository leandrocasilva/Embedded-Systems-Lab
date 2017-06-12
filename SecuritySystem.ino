/* Projeto Final
 * Recursos utilizados:
 *  - LDR para detecção do laser (LDR em série com resistor de 1k, onde é feita a leitura)
 *  - Teclado Matricial para a senha (possibilidade: usar um 3-input AND (7411) para gerar interrupção)
 *  - Módulo Bluetooth
 *  - EEPROM para guardar senha (?) (EEPROM interna)
 *  
 *  (Possíveis interrupções: 2 (INT0) e 3 (INT1))
 */

#include <stdio.h>
//#include <EEPROM.h>
#include <TimerOne.h>
#include <string.h>

/* Pinos bluetooth
   |   | RX | TX |    | GND
     |    |    |    | VCC
*/

// Pinos do teclado matricial
/* | 3 | 2 | 8 | 7 | <<<
 * | 4 |   | 5 | 6 | */
#define C1 7
#define C2 6
#define C3 5
#define L1 8
#define L2 2
#define L3 3
#define L4 4

// Outras macros
#define LDR A0 
#define LED 13
#define AUTO_MEASURE_INTERVAL 200
#define LED_INTERVAL 200
#define PASSWORD_ADDRESS 0
#define LIGHT_THRESHOLD 350                  // Limiar entre luz e sem luz do LDR

char  C[] = {C1, C2, C3},
      L[] = {L1, L2, L3, L4};
char keyboard[4][3]={ {'1','2','3'},
                      {'4','5','6'},
                      {'7','8','9'},
                      {'*','0','#'}}; 
char key, prev_key = 0;
unsigned int  counter_key = 0,
              counter_LED = 0;
byte key_wait = 0;

enum {STANDBY,
      CHANGE_PASSWORD,
      TYPE_PASSWORD,
      CHECK_PASSWORD,
      TEST} keyboard_state;

enum {NONE,
      CORRECT,
      INCORRECT} password_state;

enum {BRIGHT,
      DARK} LDR_state;

enum {OFF,
      ON,
      TRIGGERED} alarm_state;

char password[10], password_attempt[10];
uint8_t nth_digit = 0;
int LDR_reading;

char sweep(){
  int i,j;
  for(i=0;i<4;i++){
    digitalWrite(L[i], LOW);
    for(j=0;j<3;j++){
      if(digitalRead(C[j]) == LOW){
        digitalWrite(L[i], HIGH);
        return keyboard[i][j];
      }
    }
    digitalWrite(L[i], HIGH);
  }
  return 0;  
}

/* Rotina auxiliar para comparacao de strings */
int str_cmp(char *s1, char *s2) {
  /* Compare two strings up to length len. Return 1 if they are
   *  equal, and 0 otherwise.
   */
  int len;
  if ((len = strlen(s1)) != strlen(s2)) return 0;
  int i;
  for (i=0; i<len; i++) {
    if (s1[i] != s2[i]) return 0;
    if (s1[i] == '\0') return 1;
  }
  return 1;
}

/* Processo de bufferizacao. Caracteres recebidos sao armazenados em um buffer. Quando um caractere
 *  de fim de linha ('\n') e recebido, todos os caracteres do buffer sao processados simultaneamente.
 */

/* Buffer de dados recebidos */
#define MAX_BUFFER_SIZE 15
typedef struct {
  char data[MAX_BUFFER_SIZE];
  unsigned int tam_buffer;
} serial_buffer;

/* Teremos somente um buffer em nosso programa, O modificador volatile
 *  informa ao compilador que o conteudo de Buffer pode ser modificado a qualquer momento. Isso
 *  restringe algumas otimizacoes que o compilador possa fazer, evitando inconsistencias em
 *  algumas situacoes (por exemplo, evitando que ele possa ser modificado em uma rotina de interrupcao
 *  enquanto esta sendo lido no programa principal).
 */
volatile serial_buffer Buffer;

/* Todas as funcoes a seguir assumem que existe somente um buffer no programa e que ele foi
 *  declarado como Buffer. Esse padrao de design - assumir que so existe uma instancia de uma
 *  determinada estrutura - se chama Singleton (ou: uma adaptacao dele para a programacao
 *  nao-orientada-a-objetos). Ele evita que tenhamos que passar o endereco do
 *  buffer como parametro em todas as operacoes (isso pode economizar algumas instrucoes PUSH/POP
 *  nas chamadas de funcao, mas esse nao eh o nosso motivo principal para utiliza-lo), alem de
 *  garantir um ponto de acesso global a todas as informacoes contidas nele.
 */

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


/* Flags globais para controle de processos da interrupcao */
volatile int flag_check_command = 0;

/* Rotinas de interrupcao */

/* Ao receber evento da UART */
void serialEvent() {
  char c;
  while (Serial.available()) {
    c = Serial.read();
    if (c=='\n') {
      buffer_add('\0'); /* Se recebeu um fim de linha, coloca um terminador de string no buffer */
      flag_check_command = 1;
    } else {
     buffer_add(c);
    }
  }
}

int getInt(char *s){
  int number=0;
  byte i;
  for(i=4; s[i] != 0; i++){
    number += s[i]-'0';
    number *= 10;
  }
  return number/10;
}

//void EEPROM_getString(int address, char* string){
//  uint8_t i = 0;
//  while(string[i++] = EEPROM.read(address++));
//}
//
//void EEPROM_putString(int address, char* string){
//  uint8_t i = 0;
//  while(string[i]) EEPROM.write(address++, string[i++]);  
//  EEPROM.write(address, 0); // Finaliza string
//}

void ISR_timer() {
  //if(analogRead(LDR) < LIGHT_THRESHOLD) LDR_state = DARK;
  //else LDR_state = BRIGHT;
  //Serial.println(analogRead(LDR));
    
  key = sweep();
  
  // Caso a leitura não seja mesma da anterior
  if(key != prev_key) key_wait = 0;    // Não há necessidade de debouncing
  prev_key = key;
  
  // Caso em debouncing
  if(key_wait){
    counter_key++;
    key = 0;
  } else if(key){     // Caso algum botão tenha sido pressionado
      counter_key = 0;    // Entre em estado de debouncing
      key_wait = 1;
  }
  
  // Caso tempo de debouncing tenha terminado
  if(counter_key >= 100) key_wait = 0;
}

/* Funcoes internas ao void main() */

void setup() {
  /* Inicializacao */
  keyboard_state = STANDBY;
  password_state = NONE;
  alarm_state = OFF;
  buffer_clean();
  flag_check_command = 0;
  unsigned int address = 0;
  
  Serial.begin(9600);
  
  pinMode(LED, OUTPUT);
  int i;
  for(i=0;i<=3;i++){
    pinMode(L[i], OUTPUT);
    digitalWrite(L[i], HIGH);
  }
  for(i=0;i<=2;i++) pinMode(C[i], INPUT_PULLUP);
  
  Timer1.initialize(10000); // Interrupcao a cada 10ms
  Timer1.attachInterrupt(ISR_timer); // Associa a interrupcao periodica a funcao ISR_timer  

  // Update <password> variable from memory
  //EEPROM_getString(PASSWORD_ADDRESS, password);
  //sprintf(out_buffer, "Stored password: %s\n", password);
  //flag_write = 1;
}


void loop() {
  /* A flag_check_command permite separar a recepcao de caracteres
   *  (vinculada a interrupca) da interpretacao de caracteres. Dessa forma,
   *  mantemos a rotina de interrupcao mais enxuta, enquanto o processo de
   *  interpretacao de comandos - mais lento - nao impede a recepcao de
   *  outros caracteres. Como o processo nao 'prende' a maquina, ele e chamado
   *  de nao-preemptivo.
   */
  
  char out_buffer[50];
  int flag_write = 0;

  // Se tecla do teclado matricial foi pressionado
  if(key != 0){
    switch(keyboard_state){
      // Caso à espera de um comando
      case STANDBY:
        // Asterisco indica mudança de senha
        if(key == '*'){
          sprintf(out_buffer, "Typing new password...\n");
          flag_write = 1;
          keyboard_state = CHANGE_PASSWORD;
          nth_digit = 0;
          digitalWrite(LED, HIGH);
        } else { // Um dígito
          sprintf(out_buffer, "Password: %c", key);
          flag_write = 1;
          keyboard_state = TYPE_PASSWORD;
          nth_digit = 0;
          password_attempt[nth_digit++] = key;
        }
        break;

      // Caso esteja digitando nova senha
      case CHANGE_PASSWORD:
        // Caso asterisco novamente: senha finalizada
        if(key == '#'){
          keyboard_state = STANDBY;
          digitalWrite(LED, LOW);
          password[nth_digit] = 0; // Finaliza string
          //EEPROM_putString(PASSWORD_ADDRESS, password);
        } else {
          password[nth_digit++] = key; // Guarda senha
        }
        break;

      // Caso esteja digitando a senha
      case TYPE_PASSWORD:
        if(key == '#'){ // Terminou de digitar a senha
          keyboard_state = CHECK_PASSWORD;
          password_attempt[nth_digit] = 0;
          Serial.print("\n");
        } else{
          sprintf(out_buffer, "%c", key);
          flag_write = 1;
          password_attempt[nth_digit++] = key;
          break;
        }
      
      // Verificação da senha
      case CHECK_PASSWORD:
        Serial.println("Password is being verified ...");
        //flag_write = 1;
        password_state = (str_cmp(password_attempt, password) ? CORRECT : INCORRECT);    
        if(password_state == CORRECT) Serial.println("Access granted.");
        else Serial.println("Access denied.");
        keyboard_state = STANDBY; 
        break;        
    }
    key = 0;
  }

  if(alarm_state == ON && LDR_state == DARK){
    alarm_state = TRIGGERED;
    Serial.println("There's been a breach.");
    // DO something    
  }
  
//  if (flag_check_command == 1) {
//    if (str_cmp((char *)Buffer.data, "PING", 4)) {
//      // ...
//      flag_write = 1;
//    }
//    
//    else buffer_clean();
//
//    flag_check_command = 0;
//  }

  /* Posso construir uma dessas estruturas if(flag) para cada funcionalidade
   *  do sistema. Nesta a seguir, flag_write e habilitada sempre que alguma outra
   *  funcionalidade criou uma requisicao por escrever o conteudo do buffer na
   *  saida UART.
   */
  if (flag_write == 1) {
    Serial.write(out_buffer);
    buffer_clean();
    flag_write = 0;
  }

}
