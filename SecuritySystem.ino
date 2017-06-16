/* LABORATÓRIO DE SISTEMAS EMBARCADOS - EA076 - Turma C
 * FEEC/Unicamp
 * 
 * Projeto final: Sistema de segurança
 * Autores: Leandro Silva   156176
 *          Igor Maronni    155755
 * Data: Junho de 2017
 */

#include <EEPROM.h>
#include <TimerOne.h>
#include <string.h>

/*-------------------------------------------------------------------------
 * -------------------------    MACROS   ----------------------------------
 *------------------------------------------------------------------------- */
// Pinos do teclado matricial & LCD
/* | 12 | 2 | 8 | 7 | 5V  | A2 | 9 | 5V |<<<
 * | 4  |   | 5 | 6 | GND | 5V | 3 | A1 |*/

// Macros do teclado matricial
#define C1    7
#define C2    6
#define C3    5
#define L1    8
#define L2    2
#define L3    12
#define L4    4

// Macros do LCD por SPI
#define CLK   A2  // Clock
#define DIN   3   // Data input
#define DC    9   // Register Select (RS)
#define CE    A1  // Enable (ativo baixo)

#define LCD_FUNCTION_SET    0x38  // 2-line mode and display off
#define LCD_DISPLAY_CONTROL 0x0C  // Display on, cursor off and blink off
#define LCD_DISPLAY_CLEAR   0x01
#define LCD_ENTRY_MODE_SET  0x06  // Increment mode and entire shift off

// Outras macros
#define LDR                   A0    // Fotoresistor
#define LED                   13    // LED para indicar inserção de senha
#define BUZZER                11    // Buzzer para indicar violação de segurança  
#define LASER                 10
#define PASSWORD_ADDRESS      0     // Endereço da EEPROM onde será guardada a senha
#define LIGHT_THRESHOLD       600   // Limiar entre luz (maior que) e sem luz (menor que) do LDR
#define PASS_SIZE             10    // Tamanho máximo da senha

/*-------------------------------------------------------------------------
 * ---------------------  GLOBAL VARIABLES  -------------------------------
 *------------------------------------------------------------------------- */
// Variáveis do teclado matricial
char    C[] = {C1, C2, C3},
        L[] = {L1, L2, L3, L4};           // Colunas e linhas  
char    keypad[4][3]={{'1','2','3'},
                        {'4','5','6'},
                        {'7','8','9'},
                        {'*','0','#'}};   // Caracteres disponíveis
char    key, prev_key = 0;                // Atual e anterior teclas pressionadas
boolean key_debounce = false;             // Estado de debouncing do teclado  
uint8_t key_debounce_time = 0;            // Tempo de debouncing

// Variáveis do sistema de segurança
char    password[PASS_SIZE],              // Senha para controlar o sistema
        password_attempt[PASS_SIZE];      // Senha inserida para checagem
uint8_t nth_digit = 0;                    // Índice da senha digitada
boolean proceed = false;                  // Flag utilizada para adentrar na máquina de estados do teclado matricial
                                          // mesmo que nenhuma tecla tenha sido pressionada

uint8_t LDR_reading;                      // Leitura do sensor de luz  

/*-------------------------------------------------------------------------
 * -------------------------    STATES   ----------------------------------
 *------------------------------------------------------------------------- */

// Estados do teclado matricial
enum {STANDBY,                          // Em espera de um comando
      PASSWORD_ATTEMPT_PRE_DEACTIVATE,  // Inserção de senha para desativar alarme
      PASSWORD_ATTEMPT_PRE_CHANGE,      // Inserção de senha para alterar senha
      CHECK_PASSWORD_PRE_DEACTIVATE,    // Verificação de senha para desativar alarme
      CHECK_PASSWORD_PRE_CHANGE,        // Verificação de senha para alterar senha
      DEACTIVATE,                       // Desativar alarme
      CHANGE_PASSWORD} keypad_state;    // Alterar senha

// Estados da verificação de senha
enum {NONE,
      CORRECT,                          
      INCORRECT} password_state;

// Estados do sensor de luz
enum {BRIGHT,
      DARK} LDR_state;

// Estados do alarme
enum {ALARM_OFF,                // Alarme desligado
      ALARM_ON,                 // Alarme ligado
      TRIGGERED} alarm_state;   // Uma quebra na segurança foi detectada

/*-------------------------------------------------------------------------
 * -------------------   FUNCTION PROTOTYPES   ----------------------------
 *------------------------------------------------------------------------- */
void setup();
void loop();

char    KEYPAD_sweep();
boolean str_cmp(char *s1, char *s2);

