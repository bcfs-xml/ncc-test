# ====================================================================
# Makefile para genetic_nesting_optimized
# Suporta compilação para Linux e Windows (cross-compilation)
# ====================================================================

# ==================== CONFIGURAÇÃO ====================

# Nome do programa
PROGRAM = genetic_nesting_optimized
SOURCE = $(PROGRAM).c

# Compiladores
CC_LINUX = gcc
CC_WINDOWS = x86_64-w64-mingw32-gcc
CC_MSVC = cl

# Flags comuns de otimização
CFLAGS_BASE = -O3 -march=x86-64-v2 -mtune=generic -flto -Wall -Wextra
CFLAGS_OPENMP = -fopenmp
LDFLAGS = -lm -static

# Flags MSVC
MSVC_FLAGS_BASE = /O2 /GL /W3 /D_CRT_SECURE_NO_WARNINGS /MT /favor:blend
MSVC_FLAGS_OPENMP = /openmp

# Detectar sistema operacional
ifeq ($(OS),Windows_NT)
    DETECTED_OS = Windows
    EXEC_EXT = .exe
    RM = del /Q
else
    DETECTED_OS = $(shell uname -s)
    EXEC_EXT =
    RM = rm -f
endif

# ==================== TARGETS PADRÃO ====================

.PHONY: all clean help linux windows windows-no-openmp test

# Target padrão
all: help

# Help - mostra todas as opções disponíveis
help:
	@echo "=========================================="
	@echo "  Makefile - genetic_nesting_optimized"
	@echo "=========================================="
	@echo ""
	@echo "Sistema detectado: $(DETECTED_OS)"
	@echo ""
	@echo "Targets disponiveis:"
	@echo ""
	@echo "  make linux               - Compilar para Linux (com OpenMP)"
	@echo "  make linux-no-openmp     - Compilar para Linux (sem OpenMP)"
	@echo "  make windows             - Cross-compilar para Windows (com OpenMP)"
	@echo "  make windows-no-openmp   - Cross-compilar para Windows (sem OpenMP)"
	@echo "  make msvc                - Instrucoes para compilar com MSVC"
	@echo "  make clean               - Limpar arquivos compilados"
	@echo "  make test                - Testar executavel"
	@echo "  make info                - Mostrar informacoes sobre executaveis"
	@echo ""
	@echo "Exemplos:"
	@echo "  make linux               # Compila para Linux com OpenMP"
	@echo "  make windows             # Compila para Windows no WSL"
	@echo "  make clean               # Remove executaveis"
	@echo ""

# ==================== COMPILAÇÃO LINUX ====================

linux: $(PROGRAM)$(EXEC_EXT)
	@echo ""
	@echo "=========================================="
	@echo "  COMPILACAO LINUX CONCLUIDA!"
	@echo "=========================================="
	@echo "Executavel: $(PROGRAM)$(EXEC_EXT)"
	@echo "OpenMP: ATIVADO"
	@echo ""
	@echo "Para executar:"
	@echo "  ./$(PROGRAM)$(EXEC_EXT)"
	@echo ""

$(PROGRAM)$(EXEC_EXT): $(SOURCE)
	@echo "Compilando para Linux com OpenMP..."
	$(CC_LINUX) $(CFLAGS_BASE) $(CFLAGS_OPENMP) $< -o $@ $(LDFLAGS)

linux-no-openmp: $(PROGRAM)_nomp$(EXEC_EXT)
	@echo ""
	@echo "=========================================="
	@echo "  COMPILACAO LINUX CONCLUIDA!"
	@echo "=========================================="
	@echo "Executavel: $(PROGRAM)_nomp$(EXEC_EXT)"
	@echo "OpenMP: DESATIVADO"
	@echo ""
	@echo "Para executar:"
	@echo "  ./$(PROGRAM)_nomp$(EXEC_EXT)"
	@echo ""

$(PROGRAM)_nomp$(EXEC_EXT): $(SOURCE)
	@echo "Compilando para Linux sem OpenMP..."
	$(CC_LINUX) $(CFLAGS_BASE) $< -o $@ $(LDFLAGS)

# ==================== CROSS-COMPILATION WINDOWS ====================

