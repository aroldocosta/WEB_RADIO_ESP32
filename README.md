# Web Radio ESP32 📻

Um dispositivo autônomo de rádio via internet (Web Radio) baseado no microcontrolador ESP32 com estágio de áudio digital I2S, interface web responsiva embarcada (Single Page Application com Dial tátil) e suporte a atualizações remotas seguras (OTA A/B).

---

## 🛠️ Requisitos de Hardware

- **Microcontrolador**: ESP32 (Série WROVER ou S3) com **PSRAM externa obrigatória (mínimo 4MB)** e **Flash de 4MB**.
- **Saída de Áudio**: DAC / Amplificador I2S (ex: MAX98357A ou PCM5102).
- **Pinagem I2S Padrão**:
  - `BCLK`: GPIO 26
  - `LRCK / WS`: GPIO 25
  - `DOUT / DATA`: GPIO 22

---

## 🗺️ Mapa de Memória e Particionamento

O projeto utiliza um particionamento customizado (`partitions.csv`) para suporte a OTA duplo (A/B) e partição dedicada para o frontend embarcado:

| Partição | Tipo | Subtipo | Offset | Tamanho | Função |
|---|---|---|---|---|---|
| `nvs` | data | nvs | `0x9000` | 20 KB | Credenciais Wi-Fi e configurações |
| `otadata` | data | ota | `0xe000` | 8 KB | Ponteiro da partição ativa de boot |
| `app0` | app | ota_0 | `0x10000` | 1344 KB (~1.37 MB) | Slot Primário de Firmware |
| `app1` | app | ota_1 | `0x160000` | 1344 KB (~1.37 MB) | Slot Secundário de Firmware (OTA) |
| `spiffs` | data | spiffs | `0x2B0000` | 1024 KB (1 MB) | Partição LittleFS (Frontend SPA e `radios.json`) |

---

## 🚀 Como Compilar e Gravar o Firmware

### 1. Ativar o Ambiente ESP-IDF

Abra o terminal na pasta do projeto e ative o ambiente do ESP-IDF:

```bash
get_idf
```
*(Caso o alias não esteja configurado no seu shell, utilize: `. $HOME/esp/esp-idf/export.sh`)*

---

### 2. Definir o Alvo do Microcontrolador (Primeira Vez)

Para ESP32 clássico / WROVER:
```bash
idf.py set-target esp32
```
*(Se estiver usando ESP32-S3, execute `idf.py set-target esp32s3`)*

---

### 3. Compilar o Projeto

Execute a compilação completa do firmware, bootloader e tabela de partições:

```bash
idf.py build
```

Ao término, o ESP-IDF exibirá o tamanho do binário e a confirmação de que o build foi concluído com sucesso.

---

### 4. Gravar o Firmware na Placa (Flash)

Conecte a placa ESP32 ao computador via cabo USB. Identifique a porta serial (geralmente `/dev/ttyUSB0` ou `/dev/ttyACM0` no Linux, ou `COMx` no Windows).

Grave os binários executando:

```bash
idf.py -p /dev/ttyUSB0 flash
```

> **Dica**: Caso precise apenas recompilar e gravar em um único comando:
> ```bash
> idf.py -p /dev/ttyUSB0 build flash
> ```

---

### 5. Monitorar a Saída Serial

Para acompanhar os logs de inicialização, IP atribuído pelo Wi-Fi e mensagens do sistema:

```bash
idf.py -p /dev/ttyUSB0 monitor
```

Ou compilar, gravar e abrir o monitor serial automaticamente:
```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

*(Para sair do monitor serial do ESP-IDF, pressione `Ctrl + ]`)*.

---

### 6. Alternativa: Gravação Direta via `esptool.py`

Se preferir gravar os binários diretamente sem usar o `idf.py`:

```bash
python -m esptool --chip esp32 -b 460800 --before default_reset --after hard_reset write_flash \
  --flash_mode dio --flash_size 4MB --flash_freq 40m \
  0x1000  build/bootloader/bootloader.bin \
  0x8000  build/partition_table/partition-table.bin \
  0xe000  build/ota_data_initial.bin \
  0x10000 build/web_radio_esp32.bin
```

---

## ⚠️ Solução de Problemas Comuns

- **Permissão de Porta Serial no Linux (`Permission denied: /dev/ttyUSB0`)**:
  Adicione o seu usuário ao grupo `dialout`:
  ```bash
  sudo usermod -a -G dialout $USER
  ```
  *(Faça logout e login novamente para aplicar a permissão)*.

- **Placa não entra em modo de gravação (`A fatal error occurred: Failed to connect to ESP32`)**:
  Mantenha pressionado o botão **BOOT** (ou **IO0**) na placa, dê um clique rápido no botão **EN** (ou **RESET**) e solte o botão BOOT assim que o esptool iniciar a conexão.
