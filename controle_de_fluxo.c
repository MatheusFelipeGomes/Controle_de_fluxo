#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>
#include "inc/ssd1306.h"
#include "hardware/i2c.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/pwm.h"
#include "ws2818b.pio.h"
#include "thingspeak_utils.h"  // Inclui as funções do ThingSpeak

// Definições dos pinos
#define BTN_A_PIN 5  // Botão A (entrada)
#define BTN_B_PIN 6  // Botão B (saída)
#define LED_PIN 7    // Pino da matriz de LEDs
#define BUZZER_PIN 10 // Pino do buzzer

#define OLED_WIDTH 128
#define OLED_HEIGHT 32
#define LED_COUNT 25  // Número de LEDs na matriz
#define MAX_PESSOAS 10   // Define o limite de pessoas

// Pinos do barramento I2C
const uint I2C_SDA = 14;
const uint I2C_SCL = 15;

// Estado do botão
int A_state = 0;
int B_state = 0;
int last_A_state = 1;  // Estado anterior do botão A
int last_B_state = 1;  // Estado anterior do botão B
unsigned long last_press_time_A = 0;  // Tempo da última pressão do botão A
unsigned long last_press_time_B = 0;  // Tempo da última pressão do botão B

// Contagem de pessoas
int num_pessoas = 0;  // Inicializa a quantidade de pessoas

// Área de renderização e buffer do OLED
struct render_area frame_area = {
    .start_column = 0,
    .end_column = ssd1306_width - 1,
    .start_page = 0,
    .end_page = ssd1306_n_pages - 1
};
uint8_t ssd[ssd1306_buffer_length];

// Definição de pixel GRB
struct pixel_t {
  uint8_t G, R, B; // Três valores de 8-bits compõem um pixel.
};
typedef struct pixel_t pixel_t;
typedef pixel_t npLED_t; // Mudança de nome de "struct pixel_t" para "npLED_t" por clareza.

// Declaração do buffer de pixels que formam a matriz.
npLED_t leds[LED_COUNT];

// Variáveis para uso da máquina PIO.
PIO np_pio;
uint sm;

// Função para inicializar o display OLED
void ConfigureDisplay() {
    stdio_init_all();  // Inicializa as interfaces padrão
    i2c_init(i2c1, ssd1306_i2c_clock * 1000);  // Inicializa o barramento I2C
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    // Inicializa o display OLED
    ssd1306_init();
    calculate_render_area_buffer_length(&frame_area);

    // Zera o buffer do display
    memset(ssd, 0, ssd1306_buffer_length);
    render_on_display(ssd, &frame_area);
}

// Função para desenhar uma string ocupando todo o display
void draw_full_text(char* text) {
    int y = 0;  // Começa a partir do topo do display
    int text_length = strlen(text);  // Tamanho do texto

    // Calcular a largura do texto para ajustar na tela
    int char_width = 6;  // Largura aproximada por caractere (ajustável conforme a fonte)
    int max_chars_per_line = OLED_WIDTH / char_width;

    // Se o texto for maior que o espaço disponível, ajusta a exibição
    if (text_length > max_chars_per_line) {
        // Divide o texto em linhas
        int lines = (text_length / max_chars_per_line) + 1;
        for (int line = 0; line < lines; line++) {
            int start_index = line * max_chars_per_line;
            int end_index = start_index + max_chars_per_line;

            // Copia a parte do texto que cabe em cada linha
            char line_text[max_chars_per_line + 1];
            strncpy(line_text, text + start_index, max_chars_per_line);
            line_text[max_chars_per_line] = '\0';  // Garante que a string tenha o formato correto

            // Desenha o texto na linha correspondente
            ssd1306_draw_string(ssd, 0, y, line_text);
            render_on_display(ssd, &frame_area);

            // Incrementa a posição no eixo Y para a próxima linha
            y += 8;  // Ajuste o valor para controlar o espaçamento vertical entre as linhas
        }
    } else {
        // Se o texto for pequeno, desenha tudo em uma linha
        ssd1306_draw_string(ssd, 0, y, text);
        render_on_display(ssd, &frame_area);
    }
}

