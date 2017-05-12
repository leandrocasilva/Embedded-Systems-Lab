/* Datalogger
 *
 *  Igor Alves Maronni        155755
 *  Leandro Cavalcanti Silva  156176
 */

#include <stdio.h>
#include <Wire.h> 
#include <TimerOne.h>

// Macros do Teclado Matricial
#define C1 7
#define C2 6
#define C3 5
#define L1 8
#define L2 2
#define L3 3
#define L4 4

#define LDR A0  // Entrada analógica do sensor de luz
#define LED 13  // Led usado para indicar bom funcionamento

#define eeprom 0x50    // Endereço da primeira página da EEPROM 24C16

#define AUTO_MEASURE_INTERVAL 200   // Intervalo de tempo das medições automáticas (x10ms)
#define LED_INTERVAL 200            // Período de piscada do led (x10ms)

char C[] = {C1, C2, C3};      // Pinos das colunas do teclado matricial
char L[] = {L1, L2, L3, L4};  // Linhas das colunas do teclado matricial
char keyboard[4][3]={{'1','2','3'},{'4','5','6'},{'7','8','9'},{'*','0','#'}}; 

char key, prev_key = 0;   // Leituras atual e anterior do teclado matricial

// Contadores
unsigned int counter_key = 0; // Teclado matricial
unsigned int counter_LED = 0; // LED
unsigned int counter_auto = 0;// Medição automática

// Flags de estado
byte debouncing_status = 0;  // Debouncing do teclado matricial
byte auto_measure_status = 0; // Estado da medição automática
byte LED_status = 0;  // Estado do LED
volatile int flag_check_command = 0;

// Protótipos das funções
byte readEEPROM(int deviceaddress, unsigned int eeaddress );
void writeEEPROM(int deviceaddress, unsigned int eeaddress, byte data );
char sweep();
int str_cmp(char *s1, char *s2, int len);
void buffer_clean();
int buffer_add(char c_in);
void serialEvent();
int getInt(char *s);
void recordLDRinEEPROM();
void ISR_timer();
void setup();
void loop();

