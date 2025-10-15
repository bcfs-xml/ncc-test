@echo off
REM ========================================
REM  Batch script para executar o programa
REM  de forma facil no Windows
REM ========================================

REM Muda para o diretorio onde este batch esta localizado
REM Isso garante que o programa encontre input_shapes.json
cd /d "%~dp0"

echo ========================================
echo  EXECUTANDO NESTING ALGORITHM
echo ========================================
echo.
echo Diretorio de trabalho: %CD%
echo.

REM Verificar se o executavel existe
if not exist "genetic_nesting_optimized.exe" (
    echo ERRO: genetic_nesting_optimized.exe nao encontrado!
    echo.
    echo Certifique-se de que o arquivo esta no mesmo diretorio que run.bat
    echo.
    pause
    exit /b 1
)

REM Verificar se o arquivo de entrada existe
if not exist "input_shapes.json" (
    echo ERRO: input_shapes.json nao encontrado!
    echo.
    echo Certifique-se de que o arquivo esta no mesmo diretorio que run.bat
    echo.
    pause
    exit /b 1
)

echo Arquivos verificados:
echo   [OK] genetic_nesting_optimized.exe
echo   [OK] input_shapes.json
echo.
echo Iniciando processamento...
echo ========================================
echo.

REM Executar o programa
genetic_nesting_optimized.exe

echo.
echo ========================================
echo  EXECUCAO FINALIZADA
echo ========================================
echo.

REM Verificar se o arquivo de saida foi criado
if exist "genetic_nesting_optimized_result.json" (
    echo [OK] Resultado salvo em: genetic_nesting_optimized_result.json
    echo.
) else (
    echo [AVISO] Arquivo de resultado nao foi criado.
    echo         Verifique se houve erros acima.
    echo.
)

REM Pausar para o usuario ver os resultados
pause
