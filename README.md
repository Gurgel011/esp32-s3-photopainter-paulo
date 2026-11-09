# PhotoPainter · ESP32-S3

`ESP32-S3` · `ESP-IDF` · `C` · `Waveshare`

[Objetivo e etapas](#objetivo) · [Build](#build) · [Wi-Fi](#configurar-wi-fi) · [← Todos os projetos](https://github.com/Gurgel011#projetos)

Projeto novo, separado do `xiaozhi-esp32`, para o dispositivo ESP32-S3 PhotoPainter/Waveshare.

## Objetivo

Criar uma base limpa para evoluir em etapas:

1. Inicializar o ESP32-S3 com ESP-IDF puro.
2. Conectar no Wi-Fi de forma configuravel.
3. Inicializar a tela do PhotoPainter.
4. Exibir o logo da Antena 1 como elemento principal da tela.
5. Adicionar audio/stream ao vivo.
6. Evoluir para comandos locais ou servidor auxiliar.

## Hardware detectado

- Chip: ESP32-S3
- Flash: 16 MB
- PSRAM: 8 MB
- Porta serial: geralmente `/dev/ttyACM0` no Linux, `/dev/cu.usbmodem*` no macOS
- Identidade observada na flash antiga: `waveshare-s3-PhotoPainter`

## Build

```bash
cd <diretorio-do-projeto>
. <caminho-do-esp-idf>/export.sh
idf.py set-target esp32s3
idf.py build
```

## Configurar Wi-Fi

```bash
idf.py menuconfig
```

Depois vá em:

```text
PhotoPainter Paulo App -> WiFi SSID / WiFi password
```

O logo principal fica em:

```text
assets/antena1-logo.png
```

## Gravar

Use somente depois de validar que este firmware deve substituir o atual:

```bash
idf.py -p <porta-serial> flash monitor
```
