#define CLK	9
#define DIN	8 
#define DC	7 // RS
#define CE	6 // Enable (ativo baixo)

/*
 * Estrutura para representar a instrução do LCD e o seu tempo de processamento 
 */
typedef struct _lcd {
	uint8_t cop;
	uint8_t tempo;
} lcd;

#define GPIO_PIN(x)  ((1)<<(x)) ///< obtem o bit do pino x
#define LCD_FUNCTION_SET 0x38
#define LCD_DISPLAY_CONTROL 0x0C
#define LCD_DISPLAY_CLEAR 0x01
#define LCD_ENTRY_MODE_SET 0x06

void setup(){
	pinMode(CLK, OUTPUT);
	pinMode(DIN, OUTPUT);
	pinMode(DC, OUTPUT);
	pinMode(CE, OUTPUT);    

	digitalWrite(CE, HIGH); // LCD Disabled
}

/*!
 * \fn pulso (char p)
 * \brief Gera um pulso "Enable" de t*1us.
 * \param[in] p para LCD (p=0) e para Leds (p=1).
 */
void LCD_EN_Wait(uint8_t t) {
	digitalWrite(CE, HIGH);
	digitalWrite(CE, LOW);
	delayMicroseconds(t);	
}

/*!
 * \fn RS (uint8_t l)
 * \brief Envia ao LCD o sinal RS pelo pino PORTC[8].
 * \param[in] l valor do RS (0, byte de instru&ccedil;&atilde;o e 1, byte de dados).
 */
void RS(uint8_t l) {
	digitalWrite(DC, l);
}

/*!
 * \fn enviaLCD (char c)
 * \brief Envia ao LCD um byte pelos pinos PORTC[7:0]
 * \param[in] c caracter em ASCII.
 * \param[in] t tempo de processamento necess&aacute;rio.
 */
void enviaLCD(char c, uint8_t t) {
	shiftOut(DIN, CLK, MSBFIRST, c);
	LCD_EN_Wait(t);                      ///< dispara o pulso "Enable" do LCD
}

/*!
 * \fn inicLCD (void) 
 * \brief Inicializa o LCD com a sequ&ecirc;ncia de instru&ccedil;&otilde;es recomendada pelo fabricante
 */
void initLCD(void) {
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

	delayMicroseconds(40000);

	RS(0);                    ///< Seta o LCD no modo de instru&ccedil;&atilde;o
	for(k = 0; k < 4; k++) {  
		enviaLCD(init_LCD[k].cop, init_LCD[k].tempo);    ///< instru&ccedil;&atilde;o de inicializa&ccedil;&atilde;o
	}	
}

/*!
 * \fn mandaString (char *s)
 * \brief Envia uma string de caracteres.
 * \param[in] s endere&ccedil;o inicial da string.
 */
void mandaString(char * s) {
	RS(1);                          ///< Seta o LCD no modo de dados
	while (*s) {                    ///< enquanto o conte&uacute;do do endere&ccedil;o != 0
		enviaLCD(*s, 50);         	///< envia o byte
		s++;                        ///< incrementa o endere&ccedil;o
	}
}

/*!
 * \fn posicionaCursos (int linha, int coluna)
 * \brief Posiciona o cursor na tela
 * \param[in] linha Linha de 1 a 2
 * \param[in] coluna Coluna de 1 a 16
 */
void posicionaCursor(int linha, int coluna){
	RS(0);
	enviaLCD(0x80|(0x40*(linha-1)+coluna-1), 60);
}

/*!
 * \fn limpaLCD (void) 
 * \brief Envia a instrução "Clear Display" (0x01).
 */
void limpaLCD(void) {
  RS(0);                         ///< Seta o LCD no modo de instru&ccedil;&atilde;o
  enviaLCD(LCD_DISPLAY_CLEAR,1600);
}