windows: $(PROGRAM).exe
	@echo ""
	@echo "=========================================="
	@echo "  CROSS-COMPILATION WINDOWS CONCLUIDA!"
	@echo "=========================================="
	@echo "Executavel: $(PROGRAM).exe"
	@echo "OpenMP: ATIVADO"
	@echo "Arquitetura: Windows x64 (64-bit)"
	@echo "Tipo: Estatico (sem dependencias DLL)"
	@echo ""
	@echo "Para testar no WSL (requer Wine):"
	@echo "  wine $(PROGRAM).exe"
	@echo ""
	@echo "Para usar no Windows:"
	@echo "  1. Copie $(PROGRAM).exe para Windows"
	@echo "  2. Execute: $(PROGRAM).exe"
	@echo ""

$(PROGRAM).exe: $(SOURCE)
	@echo "Cross-compilando para Windows com OpenMP..."
	@echo "Verificando compilador MinGW-w64..."
	@which $(CC_WINDOWS) > /dev/null || (echo "ERRO: MinGW-w64 nao encontrado! Execute: sudo apt-get install mingw-w64" && exit 1)
	$(CC_WINDOWS) $(CFLAGS_BASE) $(CFLAGS_OPENMP) $< -o $@ $(LDFLAGS)

windows-no-openmp: $(PROGRAM)_nomp.exe
	@echo ""
	@echo "=========================================="
	@echo "  CROSS-COMPILATION WINDOWS CONCLUIDA!"
	@echo "=========================================="
	@echo "Executavel: $(PROGRAM)_nomp.exe"
	@echo "OpenMP: DESATIVADO"
	@echo "Arquitetura: Windows x64 (64-bit)"
	@echo ""

$(PROGRAM)_nomp.exe: $(SOURCE)
	@echo "Cross-compilando para Windows sem OpenMP..."
	@which $(CC_WINDOWS) > /dev/null || (echo "ERRO: MinGW-w64 nao encontrado! Execute: sudo apt-get install mingw-w64" && exit 1)
	$(CC_WINDOWS) $(CFLAGS_BASE) $< -o $@ $(LDFLAGS)

# ==================== MSVC (Windows Nativo) ====================

msvc:
	@echo ""
	@echo "=========================================="
	@echo "  COMPILACAO COM MSVC (Windows Nativo)"
	@echo "=========================================="
	@echo ""
	@echo "Para compilar com MSVC no Windows:"
	@echo ""
	@echo "1. Abra 'Developer Command Prompt for VS'"
	@echo ""
	@echo "2. Execute um dos comandos:"
	@echo ""
	@echo "   COM OpenMP:"
	@echo "     $(CC_MSVC) $(MSVC_FLAGS_BASE) $(MSVC_FLAGS_OPENMP) /Fe:$(PROGRAM).exe $(SOURCE)"
	@echo ""
	@echo "   SEM OpenMP:"
	@echo "     $(CC_MSVC) $(MSVC_FLAGS_BASE) /Fe:$(PROGRAM).exe $(SOURCE)"
	@echo ""
	@echo "3. Ou use o script fornecido:"
	@echo "     compilar_windows_msvc.bat"
	@echo ""

# ==================== LIMPEZA ====================

clean:
	@echo "Limpando arquivos compilados..."
	-$(RM) $(PROGRAM) $(PROGRAM).exe $(PROGRAM)_nomp $(PROGRAM)_nomp.exe 2>/dev/null
	-$(RM) *.obj *.o 2>/dev/null
	@echo "Limpeza concluida!"

# ==================== TESTES ====================

test:
	@echo "=========================================="
	@echo "  TESTES"
	@echo "=========================================="
	@echo ""
ifeq ($(DETECTED_OS),Windows_NT)
	@if exist $(PROGRAM).exe ( \
		echo "Testando $(PROGRAM).exe..." && \
		$(PROGRAM).exe \
	) else ( \
		echo "ERRO: $(PROGRAM).exe nao encontrado!" && \
		echo "Execute 'make windows' primeiro." \
	)
else
	@if [ -f $(PROGRAM).exe ]; then \
		echo "Testando $(PROGRAM).exe com Wine..."; \
		wine $(PROGRAM).exe; \
	elif [ -f $(PROGRAM) ]; then \
		echo "Testando $(PROGRAM)..."; \
		./$(PROGRAM); \
	else \
		echo "ERRO: Nenhum executavel encontrado!"; \
		echo "Execute 'make linux' ou 'make windows' primeiro."; \
		exit 1; \
	fi
endif

# ==================== INFORMAÇÕES ====================