boolean EEPROM_getString(int address, char* string);
void    EEPROM_putString(int address, char* string);

void ISR_timer();

void LCD_En_Wait(uint8_t t);
void LCD_RS(uint8_t select);
void LCD_send_data(char c);
void LCD_send_command(char c, uint8_t t);
void LCD_init(void);
void LCD_send_string(char * s, uint8_t line);
void LCD_clear_line(uint8_t line);
void LCD_position_cursor(int linha, int coluna);
void LCD_clean(void);

/*-------------------------------------------------------------------------
 * --------------------------    SETUP   ----------------------------------
 *------------------------------------------------------------------------- */
void setup() {
  // Inicializacao de estados
  keypad_state = STANDBY;
  password_state = NONE;
  LDR_state      = BRIGHT;
  alarm_state    = ALARM_OFF;

  pinMode(LASER, OUTPUT);
  pinMode(BUZZER,OUTPUT);
  pinMode(LED,   OUTPUT);

  // Inicialização dos pinos do teclado
  int i;
  for(i=0;i<=3;i++){
    pinMode(L[i], OUTPUT);
    digitalWrite(L[i], HIGH);
  }
  for(i=0;i<=2;i++) pinMode(C[i], INPUT_PULLUP);

  // Interrupção periódica
  Timer1.initialize(10000);           // Interrupcao a cada 10ms
  Timer1.attachInterrupt(ISR_timer);  // Associa a interrupcao periodica a funcao ISR_timer  

  // Carrega senha armazenada na EEPROM na variável <password>
  if(!EEPROM_getString(PASSWORD_ADDRESS, password)){
    strcpy(password, "12345");  // Caso não haja, insere uma senha default  
  }

  // Inicializa LCD e insere instruções na tela
  LCD_init();
  LCD_send_string("1-ON 2-OFF", 1);
  LCD_send_string("3-Alt. senha", 2);
}

/*-------------------------------------------------------------------------
 * --------------------------    LOOP   -----------------------------------
 *------------------------------------------------------------------------- */
