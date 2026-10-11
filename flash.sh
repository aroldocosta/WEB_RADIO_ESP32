#!/usr/bin/env bash
set -e

# Garante a execução na raiz do projeto
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$PROJECT_DIR"

# 1. Verifica se o ESP-IDF já está carregado no PATH
if ! command -v idf.py >/dev/null 2>&1; then
    echo "⚙️  ESP-IDF não detectado no ambiente atual. Ativando ferramentas..."
    if [ -f "$HOME/esp/esp-idf/export.sh" ]; then
        . "$HOME/esp/esp-idf/export.sh"
    elif [ -n "$IDF_PATH" ] && [ -f "$IDF_PATH/export.sh" ]; then
        . "$IDF_PATH/export.sh"
    elif [ -f "/home/user/esp/esp-idf/export.sh" ]; then
        . "/home/user/esp/esp-idf/export.sh"
    else
        echo "❌ Erro: Arquivo export.sh do ESP-IDF não foi encontrado em ~/esp/esp-idf."
        exit 1
    fi
else
    echo "✅ Ambiente ESP-IDF já ativo."
fi

# 2. Define a porta serial (pode ser passada como argumento, padrão: /dev/ttyUSB0)
PORT="${1:-/dev/ttyUSB0}"

if [ ! -e "$PORT" ]; then
    echo "⚠️  Porta '$PORT' não encontrada de imediato. Procurando dispositivos seriais..."
    DETECTED_PORT=$(ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null | head -n 1 || true)
    if [ -n "$DETECTED_PORT" ]; then
        echo "🔍 Porta serial identificada: $DETECTED_PORT"
        PORT="$DETECTED_PORT"
    fi
fi

# 3. Executa a gravação do firmware e o monitor serial
echo "🚀 Gravando firmware na porta $PORT e abrindo o monitor serial..."
exec idf.py -p "$PORT" flash