info:
	@echo "=========================================="
	@echo "  INFORMACOES DO SISTEMA"
	@echo "=========================================="
	@echo ""
	@echo "Sistema operacional: $(DETECTED_OS)"
	@echo ""
	@echo "Compiladores disponiveis:"
	@echo ""
	@which $(CC_LINUX) > /dev/null && echo "  [OK] GCC (Linux): $$($(CC_LINUX) --version | head -n1)" || echo "  [X] GCC nao encontrado"
	@which $(CC_WINDOWS) > /dev/null && echo "  [OK] MinGW-w64 (Windows cross): $$($(CC_WINDOWS) --version | head -n1)" || echo "  [X] MinGW-w64 nao encontrado (instale: sudo apt-get install mingw-w64)"
	@echo ""
	@echo "Executaveis encontrados:"
	@echo ""
	@ls -lh $(PROGRAM) $(PROGRAM).exe $(PROGRAM)_nomp $(PROGRAM)_nomp.exe 2>/dev/null | awk '{print "  " $$9 " (" $$5 ")"}' || echo "  Nenhum executavel encontrado"
	@echo ""
	@echo "Arquivos do projeto:"
	@echo ""
	@ls -lh $(SOURCE) input_shapes.json 2>/dev/null | awk '{print "  " $$9 " (" $$5 ")"}' || echo "  Arquivos nao encontrados"
	@echo ""

# ==================== TARGETS AVANÇADOS ====================

# Compilação com profile-guided optimization (PGO)
pgo-linux:
	@echo "Compilacao PGO (Profile-Guided Optimization) para Linux..."
	$(CC_LINUX) $(CFLAGS_BASE) $(CFLAGS_OPENMP) -fprofile-generate $(SOURCE) -o $(PROGRAM)_pgo $(LDFLAGS)
	@echo "Executando para coletar profile..."
	./$(PROGRAM)_pgo
	@echo "Recompilando com profile..."
	$(CC_LINUX) $(CFLAGS_BASE) $(CFLAGS_OPENMP) -fprofile-use $(SOURCE) -o $(PROGRAM) $(LDFLAGS)
	$(RM) $(PROGRAM)_pgo *.gcda
	@echo "PGO concluido!"

# Versão de debug (sem otimizações)
debug-linux:
	@echo "Compilando versao DEBUG para Linux..."
	$(CC_LINUX) -g -O0 -Wall -Wextra $(CFLAGS_OPENMP) $(SOURCE) -o $(PROGRAM)_debug $(LDFLAGS)
	@echo "Executavel debug gerado: $(PROGRAM)_debug"
	@echo "Para debugar: gdb ./$(PROGRAM)_debug"

# Strip symbols (reduzir tamanho)
strip:
	@echo "Removendo symbols de debug dos executaveis..."
	@if [ -f $(PROGRAM) ]; then strip $(PROGRAM); echo "  Stripped: $(PROGRAM)"; fi
	@if [ -f $(PROGRAM).exe ]; then strip $(PROGRAM).exe; echo "  Stripped: $(PROGRAM).exe"; fi
	@echo "Concluido!"

# Benchmark rápido
benchmark: linux
	@echo "=========================================="
	@echo "  BENCHMARK"
	@echo "=========================================="
	@echo ""
	@echo "Executando 3 vezes para benchmark..."
	@for i in 1 2 3; do \
		echo ""; \
		echo "Execucao $$i:"; \
		time ./$(PROGRAM); \
	done

# ==================== ANÁLISE DE CÓDIGO ====================

# Análise estática com cppcheck (se disponível)
analyze:
	@echo "Analisando codigo com ferramentas estaticas..."
	@which cppcheck > /dev/null && cppcheck --enable=all --suppress=missingIncludeSystem $(SOURCE) || echo "cppcheck nao encontrado (instale: sudo apt-get install cppcheck)"

# ==================== DOCUMENTAÇÃO ====================

# Mostrar estatísticas do código
stats:
	@echo "=========================================="
	@echo "  ESTATISTICAS DO CODIGO"
	@echo "=========================================="
	@echo ""
	@echo "Linhas de codigo:"
	@wc -l $(SOURCE)
	@echo ""
	@echo "Funcoes definidas:"
	@grep -c "^[a-zA-Z_].*(" $(SOURCE) || echo "N/A"
	@echo ""
	@echo "Tamanho do arquivo:"
	@ls -lh $(SOURCE) | awk '{print $$5}'
	@echo ""
	@echo "Includes utilizados:"
	@grep "^#include" $(SOURCE)
	@echo ""