void loop() {
  /*-------------------------- KEYPAD STATE MACHINE ------------------------*
   * <<< INSTRUÇÕES >>>
   * Selecione:
   * 1: Para ativar o alarme
   * 2: Para desativar o alarme
   * 3: Para alterar a senha
   * #: Para finalizar entrada de senha
   * *: Para reiniciar entrada de senha
   *-----------------------------------------------------------------------*/

  // Se um botão foi pressionado ou se há necessidade de mudança de estado
  if(key || proceed){
    proceed = false;
    switch(keypad_state){
      /*-------------------- STANDBY --------------------*
       * A espera de uma comando no teclado.             *
       *-------------------------------------------------*/
      case STANDBY:
        switch(key){
          case '1': // Ativar alarme
            LCD_send_string("Alarme ativado.", 1);
            LCD_clear_line(2);
            alarm_state = ALARM_ON;
            digitalWrite(LASER, HIGH);
            break;
          case '2': // Desativar alarme
            if(alarm_state == ALARM_OFF){
              LCD_send_string("Alarme ja", 1);
              LCD_send_string("desativado.", 2);
            } else {
              LCD_send_string("Desativar alarme.", 1);
              LCD_send_string("Senha: ", 2);              
              nth_digit = 0;  // Zera índice da senha
              digitalWrite(LED, HIGH);
              keypad_state = PASSWORD_ATTEMPT_PRE_DEACTIVATE;
            }
            break;
          case '3': // Alterar senha
            LCD_send_string("Insira senha", 1);
            LCD_send_string("atual: ", 2);            
            nth_digit = 0;  // Zera índice da senha
            keypad_state = PASSWORD_ATTEMPT_PRE_CHANGE;
            digitalWrite(LED, HIGH);
            break;
          default:
            LCD_send_string("Comando", 1);
            LCD_send_string("desconhecido. ", 2);
        }
        break;

      /*---------------- PASSWORD_ATTEMPT -----------------*
       * Senha está sendo digitada.                        *
       *---------------------------------------------------*/
      case PASSWORD_ATTEMPT_PRE_DEACTIVATE:
      case PASSWORD_ATTEMPT_PRE_CHANGE:
        if(key == '#'){ // Terminou de digitar a senha
          digitalWrite(LED, LOW);
          switch(keypad_state){
            case PASSWORD_ATTEMPT_PRE_DEACTIVATE:
              keypad_state = CHECK_PASSWORD_PRE_DEACTIVATE; break;
            case PASSWORD_ATTEMPT_PRE_CHANGE:
              keypad_state = CHECK_PASSWORD_PRE_CHANGE; break;
          }
          password_attempt[nth_digit] = 0;  // Finaliza string
          proceed = true;
        } else if (key == '*'){ // Reiniciar inserção de senha
            nth_digit = 0;  // Zera índice da senha
            // Reescreve segunda linha do teclado
            LCD_clear_line(2);
            switch(keypad_state){
            case PASSWORD_ATTEMPT_PRE_DEACTIVATE:
              LCD_send_string("Senha: ", 2); break;
            case PASSWORD_ATTEMPT_PRE_CHANGE:
             LCD_send_string("atual: ", 2); break;
          }
        } else {
          LCD_send_data(key); // Imprime tecla pressionada
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
          LCD_send_string("Senha correta.", 1);
          LCD_clear_line(2);
          switch(keypad_state){
            case CHECK_PASSWORD_PRE_DEACTIVATE:              
              proceed = true;
              keypad_state = DEACTIVATE;
              break;
            case CHECK_PASSWORD_PRE_CHANGE:
              LCD_send_string("Insira nova", 1);
              LCD_send_string("senha: ", 2);
              digitalWrite(LED, HIGH);
              keypad_state = CHANGE_PASSWORD; break;
          }
        } else {
          LCD_send_string("Senha incorreta.", 1);
          LCD_clear_line(2);
          keypad_state = STANDBY;
        }
        break;        
     /*------------------ DEACTIVATE -------------------*
     * Desativar alame.                                 *
     *--------------------------------------------------*/
      case DEACTIVATE:
        LCD_send_string("Alarme", 1);
        LCD_send_string("desativado.", 2);
        alarm_state = ALARM_OFF;
        digitalWrite(LASER, LOW);
        noTone(BUZZER);
        keypad_state = STANDBY;
        break;

      /*--------------- CHANGE_PASSWORD -----------------*
       * Digitando nova senha.                           *
       *-------------------------------------------------*/        
      case CHANGE_PASSWORD:
        if(key == '#'){ // Terminou de digitar a senha
          LCD_send_string("Senha alterada.", 1);
          LCD_clear_line(2);
          digitalWrite(LED, LOW);
          password[nth_digit] = 0; // Finaliza string
          EEPROM_putString(PASSWORD_ADDRESS, password); // Guarda senha na memória
          keypad_state = STANDBY;
        } else if (key == '*'){ // Reinicia inserção de senha
            nth_digit = 0;
            LCD_clear_line(2);
            LCD_send_string("senha: ", 2);
        } else {
          LCD_send_data(key);
          password[nth_digit++] = key;
        }
        break;
    }
    key = 0;
  }

  // Verifica se houve violação do sistema de segurança
  if(alarm_state == ALARM_ON && LDR_state == DARK){
    alarm_state = TRIGGERED;
    LCD_send_string("! ! ! ! ! ! ! !", 1);
    LCD_clear_line(2);
    tone(BUZZER, 440);    
  }
}

/*-------------------------------------------------------------------------
 * -----------------------    KEYPAD_SWEEP   ------------------------------
 *------------------------------------------------------------------------- */
// Faz a varredura do teclado para verificar se alguma tecla foi pressionada
char KEYPAD_sweep(){
  int i,j;
  for(i=0;i<4;i++){
    digitalWrite(L[i], LOW);
    for(j=0;j<3;j++){
      if(digitalRead(C[j]) == LOW){
        digitalWrite(L[i], HIGH);
        return keypad[i][j];
      }
    }
    digitalWrite(L[i], HIGH);
  }
  return 0;  
}

/*-------------------------------------------------------------------------
 * ------------------------    STR_CMP   ----------------------------------
 *------------------------------------------------------------------------- */
// Rotina auxiliar para comparacao de strings
// Retorna 1 se forem iguais e 0 caso contrário
boolean str_cmp(char *s1, char *s2) {
  uint8_t i, len;
  if ((len = strlen(s1)) != strlen(s2)) return 0;
  for (i=0; i<len; i++) {
    if (s1[i] != s2[i]) return 0;
    if (s1[i] == '\0') return 1;
  }
  return 1;
}

/*-------------------------------------------------------------------------
 * -------------------------    EEPROM   ----------------------------------
 *------------------------------------------------------------------------- */
// Lê senha no endereço <address> da EEPROM e guarda em <string>
// Retorna 1 caso tenha senha e 0 caso contrário
boolean EEPROM_getString(int address, char* string){
  uint8_t i = 0;
  while((string[i++] = EEPROM.read(address++)) && i < PASS_SIZE);
  if(i == PASS_SIZE) return 0;
  else return 1;
}

// Insere <string> no endereço <address> da EEPROM
void EEPROM_putString(int address, char* string){
  uint8_t i = 0;
  while(string[i]) EEPROM.write(address++, string[i++]);  
  EEPROM.write(address, 0); // Finaliza string
}