// Varredura do teclado matricial
// @return Caractere do botão pressionado
char sweep(){
  int i,j;
  for(i=0;i<4;i++){
    digitalWrite(L[i], LOW);  // Abaixamos o nível de cada linha
    for(j=0;j<3;j++){
      if(digitalRead(C[j]) == LOW){ // Se a coluna estiver curto-circuitada com a linha
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
char out_buffer[50];

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

// Rotina de interrupção periódica
// @brief Três tarefas estão sendo realizadas nessa rotina:
//        varredura do teclado, contador das medições automátias e contador do tempo de piscada do LED
void ISR_timer() {
  /************************** VARREDURA DO TECLADO MATRICIAL **************************/
  key = sweep();
  
  // Caso a leitura não seja mesma da anterior
  if(key != prev_key) debouncing_status = 0;    // Não há necessidade de debouncing
  prev_key = key;
  
  // Caso em debouncing
  if(debouncing_status){
    counter_key++;
    key = 0;
  } else if(key){     // Caso algum botão tenha sido pressionado
      counter_key = 0;    // Entre em estado de debouncing
      debouncing_status = 1;
  }
  
  // Caso tempo de debouncing tenha terminado
  if(counter_key >= 100) debouncing_status = 0;
  
  /************************* CONTADOR DAS MEDIÇÕES AUTOMÁTICAS *************************/
  if(auto_measure_status) counter_auto++;
  
  /**************************** CONTADOR DA PISCADA DO LED *****************************/
  if(LED_status) counter_LED++;
}

// Extrai o número da instrução "GET N"
// @param s String contendo a instrução GET N
// @return O 'N' especificado como inteiro
int getInt(char *s){
  int number=0;
  byte i;
  for(i=4; s[i] != 0; i++){
    number += s[i]-'0';
    number *= 10;
  }
  return number/10;
}

// Escrita na EEPROM
// @param devicedaddress Endereço do escravo
// @param eeaddress Endereço da posição da memória a ser escrita
// @param data Dado de 1 byte a ser escrito
void writeEEPROM(int deviceaddress, unsigned int eeaddress, byte data ) 
{
  Wire.beginTransmission(deviceaddress);
  Wire.write((int)(eeaddress));
  Wire.write(data);
  Wire.endTransmission();
 
  delay(5);
}

// Leitura na EEPROM
// @param devicedaddress Endereço do escravo
// @param eeaddress Endereço da posição da memória a ser lida
// @return Byte lido
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

// Grava a leitura atual do LDR na próxima posição vazia da EEPROM
void recordLDRinEEPROM(){
  byte current_address; // Próxima posição livre da memória EEPROM 
  byte measured;        // Leitura do LDR
  
  current_address = readEEPROM(eeprom, 0) + 1; // Posição a ser sobreescrita
  measured = map(analogRead(LDR), 0, 1023, 0, 255); // Conversão da leitura do LDR para 1 byte
  
  writeEEPROM(eeprom, current_address, measured);
  writeEEPROM(eeprom, 0, current_address);
  sprintf(out_buffer, "Recorded %d at position %d\n", measured, current_address);
}

/* Funcoes internas ao void main() */

void setup() {
  // Inicializacao
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
  int flag_write = 0;

  /* A flag_check_command permite separar a recepcao de caracteres
   *  (vinculada a interrupca) da interpretacao de caracteres. Dessa forma,
   *  mantemos a rotina de interrupcao mais enxuta, enquanto o processo de
   *  interpretacao de comandos - mais lento - nao impede a recepcao de
   *  outros caracteres. Como o processo nao 'prende' a maquina, ele e chamado
   *  de nao-preemptivo.
   */

  // Caso o botão do teclado tenha sido pressionado    
  if(key != 0){
    switch(key){
      case '1':   // Pisca LED, indicando que o sistema está responsivo
        digitalWrite(LED, HIGH);
        LED_status = 1;
        counter_LED = 0;
        break;
      case '2':   // Realiza uma medição e grava o valor na memória
        recordLDRinEEPROM();
        flag_write = 1;
        break;
      case '3':   // Ativa modo de medição automática
        auto_measure_status = 1;
        counter_auto = AUTO_MEASURE_INTERVAL;
        break;
      case '4':   // Encerra modo de medição automática
        auto_measure_status = 0;
    }
  }

  // Caso esteja no modo de piscar o LED e o tempo de piscada terminou
  if(LED_status && counter_LED > LED_INTERVAL){
    LED_status = 0;
    digitalWrite(LED, LOW);
  }

  // Caso modo de medição automática esteja ativo e o intervalo entre as medições tenha
  if(auto_measure_status && counter_auto > AUTO_MEASURE_INTERVAL){
    counter_auto = 0;
    recordLDRinEEPROM();
    flag_write = 1;
  }
  
  // Se há uma instrução a ser lida da UART
  if (flag_check_command == 1) {
    // PING
    if (str_cmp((char *)Buffer.data, "PING", 4)) {
      sprintf(out_buffer, "PONG\n");
      flag_write = 1;
    }
    // String de identificação
    else if (str_cmp((char*)Buffer.data, "ID", 2) ) {
      sprintf(out_buffer, "DATALOGGER 1\n");
      flag_write = 1;
    }
    // Retorna o valor de uma medição sem gravar na memória
    else if (str_cmp((char*)Buffer.data, "MEASURE", 7) ) {
      sprintf(out_buffer, "%d\n", map(analogRead(LDR), 0, 1023, 0, 255));
      flag_write = 1;
      //Serial.println(analogRead(LDR));
    }
    // Número de elementos da memória
    else if (str_cmp((char*)Buffer.data, "RESET", 5) ){
      writeEEPROM(eeprom, 0, 0);
      sprintf(out_buffer, "RESET\n");
      flag_write = 1;
    }
    
    else if (str_cmp((char*)Buffer.data, "MEMSTATUS", 9) ){
      sprintf(out_buffer, "%d\n", readEEPROM(eeprom, 0));
      flag_write = 1;
    }
    // Número de elementos da memória
    else if (str_cmp((char*)Buffer.data, "RECORD", 6) ){
      recordLDRinEEPROM();
      flag_write = 1;
    }
    // Retorna o N-ésimo elemento da memória se disponível
    else if (str_cmp((char*)Buffer.data, "GET", 3) ){
      int N = getInt((char*)Buffer.data);
      if(N > readEEPROM(eeprom, 0)) sprintf(out_buffer, "Unavailable position\n"); 
      else sprintf(out_buffer, "%d\n", readEEPROM(eeprom, N));
      flag_write = 1;
    }
    
    else buffer_clean();

    flag_check_command = 0;
  }

  //  Flag_write é habilitada sempre que alguma outra
  //  funcionalidade criou uma requisicao por escrever o conteudo do buffer na
  //  saida UART.
  if (flag_write == 1) {
    Serial.write(out_buffer);
    buffer_clean();
    flag_write = 0;
  }

}
