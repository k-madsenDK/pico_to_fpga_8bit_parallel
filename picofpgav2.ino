#include <Arduino.h>
#include "hardware/pio.h"
#include "hardware/gpio.h"
#include "bus_protocol.pio.h"

#define MODE_MASTER false
#define MODE_SLAVE  true

#define BUS_WRITE   false
#define BUS_READ    true


class Bushandling {
private:
    static constexpr uint PIN_D0    = 2;
    static constexpr uint PIN_CLK   = 10;
    static constexpr uint PIN_DIR   = 11;
    static constexpr uint PIN_EMPTY = 12;
    static constexpr uint PIN_FULL  = 13;

    static constexpr uint BUS_WIDTH = 8;
    static constexpr uint32_t CLK_MASK = 1u << PIN_CLK;

    bool _isSlave;
    bool _begun = false;

    bool _readMode = BUS_WRITE;
    bool _directionValid = false;

    PIO _pio = pio0;
    uint _smTx = 0;
    uint _smRx = 1;

    uint _txOffset = 0;
    uint _rxOffset = 0;

    void clearStateMachines() {
        pio_sm_set_enabled(_pio, _smTx, false);
        pio_sm_set_enabled(_pio, _smRx, false);

        pio_sm_clear_fifos(_pio, _smTx);
        pio_sm_clear_fifos(_pio, _smRx);

        pio_sm_restart(_pio, _smTx);
        pio_sm_restart(_pio, _smRx);
    }

    void waitTxDrain() {
        while (pio_sm_get_tx_fifo_level(_pio, _smTx) != 0) {
            tight_loop_contents();
        }

        // Giv sidste byte tid til at komme helt ud efter FIFO er tømt.
        //delayMicroseconds(1);
    }

public:
    Bushandling(bool mode) : _isSlave(mode) {
    }

    bool begin(PIO pioInstance = pio0, uint smTx = 0, uint smRx = 1, float clkdiv = 200.0f) {
        if (_isSlave) {
            // Denne version er til Pico som master mod FPGA.
            return false;
        }

        _pio = pioInstance;
        _smTx = smTx;
        _smRx = smRx;

        // GP2-GP9 + GP10 styres af PIO.
        for (uint pin = PIN_D0; pin < PIN_D0 + BUS_WIDTH; pin++) {
            pio_gpio_init(_pio, pin);
        }
        pio_gpio_init(_pio, PIN_CLK);

        // GP11-GP13 styres/læses af normal GPIO.
        gpio_init(PIN_DIR);
        gpio_set_dir(PIN_DIR, GPIO_OUT);
        gpio_put(PIN_DIR, BUS_WRITE);

        gpio_init(PIN_EMPTY);
        gpio_set_dir(PIN_EMPTY, GPIO_IN);

        gpio_init(PIN_FULL);
        gpio_set_dir(PIN_FULL, GPIO_IN);

        _txOffset = pio_add_program(_pio, &bus_master_tx_program);
        _rxOffset = pio_add_program(_pio, &bus_master_rx_program);

        // -------------------------
        // TX state machine
        // -------------------------
        {
            pio_sm_config c = bus_master_tx_program_get_default_config(_txOffset);

            sm_config_set_out_pins(&c, PIN_D0, BUS_WIDTH);
            sm_config_set_set_pins(&c, PIN_CLK, 1);

            // CPU skriver byte i nederste 8 bit.
            sm_config_set_out_shift(&c, true, false, 32);

            // Brug hele FIFO som TX FIFO.
            sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);

            // Debug langsomt. Senere kan denne sættes til 2.0, 1.2, 1.0 osv.
            sm_config_set_clkdiv(&c, clkdiv);

            pio_sm_init(_pio, _smTx, _txOffset, &c);

            pio_sm_set_consecutive_pindirs(_pio, _smTx, PIN_D0, BUS_WIDTH, true);
            pio_sm_set_consecutive_pindirs(_pio, _smTx, PIN_CLK, 1, true);
        }

