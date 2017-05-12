/* Protocolo de aplicacao - Implementacao usando rotina de interrupcao e
 *  controle.
 *
 *  Uso:
 *  - Computador envia (pelo terminal) uma mensagem:
 *  PING\n\n
 *  - MCU retorna (no terminal):
 *  PONG\n
 *
 *  Tiago F. Tavares
 *  GPL3.0 - 2017
 */

/* stdio.h contem rotinas para processamento de expressoes regulares */
#include <stdio.h>
#include <Wire.h> 
#include <TimerOne.h>

#define C1 7
#define C2 6
#define C3 5
#define L1 8
#define L2 2
#define L3 3
#define L4 4
#define LDR A0
#define LED 13
#define eeprom 0x50    //Address of 24C16 eeprom chip
#define AUTO_MEASURE_INTERVAL 200
#define LED_INTERVAL 200

char C[] = {C1, C2, C3};
char L[] = {L1, L2, L3, L4};
char keyboard[4][3]={{'1','2','3'},{'4','5','6'},{'7','8','9'},{'*','0','#'}}; 
char key, prev_key = 0;
unsigned int counter_key = 0;
unsigned int counter_LED = 0;
unsigned int counter_auto = 0;
byte key_wait = 0;
byte auto_measure_status = 0;
byte LED_status = 0;

byte readEEPROM(int deviceaddress, unsigned int eeaddress );
void writeEEPROM(int deviceaddress, unsigned int eeaddress, byte data );

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
int str_cmp(char *s1, char *s2, int len) {
  /* Compare two strings up to length len. Return 1 if they are
   *  equal, and 0 otherwise.
   */
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

void writeEEPROM(int deviceaddress, unsigned int eeaddress, byte data ) 
{
  Wire.beginTransmission(deviceaddress);
  Wire.write((int)(eeaddress));
  Wire.write(data);
  Wire.endTransmission();
 
  delay(5);
}
 
byte readEEPROM(int deviceaddress, unsigned int eeaddress ) 
{
  byte rdata = 0xFF;
 
  Wire.beginTransmission(deviceaddress);
  Wire.write((int)(eeaddress));
  Wire.endTransmission();
 
  Wire.requestFrom(deviceaddress,1);
 
  if (Wire.available()) rdata = Wire.read();
 
  return rdata;
}

void ISR_timer() {
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

  if(auto_measure_status) counter_auto++;
  if(LED_status) counter_LED++;
}

/* Funcoes internas ao void main() */

void setup() {
  /* Inicializacao */
  buffer_clean();
  flag_check_command = 0;
  unsigned int address = 0;
  
  Serial.begin(9600);
  Wire.begin(); 
  
  pinMode(LED, OUTPUT);
  int i;
  for(i=0;i<=3;i++){
    pinMode(L[i], OUTPUT);
    digitalWrite(L[i], HIGH);
  }
  for(i=0;i<=2;i++) pinMode(C[i], INPUT_PULLUP);
  
  Timer1.initialize(10000); // Interrupcao a cada 10ms
  Timer1.attachInterrupt(ISR_timer); // Associa a interrupcao periodica a funcao ISR_timer  
}


void loop() {
  int x, y;
  char out_buffer[50];
  int flag_write = 0;
  byte current_address;
  byte measured;

  /* A flag_check_command permite separar a recepcao de caracteres
   *  (vinculada a interrupca) da interpretacao de caracteres. Dessa forma,
   *  mantemos a rotina de interrupcao mais enxuta, enquanto o processo de
   *  interpretacao de comandos - mais lento - nao impede a recepcao de
   *  outros caracteres. Como o processo nao 'prende' a maquina, ele e chamado
   *  de nao-preemptivo.
   */

    
  if(key != 0){
    switch(key){
      case '1': 
        digitalWrite(LED, HIGH);
        LED_status = 1;
        counter_LED = 0;
        break;
      case '2': 
        current_address = readEEPROM(eeprom, 0) + 1;
        measured = map(analogRead(LDR), 0, 1023, 0, 255);
        writeEEPROM(eeprom, current_address, measured);
        writeEEPROM(eeprom, 0, current_address);
        sprintf(out_buffer, "Recorded %d at position %d\n", measured, current_address);
        flag_write = 1;
        break;
      case '3': 
        auto_measure_status = 1;
        counter_auto = AUTO_MEASURE_INTERVAL;
        break;
      case '4':
        auto_measure_status = 0;
    }
  }

  if(LED_status && counter_LED > LED_INTERVAL){
    LED_status = 0;
    digitalWrite(LED, LOW);
  }

  if(auto_measure_status && counter_auto > AUTO_MEASURE_INTERVAL){
    counter_auto = 0;
    current_address = readEEPROM(eeprom, 0) + 1;
    measured = map(analogRead(LDR), 0, 1023, 0, 255);
    writeEEPROM(eeprom, current_address, measured);
    writeEEPROM(eeprom, 0, current_address);
    sprintf(out_buffer, "Recorded %d at position %d\n", measured, current_address);
    flag_write = 1;
  }
  
  if (flag_check_command == 1) {
    if (str_cmp((char *)Buffer.data, "PING", 4)) {
      sprintf(out_buffer, "PONG\n");
      flag_write = 1;
    }

    else if (str_cmp((char*)Buffer.data, "ID", 2) ) {
      sprintf(out_buffer, "DATALOGGER 1\n");
      flag_write = 1;
    }
    
    else if (str_cmp((char*)Buffer.data, "MEASURE", 7) ) {
      sprintf(out_buffer, "%d\n", map(analogRead(LDR), 0, 1023, 0, 255));
      flag_write = 1;
      //Serial.println(analogRead(LDR));
    }
    
    else if (str_cmp((char*)Buffer.data, "RESET", 5) ){
      writeEEPROM(eeprom, 0, 0);
      sprintf(out_buffer, "RESET\n");
      flag_write = 1;
    }
    
    else if (str_cmp((char*)Buffer.data, "MEMSTATUS", 9) ){
      sprintf(out_buffer, "%d\n", readEEPROM(eeprom, 0));
      flag_write = 1;
    }
    
    else if (str_cmp((char*)Buffer.data, "RECORD", 6) ){
      current_address = readEEPROM(eeprom, 0) + 1;
      measured = map(analogRead(LDR), 0, 1023, 0, 255);
      writeEEPROM(eeprom, current_address, measured);
      writeEEPROM(eeprom, 0, current_address);
      sprintf(out_buffer, "Recorded %d at position %d\n", measured, current_address);
      flag_write = 1;
    }
    
    else if (str_cmp((char*)Buffer.data, "GET", 3) ){
      int N = getInt((char*)Buffer.data);
      if(N > readEEPROM(eeprom, 0)) sprintf(out_buffer, "Unavailable position\n"); 
      else sprintf(out_buffer, "%d\n", readEEPROM(eeprom, N));
      flag_write = 1;
    }
    
    else buffer_clean();

    flag_check_command = 0;
  }

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
