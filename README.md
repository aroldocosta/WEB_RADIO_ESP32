# Web Radio ESP32 📻

Um dispositivo autônomo de rádio via internet (Web Radio) baseado no microcontrolador ESP32 com estágio de áudio digital I2S, interface web responsiva embarcada (Single Page Application com Dial tátil) e suporte a atualizações remotas seguras (OTA A/B).

---

## 🛠️ Requisitos de Hardware

- **Placa de Desenvolvimento**: **ESP32-Audio-Kit** equipada com módulo **Ai-Thinker ESP32-A1S**.
- **Memória**: Flash SPI de **4MB** e **PSRAM integrada de 4MB/8MB** (habilitada no `sdkconfig.defaults`).
- **Codec de Áudio Integrado**: **ES8388** (controle por I2C e áudio digital por I2S).
- **Amplificador Onboard**: NS4150 (3W estéreo Classe D) com chave de habilitação (PA).
- **Pinagem Integrada do Módulo ESP32-A1S**:
  - **Barramento I2C (Controle do Codec)**:
    - `SCL`: GPIO 32
    - `SDA`: GPIO 33 (Endereço I2C `0x10`)
  - **Barramento I2S (Áudio Digital PCM)**:
    - `MCLK`: GPIO 0 *(Clock Mestre obrigatório para o codec)*
    - `BCLK`: GPIO 27
    - `LRCK / WS`: GPIO 25
    - `DOUT` *(ESP32 -> DAC)*: GPIO 26
    - `DIN` *(ADC/Mic -> ESP32)*: GPIO 35
  - **Habilitação do Amplificador de Potência (PA)**:
    - `PA Enable`: GPIO 21 *(nível lógico HIGH ativa os alto-falantes)*
  - **Saídas de Áudio Disponíveis**:
    - Conector P2 estéreo para Fone de Ouvido
    - Bornes para conexão direta de até 2 alto-falantes de 3W (4Ω a 8Ω)

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

- **Sem áudio no fone de ouvido P2 (Conector TRRS de 4 vias vs TRS de 3 vias)**:
  O jack fêmea P2 da placa ESP32-Audio-Kit é estéreo padrão de **3 vias (TRS)**. Fones de celular modernos com microfone embutido possuem plugue de **4 vias (TRRS)**, o que frequentemente causa mau contato no terminal do terra (GND) e ausência de som. Utilize um fone/cabo convencional de **3 vias (TRS)** ou adaptador TRRS->TRS.

- **Conector P2 correto (`PHONE` vs `LINE IN`)**:
  A placa possui dois conectores P2 de 3.5mm lado a lado. Certifique-se de plugar no jack marcado como **`PHONE` / `HEADPHONE`**, e não no conector de entrada **`LINE IN`**.

