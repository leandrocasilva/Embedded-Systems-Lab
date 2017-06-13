/* Projeto Final
 * Recursos utilizados:
 *  - LDR para detecção do laser (LDR em série com resistor de 1k, onde é feita a leitura)
 *  - Teclado Matricial para a senha (possibilidade: usar um 3-input AND (7411) para gerar interrupção)
 *  - Módulo Bluetooth
 *  - EEPROM para guardar senha (EEPROM interna)
 *  - Buzzer
 *  
 *  (Possíveis interrupções: 2 (INT0) e 3 (INT1))
 */

#include <stdio.h>
#include <EEPROM.h>
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
#define LDR                   A0 
#define LED                   13
#define BUZZER                9
#define PASSWORD_ADDRESS      0
#define LIGHT_THRESHOLD       350          // Limiar entre luz e sem luz do LDR
#define PASS_SIZE             10

char    C[] = {C1, C2, C3},
        L[] = {L1, L2, L3, L4};
char    keyboard[4][3]={{'1','2','3'},
                        {'4','5','6'},
                        {'7','8','9'},
                        {'*','0','#'}}; 
char    key, prev_key = 0;
boolean key_wait = false;
uint8_t counter_key = 0;

char    password[PASS_SIZE],
        password_attempt[PASS_SIZE];
uint8_t nth_digit = 0;
boolean proceed = false;

uint8_t LDR_reading;

char    out_buffer[50];
boolean flag_write = 0;

/*-------------------------------------------------------------------------
 * -------------------------    STATES   ----------------------------------
 *------------------------------------------------------------------------- */
enum {STANDBY,
      PASSWORD_ATTEMPT_PRE_DEACTIVATE,
      PASSWORD_ATTEMPT_PRE_CHANGE,
      CHECK_PASSWORD_PRE_DEACTIVATE,
      CHECK_PASSWORD_PRE_CHANGE,
      DEACTIVATE,
      CHANGE_PASSWORD} keyboard_state;

enum {NONE,
      CORRECT,
      INCORRECT} password_state;

enum {BRIGHT,
      DARK} LDR_state;

enum {ALARM_OFF,
      ALARM_ON,
      TRIGGERED} alarm_state;

/*-------------------------------------------------------------------------
 * --------------------------    SWEEP   ----------------------------------
 *------------------------------------------------------------------------- */
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

/*-------------------------------------------------------------------------
 * ------------------------    STR_CMP   ----------------------------------
 *------------------------------------------------------------------------- */
/* Rotina auxiliar para comparacao de strings */
boolean str_cmp(char *s1, char *s2) {
  /* Compare two strings up to length len. Return 1 if they are
   *  equal, and 0 otherwise.
   */
  uint8_t i, len;
  if ((len = strlen(s1)) != strlen(s2)) return 0;
  for (i=0; i<len; i++) {
    if (s1[i] != s2[i]) return 0;
    if (s1[i] == '\0') return 1;
  }
  return 1;
}

/*-------------------------------------------------------------------------
 * --------------------------    SERIAL   ---------------------------------
 *------------------------------------------------------------------------- */

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
volatile boolean flag_check_command = 0;

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

/*-------------------------------------------------------------------------
 * -------------------------    EEPROM   ----------------------------------
 *------------------------------------------------------------------------- */
boolean EEPROM_getString(int address, char* string){
  uint8_t i = 0;
  while((string[i++] = EEPROM.read(address++)) && i < PASS_SIZE);
  if(i == PASS_SIZE) return 0;
  else return 1;
}

void EEPROM_putString(int address, char* string){
  uint8_t i = 0;
  while(string[i]) EEPROM.write(address++, string[i++]);  
  EEPROM.write(address, 0); // Finaliza string
}

/*-------------------------------------------------------------------------
 * ------------------------   ISR_TIMER   ---------------------------------
 *------------------------------------------------------------------------- */
void ISR_timer() {
  //if(analogRead(LDR) < LIGHT_THRESHOLD) LDR_state = DARK;
  //else LDR_state = BRIGHT;
  //Serial.println(analogRead(LDR));
    
  key = sweep();
  
  // Caso a leitura não seja mesma da anterior
  if(key != prev_key) key_wait = false;    // Não há necessidade de debouncing
  prev_key = key;
  
  // Caso em debouncing
  if(key_wait){
    counter_key++;
    key = 0;
  } else if(key){     // Caso algum botão tenha sido pressionado
      counter_key = 0;    // Entre em estado de debouncing
      key_wait = true;
  }
  
  // Caso tempo de debouncing tenha terminado
  if(counter_key >= 100) key_wait = false;
}


/*-------------------------------------------------------------------------
 * --------------------------    SETUP   ----------------------------------
 *------------------------------------------------------------------------- */
void setup() {
  /* Inicializacao */
  keyboard_state = STANDBY;
  password_state = NONE;
  alarm_state = ALARM_OFF;
  buffer_clean();
  flag_check_command = 0;
  unsigned int address = 0;
  
  Serial.begin(9600);

  pinMode(BUZZER,OUTPUT);
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
  if(EEPROM_getString(PASSWORD_ADDRESS, password))
    sprintf(out_buffer, "Stored password: %s\n", password);
  else
    sprintf(out_buffer, "No stored password.\n"); 
  flag_write = 1;
}

