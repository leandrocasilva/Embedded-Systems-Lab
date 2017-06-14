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

// Pinos do teclado matricial / LCD
/* | 12 | 2 | 8 | 7 | <<<
 * | 4 |   | 5 | 6 | */
#define C1 7
#define C2 6
#define C3 5
#define L1 8
#define L2 2
#define L3 12
#define L4 4

// Outras macros
#define LDR                   A0 
#define LED                   13
#define BUZZER                11
#define LASER                 10
#define PASSWORD_ADDRESS      0
#define LIGHT_THRESHOLD       700          // Limiar entre luz e sem luz do LDR
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
  if(analogRead(LDR) < LIGHT_THRESHOLD) LDR_state = DARK;
  else LDR_state = BRIGHT;
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

  pinMode(LASER, OUTPUT);
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
  else {
    sprintf(out_buffer, "Default password: 12345.\n"); 
    strcpy(password, "12345");
  }
  flag_write = 1;

  initLCD();
  LCD_position_cursor(1,1);
  LCD_send_string("paralelo");
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
    proceed = false;
    switch(keyboard_state){
      /*-------------------- STANDBY --------------------*
       * A espera de uma comando no teclado.             *
       *-------------------------------------------------*/
      case STANDBY:
        switch(key){
          case '1':
            alarm_state = ALARM_ON;
            digitalWrite(LASER, HIGH);
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
          nth_digit = 0;
          Serial.print("\n");   
          proceed = true;
        } else if (key == '*'){
            nth_digit = 0;
            Serial.print("\nDigite novamente a senha: ");
        } else {
          sprintf(out_buffer, "%c", key);
          flag_write = 1;
          password_attempt[nth_digit++] = key;
          
        }
        break;
      
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
        keyboard_state = STANDBY;
        alarm_state = ALARM_OFF;
        digitalWrite(LASER, LOW);
        Serial.println("Alarme desativado com sucesso.");
        noTone(BUZZER);
        break;

      /*--------------- CHANGE_PASSWORD -----------------*
       * Digitando nova senha.                           *
       *-------------------------------------------------*/        
      case CHANGE_PASSWORD:
        if(key == '#'){
          keyboard_state = STANDBY;
          digitalWrite(LED, LOW);
          password[nth_digit] = 0; // Finaliza string
          sprintf(out_buffer, "\nSenha alterada para %s\n", password);
          flag_write = 1;
          EEPROM_putString(PASSWORD_ADDRESS, password);
        } else if (key == '*'){
            nth_digit = 0;
            Serial.print("\nDigite novamente a senha: ");
        } else {
          sprintf(out_buffer, "%c", key);
          flag_write = 1;
          password[nth_digit++] = key; // Guarda senha
        }
        break;
    }
    key = 0;
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

#define CLK  A2
#define DIN 3 
#define DC  9 // RS
#define CE  A1 // Enable (ativo baixo)

/*
 * Estrutura para representar a instru��o do LCD e o seu tempo de processamento 
 */
typedef struct _lcd {
  uint8_t cop;
  uint8_t tempo;
} lcd;

#define LCD_FUNCTION_SET 0x38
#define LCD_DISPLAY_CONTROL 0x0C
#define LCD_DISPLAY_CLEAR 0x01
#define LCD_ENTRY_MODE_SET 0x06

/*!
 * \fn pulso (char p)
 * \brief Gera um pulso "Enable" de t*1us.
 * \param[in] p para LCD (p=0) e para Leds (p=1).
 */
void LCD_En_Wait(uint8_t t) {
  digitalWrite(CE, LOW);
  digitalWrite(CE, HIGH);
  delayMicroseconds(t); 
}

/*!
 * \fn RS (uint8_t l)
 * \brief Envia ao LCD o sinal RS pelo pino PORTC[8].
 * \param[in] select valor do RS (0, byte de instru&ccedil;&atilde;o e 1, byte de dados).
 */
void RS(uint8_t select) {
  digitalWrite(DC, select);
}

/*!
 * \fn enviaLCD (char c)
 * \brief Envia ao LCD um byte pelos pinos PORTC[7:0]
 * \param[in] c caracter em ASCII.
 * \param[in] t tempo de processamento necess&aacute;rio.
 */
void LCD_send_char(char c, uint8_t t) {
  shiftOut(DIN, CLK, MSBFIRST, c);
  LCD_En_Wait(t);                      ///< dispara o pulso "Enable" do LCD
}

/*!
 * \fn inicLCD (void) 
 * \brief Inicializa o LCD com a sequ&ecirc;ncia de instru&ccedil;&otilde;es recomendada pelo fabricante
 */
void initLCD(void) {
  pinMode(CLK, OUTPUT);
  pinMode(DIN, OUTPUT);
  pinMode(DC, OUTPUT);
  pinMode(CE, OUTPUT);    

  digitalWrite(CE, HIGH); // LCD Disabled
  
  int k;
  lcd init_LCD[4];

  /*! 
  * Instru&ccedil;&otilde;es de inicializa&ccedil;&atilde;o do LCD
  */
  init_LCD[0].cop = LCD_FUNCTION_SET;
  init_LCD[0].tempo = 40;
  init_LCD[1].cop = LCD_DISPLAY_CONTROL;
  init_LCD[1].tempo = 40;
  init_LCD[2].cop = LCD_DISPLAY_CLEAR;
  init_LCD[2].tempo = 1530;
  init_LCD[3].cop = LCD_ENTRY_MODE_SET;
  init_LCD[3].tempo = 40;

  RS(0);                    ///< Seta o LCD no modo de instru&ccedil;&atilde;o
  for(k = 0; k < 4; k++) {  
    LCD_send_char(init_LCD[k].cop, init_LCD[k].tempo);    ///< instru&ccedil;&atilde;o de inicializa&ccedil;&atilde;o
  } 

  delayMicroseconds(40000);
}

/*!
 * \fn mandaString (char *s)
 * \brief Envia uma string de caracteres.
 * \param[in] s endere&ccedil;o inicial da string.
 */
void LCD_send_string(char * s, uint8_t line) {
  LCD_position_cursor(line, 1);
  RS(1);                          ///< Seta o LCD no modo de dados
  while (*s) {                    ///< enquanto o conte&uacute;do do endere&ccedil;o != 0
    LCD_send_char(*s, 50);          ///< envia o byte
    s++;                        ///< incrementa o endere&ccedil;o
  }
}

/*!
 * \fn posicionaCursos (int linha, int coluna)
 * \brief Posiciona o cursor na tela
 * \param[in] linha Linha de 1 a 2
 * \param[in] coluna Coluna de 1 a 16
 */
void LCD_position_cursor(int linha, int coluna){
  RS(0);
  LCD_send_char(0x80|(0x40*(linha-1)+coluna-1), 60);
}

/*!
 * \fn limpaLCD (void) 
 * \brief Envia a instru��o "Clear Display" (0x01).
 */
void LCD_clean(void) {
  RS(0);                         ///< Seta o LCD no modo de instru&ccedil;&atilde;o
  LCD_send_char(LCD_DISPLAY_CLEAR,1600);
}

