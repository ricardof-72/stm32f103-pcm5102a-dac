Este projeto implementa a reprodução de áudio digital utilizando o microcontrolador **STM32F103C8T6 (Blue Pill)** e o módulo **DAC PCM5102A**. O envio de dados para o DAC é feito via **DMA em Buffer Circular**, garantindo a transmissão contínua dos dados de áudio sem sobrecarregar a CPU.

---

O sistema carrega os buffers de áudio em memória RAM e utiliza o controlador DMA do STM32 para transferir as amostras continuamente para o conversor digital-analógico (PCM5102A). 

- **Arquitetura de Transmissão:** DMA Circular (Half-Transfer & Transfer Complete Interruption)
- **Taxa de Amostragem / Resolução:** [Ex: 44.1 kHz, 16-bit Stereo]

---

```text
├── Core/
│   ├── Inc/               
│   └── Src/                
├── Drivers/               
├── PCM5102A_project.ioc   
└── STM32F103C8TX_FLASH.ld 

PCM5102A        STM32F103
---------------------------
  VIN --------->  3.3V
  GND --------->  GND
  LCK --------->  PB12
  DIN --------->  PB15
  BCK --------->  PB13
  SCK --------->  GND
 XSMT --------->  3.3V
  FMT --------->  3.3V