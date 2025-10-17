 x86_64-w64-mingw32-gcc -O3 -march=x86-64-v2 -mtune=generic -flto \
      genetic_nesting_optimized.c -o nesting.exe -lm -static

## Compilação com OpenMP (Paralelização Acelerada)

### Linux/WSL:
```bash
gcc -O3 -march=native -fopenmp genetic_nesting_optimized.c -o genetic_nesting_optimized -lm
```

### Windows (MinGW-w64):
```bash
x86_64-w64-mingw32-gcc -O3 -march=x86-64-v2 -mtune=generic -flto -fopenmp \
    genetic_nesting_optimized.c -o genetic_nesting_optimized.exe -lm -static
```

### Windows (MSVC):
```bash
cl /O2 /openmp genetic_nesting_optimized.c /Fe:genetic_nesting_optimized.exe
```

### Controlar número de threads:
```bash
# Linux/WSL
export OMP_NUM_THREADS=8
./genetic_nesting_optimized

# Windows
set OMP_NUM_THREADS=8
genetic_nesting_optimized.exe
```

## Notas sobre OpenMP

- O código agora suporta paralelização automática com OpenMP
- Por padrão, usa todos os cores disponíveis no sistema
- Use a variável de ambiente `OMP_NUM_THREADS` para controlar o número de threads
- A flag `-fopenmp` (GCC) ou `/openmp` (MSVC) é necessária para ativar OpenMP
- Sem OpenMP, o código funciona normalmente em modo serial