/*-------------------------------------------------------------------------
 * --------------------------    LOOP   -----------------------------------
 *------------------------------------------------------------------------- */
void loop() {
  /* A flag_check_command permite separar a recepcao de caracteres
   *  (vinculada a interrupca) da interpretacao de caracteres. Dessa forma,
   *  mantemos a rotina de interrupcao mais enxuta, enquanto o processo de
   *  interpretacao de comandos - mais lento - nao impede a recepcao de
   *  outros caracteres. Como o processo nao 'prende' a maquina, ele e chamado
   *  de nao-preemptivo.
   */
  
  

  /*-------------------------- KEYBOARD STATE MACHINE ----------------------*
   * <<< INSTRUÇÕES >>>
   * Selecione:
   * 1: Para ativar o alarme
   * 2: Para desativar o alarme
   * 3: Para alterar a senha
   * #: Para finalizar entrada de senha
   * *: Para reiniciar entrada de senha
   *-----------------------------------------------------------------------*/
  if(key || proceed){
    switch(keyboard_state){
      /*-------------------- STANDBY --------------------*
       * A espera de uma comando no teclado.             *
       *-------------------------------------------------*/
      case STANDBY:
        switch(key){
          case '1':
            alarm_state = ALARM_ON;
            Serial.println("Alarme ativado com sucesso.");
            break;
          case '2':
            Serial.print( "Para desativar o alarme, digite a senha: ");
            keyboard_state = PASSWORD_ATTEMPT_PRE_DEACTIVATE;
            nth_digit = 0;
            digitalWrite(LED, HIGH);
            break;
          case '3':
            Serial.print("Antes de alterar a senha, digite a antiga: ");
            keyboard_state = PASSWORD_ATTEMPT_PRE_CHANGE;
            nth_digit = 0;
            digitalWrite(LED, HIGH);
            break;
          default:
            Serial.println("Comando desconhecido.");
        }
        break;

      /*---------------- PASSWORD_ATTEMPT -----------------*
       * Senha está sendo digitada.                        *
       *---------------------------------------------------*/
      case PASSWORD_ATTEMPT_PRE_DEACTIVATE:
      case PASSWORD_ATTEMPT_PRE_CHANGE:
        if(key == '#'){ // Terminou de digitar a senha
          digitalWrite(LED, LOW);
          switch(keyboard_state){
            case PASSWORD_ATTEMPT_PRE_DEACTIVATE:
              keyboard_state = CHECK_PASSWORD_PRE_DEACTIVATE; break;
            case PASSWORD_ATTEMPT_PRE_CHANGE:
              keyboard_state = CHECK_PASSWORD_PRE_CHANGE; break;
          }
          password_attempt[nth_digit] = 0;
          Serial.print("\n");   
          proceed = true;
          break;   
        } else if (key == '*'){
            nth_digit = 0;
        } else {
          sprintf(out_buffer, "%c", key);
          flag_write = 1;
          password_attempt[nth_digit++] = key;
          break;
        }
      
       /*-------------- CHECK_PASSWORD ------------------*
       * Verificação de senha.                           *
       *-------------------------------------------------*/
      case CHECK_PASSWORD_PRE_DEACTIVATE:
      case CHECK_PASSWORD_PRE_CHANGE:
        password_state = (str_cmp(password_attempt, password) ? CORRECT : INCORRECT);    
        if(password_state == CORRECT){
          Serial.println("Senha correta.");
          switch(keyboard_state){
            case CHECK_PASSWORD_PRE_DEACTIVATE:
              keyboard_state = DEACTIVATE;
              proceed = true;
              break;
            case CHECK_PASSWORD_PRE_CHANGE:
              Serial.print("Insira nova senha: ");
              digitalWrite(LED, HIGH);
              keyboard_state = CHANGE_PASSWORD; break;
          }
        } else {
          Serial.println("Senha incorreta.");
          keyboard_state = STANDBY;
        }
        break;        
     /*------------------ DEACTIVATE -------------------*
     * Desativar alame.                                 *
     *--------------------------------------------------*/
      case DEACTIVATE:
        alarm_state = ALARM_OFF;
        Serial.println("Alarme desativado com sucesso.");
        noTone(BUZZER);
        break;

      /*--------------- CHANGE_PASSWORD -----------------*
       * Digitando nova senha.                           *
       *-------------------------------------------------*/        
      case CHANGE_PASSWORD:
        if(key == '#'){
          sprintf(out_buffer, "Senha alterada para %s\n", password);
          flag_write = 1;
          keyboard_state = STANDBY;
          digitalWrite(LED, LOW);
          password[nth_digit] = 0; // Finaliza string
          EEPROM_putString(PASSWORD_ADDRESS, password);
        } else if (key == '*'){
            nth_digit = 0;
        } else {
          sprintf(out_buffer, "%c", key);
          flag_write = 1;
          password[nth_digit++] = key; // Guarda senha
        }
        break;
    }
    key = 0;
    proceed = false;
  }

  if(alarm_state == ALARM_ON && LDR_state == DARK){
    alarm_state = TRIGGERED;
    Serial.println("There's been a breach.");
    tone(BUZZER, 440);    
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