/*-------------------------------------------------------------------------
 * ------------------------   ISR_TIMER   ---------------------------------
 *------------------------------------------------------------------------- */
 // Rotina periódica de interrupção que verifica
 // estado do fotoresistor e do teclado matricial
void ISR_timer() {
  /*-------------------------------------------------*
  * LEITURA DO SENSOR DE LUZ                         *
  *--------------------------------------------------*/
  if(alarm_state == ALARM_ON){
    if(analogRead(LDR) < LIGHT_THRESHOLD) LDR_state = DARK;
    else LDR_state = BRIGHT;
  }

  /*-------------------------------------------------*
  * LEITURA DO TECLADO MATRICIAL                     *
  *--------------------------------------------------*/
  key = KEYPAD_sweep();
  
  // Caso a leitura não seja mesma da anterior
  if(key != prev_key) key_debounce = false;    // Não há necessidade de debouncing
  prev_key = key;
  
  // Caso em debouncing
  if(key_debounce){
    key_debounce_time++;
    key = 0;
  } else if(key){     // Caso algum botão tenha sido pressionado
      key_debounce_time = 0;    // Entre em estado de debouncing
      key_debounce = true;
  }
  
  // Caso tempo de debouncing tenha terminado
  if(key_debounce_time >= 100) key_debounce = false;
}

/*-------------------------------------------------------------------------
 * --------------------------  LCD_SPI   ----------------------------------
 *------------------------------------------------------------------------- */
// Envia um pulso de enable ao LCD e espera por <t> microsegundos
void LCD_En_Wait(uint8_t t) {
  digitalWrite(CE, LOW);
  digitalWrite(CE, HIGH);
  delayMicroseconds(t); 
}

// Chaveia envio de bytes ao LCD como de comando (0) ou dados (1)
void LCD_RS(uint8_t select) {
  digitalWrite(DC, select);
}

// Envia um dado <c> ao LCD
void LCD_send_data(char c) {
  LCD_RS(1);
  shiftOut(DIN, CLK, MSBFIRST, c);    // Envia serialmente para o Shift Register
  LCD_En_Wait(50);
}

// Envia um comando <c> a LCD com um tempo <t> microssegundos de processamento
void LCD_send_command(char c, uint8_t t) {
  LCD_RS(0);
  shiftOut(DIN, CLK, MSBFIRST, c);    // Envia serialmente para o Shift Register
  LCD_En_Wait(t);
}

// Inicializa o LCD
void LCD_init(void) {
  pinMode(CLK, OUTPUT);
  pinMode(DIN, OUTPUT);
  pinMode(DC, OUTPUT);
  pinMode(CE, OUTPUT);    

  digitalWrite(CE, HIGH); // LCD Disabled

  struct lcd {
    uint8_t code;
    uint8_t wait;
  } init_LCD[4];

  // Tempos de processamento abaixo foram retirados do datasheet do LCD
  init_LCD[0].code = LCD_FUNCTION_SET;
  init_LCD[0].wait = 40;
  init_LCD[1].code = LCD_DISPLAY_CONTROL;
  init_LCD[1].wait = 40;
  init_LCD[2].code = LCD_DISPLAY_CLEAR;
  init_LCD[2].wait = 1530;
  init_LCD[3].code = LCD_ENTRY_MODE_SET;
  init_LCD[3].wait = 40;

  for(int k = 0; k < 4; k++) {  
    LCD_send_command(init_LCD[k].code, init_LCD[k].wait); 
  } 

  delayMicroseconds(40000);
}

// Enviar um string <s> na linha <line> (1 ou 2) do teclado
void LCD_send_string(char * s, uint8_t line) {
  LCD_clear_line(line); // Limpa linha
  LCD_position_cursor(line, 1); // Posiciona o cursor no começo da linha
  while (*s) {                   
    LCD_send_data(*s);  
    s++; 
  }  
}

// Limpa linha <line> (1 ou 2)
void LCD_clear_line(uint8_t line) {
  LCD_position_cursor(line, 1); // Posiciona o cursor no começo da linha
  int i = 16;
  while (i--) LCD_send_data(' '); 
}

// Posiciona o cursor na tela
// Sendo <linha> 1 ou 2 e <coluna> entre valores de 1 a 16
void LCD_position_cursor(int linha, int coluna){
  LCD_send_command(0x80|(0x40*(linha-1)+coluna-1), 60);
}

// Limpa LCD
void LCD_clean(void) {
  LCD_send_command(LCD_DISPLAY_CLEAR,1600);
}
