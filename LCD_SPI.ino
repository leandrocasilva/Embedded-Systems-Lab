
#include "atrasos.h"
#include "lcdled.h"

/**
* \brief Inicializa como GPIO A12, B18, B19, C0-10 e D1 
*/
void initLcdledGPIO(void) {
	SIM_SCGC5 |= SIM_SCGC5_PORTC_MASK ;     ///< Habilita os clocks dos módulos

	/*! Configura os pinos PORTC[10:0] como GPIO */
	PORTC_PCR0 |= PORT_PCR_MUX(1);       ///< D0-D7 dos dados (GPIO)
	PORTC_PCR1 |= PORT_PCR_MUX(1);
	PORTC_PCR2 |= PORT_PCR_MUX(1);
	PORTC_PCR3 |= PORT_PCR_MUX(1);
	PORTC_PCR4 |= PORT_PCR_MUX(1);
	PORTC_PCR5 |= PORT_PCR_MUX(1);
	PORTC_PCR6 |= PORT_PCR_MUX(1);
	PORTC_PCR7 |= PORT_PCR_MUX(1);
	PORTC_PCR8 |= PORT_PCR_MUX(1);        ///< RS do LCD
	PORTC_PCR9 |= PORT_PCR_MUX(1);        ///< E do LCD
	PORTC_PCR10 |= PORT_PCR_MUX(1);       ///< /LE do latch


	/*! Configura os pinos PORTC[10:0] como de sa&iacute;da */
	GPIOC_PDDR |= GPIO_PDDR_PDD(GPIO_PIN(10) | GPIO_PIN(9) | GPIO_PIN(8) |GPIO_PIN(7) | 
			GPIO_PIN(6) | GPIO_PIN(5) | GPIO_PIN(4) | GPIO_PIN(3) | 
			GPIO_PIN(2) | GPIO_PIN(1) | GPIO_PIN(0));       

	/*! Inicializa os pinos com valor 0 */
	GPIOC_PDOR &= ~GPIO_PDOR_PDO(0xFF); 
}

/*!
 * \fn pulso (char p)
 * \brief Gera um pulso "Enable" de t*1us.
 * \param[in] p para LCD (p=0) e para Leds (p=1).
 */
void pulso(tipo_enable p, uint8_t t) {
	GPIOC_PSOR = GPIO_PIN (9 + p);     ///< Seta 1 no PORTC[9+p]
	GPIOC_PCOR = GPIO_PIN (9 + p);     ///< Limpa em 0 o PORTC[9+p]
	delay10us(t);                      ///< mant&eacute;m aprox. t*10us
	
}

/*!
 * \fn RS (uint8_t l)
 * \brief Envia ao LCD o sinal RS pelo pino PORTC[8].
 * \param[in] l valor do RS (0, byte de instru&ccedil;&atilde;o e 1, byte de dados).
 */
void RS(uint8_t l) {
	if(l) {                      ///< (l != 0)
		GPIOC_PSOR = GPIO_PIN(8);    ///< Seta o LCD no modo de dados
	} else {   
		GPIOC_PCOR = GPIO_PIN(8);     ///< Seta o LCD no modo de instru&ccedil;&atilde;o
	}
}

/*!
 * \fn enviaLCD (char c)
 * \brief Envia ao LCD um byte pelos pinos PORTC[7:0]
 * \param[in] c caracter em ASCII.
 * \param[in] t tempo de processamento necess&aacute;rio.
 */
void enviaLCD(char c, uint8_t t) {
	GPIOC_PCOR = 0x000000FF;      ///< limpa em 0 PORTC[7:0]
	GPIOC_PSOR = (unsigned int)c; ///< Seta os bits que devem ser setados
	pulso(0, t);                      ///< dispara o pulso "Enable" do LCD
}

/*!
 * \fn enviaLed (char c)
 * \brief Envia ao LED um byte pelos pinos PORTC[7:0]
 * \param[in] c caracter em ASCII.
 * \param[in] t tempo de processamento necess&aacute;rio.
 */
void enviaLed(char c) {
	GPIOC_PCOR = 0x000000FF;      ///< limpa em 0 PORTC[7:0]
	GPIOC_PSOR = (unsigned int)c; ///< Seta os bits que devem ser setados
	pulso(1, 1);               ///< dispara o pulso "Enable" do Latch 74573
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
    init_LCD[0].tempo = 4;
    init_LCD[1].cop = LCD_DISPLAY_CONTROL;
    init_LCD[1].tempo = 4;
    init_LCD[2].cop = LCD_DISPLAY_CLEAR;
    init_LCD[2].tempo = 153;
    init_LCD[3].cop = LCD_ENTRY_MODE_SET;
    init_LCD[3].tempo = 4;
    
    delay10us(4000);
	
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
		enviaLCD(*s, 5);         	///< envia o byte
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
	enviaLCD(0x80|(0x40*(linha-1)+coluna-1), 6);
}

/*!
 * \fn limpaLCD (void) 
 * \brief Envia a instrução "Clear Display" (0x01).
 */
void limpaLCD(void) {
  RS(0);                         ///< Seta o LCD no modo de instru&ccedil;&atilde;o
  enviaLCD(LCD_DISPLAY_CLEAR,160);
}

/*!
 * \fn criaBitmapI (uint8_t end)
 * \brief Cria um bitmap "&iacute;"
 * \param[in] end endere&ccedil;o onde o bitmap &eacute; gravado
 * \param[in] bitmap matriz do bitmap
 */
void criaBitmap(uint8_t end, uint8_t *bitmap){
	RS(0);
	enviaLCD(0x40|end,4);		///< define o endere&ccedil;&atilde;o em CGRAM

	RS(1); ///< seta para dados
	enviaLCD(bitmap[0], 4); ///< carrega a linha 1 
	enviaLCD(bitmap[1], 4); ///< carrega a linha 2 
	enviaLCD(bitmap[2], 4); ///< carrega a linha 3 
	enviaLCD(bitmap[3], 4); ///< carrega a linha 4 
	enviaLCD(bitmap[4], 4); ///< carrega a linha 5 
	enviaLCD(bitmap[5], 4); ///< carrega a linha 6
	enviaLCD(bitmap[6], 4); ///< carrega a linha 7 
	enviaLCD(bitmap[7], 4); ///< carrega a linha 8 
}