        // -------------------------
        // RX state machine
        // -------------------------
        {
    pio_sm_config c = bus_master_rx_program_get_default_config(_rxOffset);

    sm_config_set_in_pins(&c, PIN_D0);
    sm_config_set_set_pins(&c, PIN_CLK, 1);

    // shift_right = false:
    // "in pins, 8" lander i nederste 8 bit, så raw & 0xFF virker.
    sm_config_set_in_shift(&c, false, false, 32);

    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
    sm_config_set_clkdiv(&c, clkdiv);

    pio_sm_init(_pio, _smRx, _rxOffset, &c);

    // I read-mode skal GP2-GP9 være input.
    pio_sm_set_consecutive_pindirs(_pio, _smRx, PIN_D0, BUS_WIDTH, false);

    // GP10 clock drives af Pico.
    pio_sm_set_consecutive_pindirs(_pio, _smRx, PIN_CLK, 1, true);
}

        // Clock low.
        pio_sm_set_pins_with_mask(_pio, _smTx, 0, CLK_MASK);
        pio_sm_set_pins_with_mask(_pio, _smRx, 0, CLK_MASK);

        clearStateMachines();

        _begun = true;
        _directionValid = false;
        _readMode = BUS_WRITE;

        // Første gang skal TX state machine startes.
        setDirection(BUS_WRITE);

        return true;
    }

    bool isSlave() const {
        return _isSlave;
    }

void setDirection(bool readMode) {
    if (!_begun || _isSlave) {
        return;
    }

    if (_directionValid && readMode == _readMode) {
        return;
    }

    // Hvis vi forlader write-mode, skal sidste TX-byte være ude.
    if (_directionValid && _readMode == BUS_WRITE) {
        waitTxDrain();
    }

    clearStateMachines();

    if (readMode == BUS_READ) {
        // --------------------------------------------------
        // READ MODE:
        // GP11 = HIGH
        // Pico data bus GP2-GP9 skal være INPUT hele tiden.
        // FPGA driver databussen.
        // --------------------------------------------------

        // Slip databussen FØR FPGA får besked om at drive den.
        pio_sm_set_consecutive_pindirs(_pio, _smTx, PIN_D0, BUS_WIDTH, false);
        pio_sm_set_consecutive_pindirs(_pio, _smRx, PIN_D0, BUS_WIDTH, false);

        // Clock skal stadig drives af Pico.
        pio_sm_set_consecutive_pindirs(_pio, _smRx, PIN_CLK, 1, true);

        // Sørg for clock LOW før read starter.
        pio_sm_set_pins_with_mask(_pio, _smRx, 0, CLK_MASK);

        // Fortæl FPGA: Pico vil læse, FPGA skal drive bus.
        gpio_put(PIN_DIR, HIGH);

        // Giv FPGA tid til at skifte busretning og lægge første byte ud.
        delayMicroseconds(2);

        // Start RX PIO. Pico-data bus forbliver INPUT.
        pio_sm_set_enabled(_pio, _smRx, true);
    } else {
        // --------------------------------------------------
        // WRITE MODE:
        // GP11 = LOW
        // Pico driver databussen.
        // FPGA lytter.
        // --------------------------------------------------

        // Fortæl FPGA at den skal slippe databussen.
        gpio_put(PIN_DIR, LOW);

        // Giv FPGA tid til at skifte bus til input/high-Z.
        delayMicroseconds(2);

        // Clock LOW før TX starter.
        pio_sm_set_pins_with_mask(_pio, _smTx, 0, CLK_MASK);

        // Pico overtager databussen.
        pio_sm_set_consecutive_pindirs(_pio, _smTx, PIN_D0, BUS_WIDTH, true);
        pio_sm_set_consecutive_pindirs(_pio, _smTx, PIN_CLK, 1, true);

        // Start TX PIO.
        pio_sm_set_enabled(_pio, _smTx, true);
    }

    _readMode = readMode;
    _directionValid = true;
}

    bool masterCanWrite() {
        return gpio_get(PIN_FULL) == LOW;
    }

    bool masterCanRead() {
        if (!_begun || _isSlave) {
            return false;
        }

        if (pio_sm_get_rx_fifo_level(_pio, _smRx) > 0) {
            return true;
        }

        return gpio_get(PIN_EMPTY) == LOW;
    }

    void masterWrite(uint8_t val) {
        if (!_begun || _isSlave) {
            return;
        }

        if (!_directionValid || _readMode != BUS_WRITE) {
            setDirection(BUS_WRITE);
        }

        // PIO-programmet venter selv på FULL = LOW.
        pio_sm_put_blocking(_pio, _smTx, (uint32_t)val);
    }

    uint8_t masterRead() {
        if (!_begun || _isSlave) {
            return 0;
        }

        if (!_directionValid || _readMode != BUS_READ) {
            setDirection(BUS_READ);
        }

        // PIO-programmet venter selv på EMPTY = LOW, sampler data og clocker ACK.
        uint32_t raw = pio_sm_get_blocking(_pio, _smRx);

        return (uint8_t)(raw & 0xFF);
    }

    void flushWrite() {
        if (!_begun || _isSlave) {
            return;
        }

        if (_directionValid && _readMode == BUS_WRITE) {
            waitTxDrain();
        }
    }

    void debugPio() {
        Serial.print("PIO0 CTRL: 0x");
        Serial.println(pio0->ctrl, HEX);

        Serial.print("SM0 TX enabled: ");
        Serial.println((pio0->ctrl & (1u << _smTx)) ? 1 : 0);

        Serial.print("SM1 RX enabled: ");
        Serial.println((pio0->ctrl & (1u << _smRx)) ? 1 : 0);

        Serial.print("DIR GP11: ");
        Serial.println(gpio_get(PIN_DIR));

        Serial.print("EMPTY GP12: ");
        Serial.println(gpio_get(PIN_EMPTY));

        Serial.print("FULL GP13: ");
        Serial.println(gpio_get(PIN_FULL));

        Serial.print("TX FIFO level: ");
        Serial.println(pio_sm_get_tx_fifo_level(_pio, _smTx));

        Serial.print("RX FIFO level: ");
        Serial.println(pio_sm_get_rx_fifo_level(_pio, _smRx));
    }
};

