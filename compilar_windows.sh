#!/bin/bash

# Script de compilacao para Windows
# Execute este script no WSL2 para gerar o executavel Windows

echo "=========================================="
echo "  COMPILACAO PARA WINDOWS"
echo "=========================================="
echo ""

# Verificar se o mingw-w64 esta instalado
if ! command -v x86_64-w64-mingw32-gcc &> /dev/null; then
    echo "ERRO: mingw-w64 nao encontrado!"
    echo ""
    echo "Instalando mingw-w64..."
    sudo apt update
    sudo apt install -y mingw-w64

    if [ $? -ne 0 ]; then
        echo "ERRO: Falha ao instalar mingw-w64"
        echo "Execute manualmente: sudo apt install mingw-w64"
        exit 1
    fi
fi

echo "Compilador encontrado: $(x86_64-w64-mingw32-gcc --version | head -1)"
echo ""

# Compilar para Windows
echo "Compilando para Windows..."
echo ""

x86_64-w64-mingw32-gcc \
    -o genetic_nesting_optimized.exe \
    genetic_nesting_optimized.c \
    -lm \
    -O3 \
    -static \
    -march=x86-64 \
    -mtune=generic \
    -ffast-math \
    -funroll-loops

if [ $? -eq 0 ]; then
    echo ""
    echo "=========================================="
    echo "  COMPILACAO CONCLUIDA COM SUCESSO!"
    echo "=========================================="
    echo ""
    echo "Executavel gerado: genetic_nesting_optimized.exe"
    echo "Tipo: $(file genetic_nesting_optimized.exe)"
    echo "Tamanho: $(du -h genetic_nesting_optimized.exe | cut -f1)"
    echo ""
    echo "PROXIMOS PASSOS:"
    echo "1. Copie os arquivos para o Windows:"
    echo "   - genetic_nesting_optimized.exe"
    echo "   - input_shapes.json"
    echo ""
    echo "2. No Windows, abra o CMD ou PowerShell"
    echo "3. Execute: genetic_nesting_optimized.exe"
    echo ""
    echo "=========================================="
else
    echo ""
    echo "ERRO: Falha na compilacao!"
    echo "Verifique os erros acima."
    exit 1
fi