// Função para atualizar a quantidade de pessoas no display
void atualizar_display() {
    char texto[20];
    snprintf(texto, sizeof(texto), "Pessoas: %d  ", num_pessoas);
    draw_full_text(texto);
}

/**
 * Inicializa a máquina PIO para controle da matriz de LEDs.
 */
void npInit(uint pin) {
  uint offset = pio_add_program(pio0, &ws2818b_program);
  np_pio = pio0;
  sm = pio_claim_unused_sm(np_pio, false);
  if (sm < 0) {
    np_pio = pio1;
    sm = pio_claim_unused_sm(np_pio, true);
  }
  ws2818b_program_init(np_pio, sm, offset, pin, 800000.f);
  for (uint i = 0; i < LED_COUNT; ++i) {
    leds[i].R = 0;
    leds[i].G = 0;
    leds[i].B = 0;
  }
}

/**
 * Atribui uma cor RGB a um LED.
 */
void npSetLED(const uint index, const uint8_t r, const uint8_t g, const uint8_t b) {
  leds[index].R = r;
  leds[index].G = g;
  leds[index].B = b;
}

/**
 * Limpa o buffer de pixels.
 */
void npClear() {
  for (uint i = 0; i < LED_COUNT; ++i)
    npSetLED(i, 0, 0, 0);
}

/**
 * Escreve os dados do buffer nos LEDs.
 */
void npWrite() {
  for (uint i = 0; i < LED_COUNT; ++i) {
    pio_sm_put_blocking(np_pio, sm, leds[i].G);
    pio_sm_put_blocking(np_pio, sm, leds[i].R);
    pio_sm_put_blocking(np_pio, sm, leds[i].B);
  }
  sleep_us(100);
}

// Converte posição da matriz para vetor
int getIndex(int x, int y) {
  if (y % 2 == 0) {
    return 24 - (y * 5 + x);
  } else {
    return 24 - (y * 5 + (4 - x));
  }
}

/**
 * Inicializa o buzzer no pino BUZZER_PIN.
 */
void buzzer_init() {
    gpio_set_function(BUZZER_PIN, GPIO_FUNC_PWM);
    uint slice_num = pwm_gpio_to_slice_num(BUZZER_PIN);
    pwm_set_enabled(slice_num, false);
}

/**
 * Liga o buzzer com uma frequência específica.
 */
void buzzer_on(int frequency) {
    uint slice_num = pwm_gpio_to_slice_num(BUZZER_PIN);
    pwm_set_clkdiv(slice_num, 4.0);
    pwm_set_wrap(slice_num, clock_get_hz(clk_sys) / (4 * frequency) - 1);
    uint16_t pwm_wrap = pwm_hw->slice[slice_num].top; // Alternativa para pwm_get_wrap()
    pwm_set_gpio_level(BUZZER_PIN, pwm_wrap / 2);
    pwm_set_enabled(slice_num, true);
}

/**
 * Desliga o buzzer.
 */
void buzzer_off() {
    uint slice_num = pwm_gpio_to_slice_num(BUZZER_PIN);
    pwm_set_enabled(slice_num, false);
}