// --- GLOBALE VARIABLER TIL TEST ---
uint32_t gennemloeb = 0;
uint32_t succes = 0;
uint32_t fejl = 0;

void setup(){
    Serial.begin(115200);
    delay(1000);
    Serial.println("Pico FPGA PIO bus test starter");
}

void loop(){
    Serial.print("Gennemløb: ");
    Serial.print(gennemloeb);
    Serial.print(" | Succes: ");
    Serial.print(succes);
    Serial.print(" | Fejl: ");
    Serial.println(fejl);
    delay(250);
}

Bushandling myBus(MODE_MASTER);

void setup1() {

    delay(2000);



    // Debug langsomt.
    // Når det virker, prøv 20.0f, 2.0f, 1.2f osv.
    if (!myBus.begin(pio0, 0, 1, 1.0f)) {
        Serial.println("Fejl: Denne version er master-only.");
        while (true) {
            delay(1000);
        }
    }

    myBus.setDirection(BUS_WRITE);
    myBus.debugPio();
}



void loop1() {
    gennemloeb++;                 // Tæl antal gennemløb op
    bool har_fejl = false;        // Flag til at spore fejl i det nuværende gennemløb
    uint8_t forventet_data = 0;   // Den byte vi forventer at læse
    uint8_t antal_sendt = 230;    // Hvor mange bytes vi sender

    // --- 1. MASTER SKRIVER TIL SLAVE ---
    myBus.setDirection(BUS_WRITE);
    
    for (uint8_t i = 0; i < antal_sendt; i++) {
        // Vi bruger 'while' i stedet for 'if' her for at tvinge Pico'en til at 
        // vente på, at FPGA'en er klar. Med et 'if' ville den bare skippe byten!
        while (!myBus.masterCanWrite()) { 
            // Vent...
        }
        myBus.masterWrite(i); // Sender 0, 1, 2... op til 229
    }

    // --- 2. MASTER LÆSER FRA SLAVE ---
    myBus.setDirection(BUS_READ);
    
    while (myBus.masterCanRead()) {
        uint8_t incoming = myBus.masterRead();
        
        // Tjek om den modtagne data er den vi forventede
        if (incoming != forventet_data) {
            har_fejl = true;
        }
        forventet_data++;
    }

    // Tjek om vi modtog præcis det antal bytes, vi sendte.
    // Hvis vi læste for få eller for mange, er det også en fejl.
    if (forventet_data != antal_sendt) {
        har_fejl = true;
    }

    // --- 3. OPDATER STATISTIK OG PRINT ---
    if (har_fejl) {
        fejl++;
    } else {
        succes++;
    }

}