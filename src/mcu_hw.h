#ifndef MCU_HW_H
#define MCU_HW_H

#define LOGO "\033[1;33m"\
  "  __  __ _ ___ _____             _  _               \r\n"\
  " |  \\/  (_) __|_   _|__ _ _ _  _| \\| |__ _ _ _  ___ \r\n"\
  " | |\\/| | \\__ \\ | |/ -_) '_| || | .` / _` | ' \\/ _ \\\r\n"\
  " |_|  |_|_|___/ |_|\\___|_|  \\_, |_|\\_\\__,_|_||_\\___/\r\n"\
  "                            |__/                    \r\n\033[0m"
  
void mcu_hw_init(void);
void mcu_hw_main_loop(void);

void mcu_hw_irq_ack(void);
void mcu_hw_reset(void);

// HW SPI interface
void mcu_hw_spi_begin(void);
unsigned char mcu_hw_spi_tx_u08(unsigned char b);
void mcu_hw_spi_end(void);

#endif // MCU_HW_H