// Função para atualizar a matriz de LEDs com base no número de pessoas
void atualizar_matriz_leds() {
    if (num_pessoas < MAX_PESSOAS) {
        // Matriz Verde
        int matriz[5][5][3] = {
            {{0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}}, 
            {{0,255,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,255,0}}, 
            {{0,0,0}, {0,255,0}, {0,0,0}, {0,255,0}, {0,0,0}}, 
            {{0,0,0}, {0,0,0}, {0,255,0}, {0,0,0}, {0,0,0}}, 
            {{0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}}
        };

        for(int linha = 0; linha < 5; linha++) {
            for(int coluna = 0; coluna < 5; coluna++) {
                int posicao = getIndex(linha, coluna);
                npSetLED(posicao, matriz[coluna][linha][0], matriz[coluna][linha][1], matriz[coluna][linha][2]);
            }
        }
        npWrite();
    } else {
        // Matriz Vermelha
        int matriz2[5][5][3] = {
            {{255,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {255,0,0}}, 
            {{0,0,0}, {255,0,0}, {0,0,0}, {255,0,0}, {0,0,0}}, 
            {{0,0,0}, {0,0,0}, {255,0,0}, {0,0,0}, {0,0,0}}, 
            {{0,0,0}, {255,0,0}, {0,0,0}, {255,0,0}, {0,0,0}}, 
            {{255,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {255,0,0}}
        };

        for(int linha = 0; linha < 5; linha++) {
            for(int coluna = 0; coluna < 5; coluna++) {
                int posicao = getIndex(linha, coluna);
                npSetLED(posicao, matriz2[coluna][linha][0], matriz2[coluna][linha][1], matriz2[coluna][linha][2]);
            }
        }
        npWrite();

        // Liga o buzzer ao exibir a matriz vermelha
        buzzer_on(1000);  // Toca em 1 kHz
        sleep_ms(500);
        buzzer_off();  // Desliga o buzzer
    }
}
unsigned long last_send_time = 0;  // Variável para controlar o intervalo de envio


void enviar_se_necessario() {
    unsigned long current_time = time_us_32();  // Obtém o tempo atual

    // Envia dados apenas se passaram 5 segundos desde o último envio
    if (current_time - last_send_time > 60000000) {  // 1 minuto
        enviar_dados_thingspeak(num_pessoas);  // Função para enviar os dados
        last_send_time = current_time;  // Atualiza o tempo do último envio
    }
}

int main() {
    // Inicializando os pinos de botão
    gpio_init(BTN_A_PIN);
    gpio_set_dir(BTN_A_PIN, GPIO_IN);
    gpio_pull_up(BTN_A_PIN);

    gpio_init(BTN_B_PIN);
    gpio_set_dir(BTN_B_PIN, GPIO_IN);
    gpio_pull_up(BTN_B_PIN);

    // Inicializa o OLED
    ConfigureDisplay();

    // Inicializa a matriz de LEDs e o buzzer
    npInit(LED_PIN);
    buzzer_init();

    // Exibe a quantidade inicial de pessoas
    atualizar_display();
    atualizar_matriz_leds();

    while (true) {
        // Obtém o estado atual dos botões
        A_state = !gpio_get(BTN_A_PIN);  // Se o botão A foi pressionado (inversão de lógica)
        B_state = !gpio_get(BTN_B_PIN);  // Se o botão B foi pressionado (inversão de lógica)

        // Verifica debounce para o botão A (entrada)
        if (A_state && last_A_state == 1 && (time_us_32() - last_press_time_A) > 200000) {  // Tempo de debounce de 200ms
            last_press_time_A = time_us_32();
            if (num_pessoas < MAX_PESSOAS) {  // Limita a contagem a 10 pessoas
                num_pessoas++;  // Incrementa a quantidade de pessoas
                atualizar_display();  // Atualiza o display
                atualizar_matriz_leds();  // Atualiza a matriz de LEDs
            }
        }

        // Verifica debounce para o botão B (saída)
        if (B_state && last_B_state == 1 && (time_us_32() - last_press_time_B) > 200000) {  // Tempo de debounce de 200ms
            last_press_time_B = time_us_32();
            if (num_pessoas == MAX_PESSOAS) {  // Se a contagem for 10, volta para 9
                num_pessoas = 9;  // Define o número de pessoas para 9
                atualizar_display();  // Atualiza o display
                atualizar_matriz_leds();  // Atualiza a matriz de LEDs
            } else if (num_pessoas > 0) {  // Garante que o contador não fique negativo
                num_pessoas--;  // Decrementa a quantidade de pessoas
                atualizar_display();  // Atualiza o display
                atualizar_matriz_leds();  // Atualiza a matriz de LEDs
            }
        }
       
        // Envia dados para o ThingSpeak com uso do time ( para não travar as condições anteriores)
        enviar_se_necessario();
        sleep_ms(200000);
        
        atualizar_display(); // Atualiza o display


        // Atualiza o estado anterior
        last_A_state = A_state;
        last_B_state = B_state;

        sleep_ms(100);  // Pequeno delay para evitar sobrecarga no loop
    }

    return 0;
}