#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <float.h>
#include <stdbool.h>
#include <errno.h>

#ifdef _OPENMP
    #include <omp.h>
#endif

#ifdef _WIN32
    #include <windows.h>
    #include <process.h>
    #define getpid _getpid
#else
    #include <unistd.h>
    #include <sys/time.h>
#endif

#define MAX_PIECES 100
#define MAX_POINTS 1000
#define MAX_BOARDS 50
#define MAX_ANGLES 10
#define PI 3.14159265359

// ==================== CONCAVE NESTING FEATURE ====================
// Feature flag: Set to 0 to disable concave nesting optimization (Phase 3)
#define ENABLE_CONCAVE_NESTING 1

// Concave nesting parameters - IMPROVED PRECISION
#define CONCAVITY_THRESHOLD 0.25      // Minimum 25% empty space in bbox to consider concavity
#define GRID_RESOLUTION 40            // 40x40 grid sampling for candidate points (was 20, increased 4x coverage)
#define SUBGRID_RESOLUTION 5          // 5x5 sub-grid refinement around promising positions
#define MAX_SMALL_PIECE_RATIO 0.25    // Small piece = max 25% of large piece area (was 0.15, more permissive)

// Debug mode: Set to 1 to enable detailed logging
#define DEBUG_CONCAVE_NESTING 1

// Alternative parameters for experimentation:
// For maximum precision (slower): GRID_RESOLUTION 60, MAX_SMALL_PIECE_RATIO 0.30
// For speed (faster): GRID_RESOLUTION 30, MAX_SMALL_PIECE_RATIO 0.20
// For aggressive fitting: CONCAVITY_THRESHOLD 0.15, MAX_SMALL_PIECE_RATIO 0.35

// Parametros do Algoritmo Genetico - AJUSTADOS PARA EVITAR CONVERGÊNCIA PREMATURA
#define POPULATION_SIZE 100
#define GENERATIONS 50
#define TOURNAMENT_SIZE 3
#define MUTATION_RATE 0.15
#define ELITE_SIZE 10


// #define POPULATION_SIZE 100
// #define GENERATIONS 50
// #define TOURNAMENT_SIZE 3
// #define MUTATION_RATE 0.30
// #define ELITE_SIZE 2

// Core data structures (mesmas do codigo original)
typedef struct {
    double x, y;
} Point;

#if ENABLE_CONCAVE_NESTING
// ==================== CONCAVE NESTING DATA STRUCTURES ====================

typedef struct {
    double x, y;
} ConcavePoint;

typedef struct {
    ConcavePoint* points;
    int num_points;
    double concavity_ratio;
} ConcavityInfo;

#endif // ENABLE_CONCAVE_NESTING

// Pre-allocated buffer pools para evitar malloc/free repetidos
#define POOL_SIZE 1024
static Point point_pool[POOL_SIZE];
static int pool_index = 0;

typedef struct {
    Point* points;
    int point_count;
    int* allowed_angles;
    int angle_count;
    int id;
    double width, height;
    double area;
    // Otimização: cache de bounding box
    double min_x, min_y, max_x, max_y;
} Piece;

typedef struct {
    Point position;
    int angle;
    int piece_id;
    Piece rotated_piece;
} PlacedPiece;

typedef struct {
    double width, height;
    PlacedPiece* placed_pieces;
    int piece_count;
    double used_area;
    double efficiency;
} Board;

typedef struct {
    double board_x, board_y;
    double distance_between_boards;
    double distance_between_pieces;
    Piece* pieces;
    int piece_count;
} InputData;

typedef struct {
    Board* boards;
    int board_count;
    double total_efficiency;
    double execution_time;
} Result;

// Estrutura do Genoma (Individuo)
// NOTA: rotation_choices é indexado por piece_id, NÃO por posição na sequência!
typedef struct {
    int* piece_sequence;     // sequence[i] = piece_id
    int* rotation_choices;   // rotation_choices[piece_id] = rotation index
    double fitness;
    int board_count;
    double total_efficiency;
} Genome;

// Global variables
InputData input_data;
Result result;
Result best_result;

// Thread-local RNG state para evitar race conditions
// Cada thread mantem seu proprio estado de geracao de numeros aleatorios
#ifdef _OPENMP
    static unsigned int* thread_seeds = NULL;
    static int max_threads = 0;
#endif

// Cache para senos e cossenos pre-calculados
#define ANGLE_CACHE_SIZE 360
static double cos_cache[ANGLE_CACHE_SIZE];
static double sin_cache[ANGLE_CACHE_SIZE];
static bool cache_initialized = false;

void init_trig_cache() {
    if (cache_initialized) return;
    for (int i = 0; i < ANGLE_CACHE_SIZE; i++) {
        double angle_rad = i * PI / 180.0;
        cos_cache[i] = cos(angle_rad);
        sin_cache[i] = sin(angle_rad);
    }
    cache_initialized = true;
}

// ==================== OPTIMIZED UTILITY FUNCTIONS ====================

// Inline functions para operações simples
static inline double calculate_distance_squared(Point a, Point b) {
    double dx = a.x - b.x;
    double dy = a.y - b.y;
    return dx * dx + dy * dy;
}

static inline double calculate_distance(Point a, Point b) {
    return sqrt(calculate_distance_squared(a, b));
}

static inline double min_double(double a, double b) {
    return (a < b) ? a : b;
}

static inline double max_double(double a, double b) {
    return (a > b) ? a : b;
}

// Versão otimizada de rotate_point usando cache
Point rotate_point_fast(Point p, Point center, int angle_deg) {
    angle_deg = angle_deg % 360;
    if (angle_deg < 0) angle_deg += 360;

    double cos_a = cos_cache[angle_deg];
    double sin_a = sin_cache[angle_deg];

    Point rotated;
    double dx = p.x - center.x;
    double dy = p.y - center.y;
    rotated.x = center.x + dx * cos_a - dy * sin_a;
    rotated.y = center.y + dx * sin_a + dy * cos_a;

    return rotated;
}

// Versão otimizada que calcula e armazena bounding box
void calculate_bounding_box_cached(Piece* piece) {
    if (piece->point_count == 0) return;

    piece->min_x = piece->max_x = piece->points[0].x;
    piece->min_y = piece->max_y = piece->points[0].y;

    // Loop unrolling parcial para melhor performance
    int i;
    for (i = 1; i + 3 < piece->point_count; i += 4) {
        double x0 = piece->points[i].x;
        double y0 = piece->points[i].y;
        double x1 = piece->points[i+1].x;
        double y1 = piece->points[i+1].y;
        double x2 = piece->points[i+2].x;
        double y2 = piece->points[i+2].y;
        double x3 = piece->points[i+3].x;
        double y3 = piece->points[i+3].y;

        if (x0 < piece->min_x) piece->min_x = x0;
        if (x0 > piece->max_x) piece->max_x = x0;
        if (x1 < piece->min_x) piece->min_x = x1;
        if (x1 > piece->max_x) piece->max_x = x1;
        if (x2 < piece->min_x) piece->min_x = x2;
        if (x2 > piece->max_x) piece->max_x = x2;
        if (x3 < piece->min_x) piece->min_x = x3;
        if (x3 > piece->max_x) piece->max_x = x3;

        if (y0 < piece->min_y) piece->min_y = y0;
        if (y0 > piece->max_y) piece->max_y = y0;
        if (y1 < piece->min_y) piece->min_y = y1;
        if (y1 > piece->max_y) piece->max_y = y1;
        if (y2 < piece->min_y) piece->min_y = y2;
        if (y2 > piece->max_y) piece->max_y = y2;
        if (y3 < piece->min_y) piece->min_y = y3;
        if (y3 > piece->max_y) piece->max_y = y3;
    }

    // Processar elementos restantes
    for (; i < piece->point_count; i++) {
        if (piece->points[i].x < piece->min_x) piece->min_x = piece->points[i].x;
        if (piece->points[i].x > piece->max_x) piece->max_x = piece->points[i].x;
        if (piece->points[i].y < piece->min_y) piece->min_y = piece->points[i].y;
        if (piece->points[i].y > piece->max_y) piece->max_y = piece->points[i].y;
    }
}

Piece rotate_piece(Piece* original, int angle) {
    Piece rotated = *original;
    rotated.points = malloc(sizeof(Point) * original->point_count);

    Point center = {0, 0};
    for (int i = 0; i < original->point_count; i++) {
        center.x += original->points[i].x;
        center.y += original->points[i].y;
    }
    center.x /= original->point_count;
    center.y /= original->point_count;

    for (int i = 0; i < original->point_count; i++) {
        rotated.points[i] = rotate_point_fast(original->points[i], center, angle);
    }

    calculate_bounding_box_cached(&rotated);
    rotated.width = rotated.max_x - rotated.min_x;
    rotated.height = rotated.max_y - rotated.min_y;

    return rotated;
}

double calculate_polygon_area(Point* points, int count) {
    double area = 0.0;
    for (int i = 0; i < count; i++) {
        int j = (i + 1) % count;
        area += points[i].x * points[j].y;
        area -= points[j].x * points[i].y;
    }
    return fabs(area) * 0.5;
}

bool point_in_polygon(Point test, Point* polygon, int count) {
    bool inside = false;
    for (int i = 0, j = count - 1; i < count; j = i++) {
        if (((polygon[i].y > test.y) != (polygon[j].y > test.y)) &&
            (test.x < (polygon[j].x - polygon[i].x) * (test.y - polygon[i].y) / (polygon[j].y - polygon[i].y) + polygon[i].x)) {
            inside = !inside;
        }
    }
    return inside;
}

double point_to_segment_distance(Point p, Point a, Point b) {
    double dx = b.x - a.x;
    double dy = b.y - a.y;
    double len_sq = dx * dx + dy * dy;

    if (len_sq < 1e-10) {
        return calculate_distance(p, a);
    }

    double t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / len_sq;
    t = (t < 0) ? 0 : ((t > 1) ? 1 : t);

    Point closest = {a.x + t * dx, a.y + t * dy};
    return calculate_distance(p, closest);
}

static inline int get_orientation(Point p, Point q, Point r) {
    double val = (q.y - p.y) * (r.x - q.x) - (q.x - p.x) * (r.y - q.y);
    if (fabs(val) < 1e-10) return 0;
    return (val > 0) ? 1 : 2;
}

static inline bool point_on_segment(Point p, Point q, Point r) {
    return q.x <= max_double(p.x, r.x) && q.x >= min_double(p.x, r.x) &&
           q.y <= max_double(p.y, r.y) && q.y >= min_double(p.y, r.y);
}

bool segments_intersect(Point p1, Point q1, Point p2, Point q2) {
    int o1 = get_orientation(p1, q1, p2);
    int o2 = get_orientation(p1, q1, q2);
    int o3 = get_orientation(p2, q2, p1);
    int o4 = get_orientation(p2, q2, q1);

    if (o1 != o2 && o3 != o4) return true;
    if (o1 == 0 && point_on_segment(p1, p2, q1)) return true;
    if (o2 == 0 && point_on_segment(p1, q2, q1)) return true;
    if (o3 == 0 && point_on_segment(p2, p1, q2)) return true;
    if (o4 == 0 && point_on_segment(p2, q1, q2)) return true;

    return false;
}

// Otimização: Bounding box check antes de calcular distância exata
static inline bool bounding_boxes_overlap(Piece* p1, Point pos1, Piece* p2, Point pos2, double min_distance) {
    double p1_min_x = p1->min_x + pos1.x - min_distance;
    double p1_max_x = p1->max_x + pos1.x + min_distance;
    double p1_min_y = p1->min_y + pos1.y - min_distance;
    double p1_max_y = p1->max_y + pos1.y + min_distance;

    double p2_min_x = p2->min_x + pos2.x;
    double p2_max_x = p2->max_x + pos2.x;
    double p2_min_y = p2->min_y + pos2.y;
    double p2_max_y = p2->max_y + pos2.y;

    return !(p1_max_x < p2_min_x || p2_max_x < p1_min_x ||
             p1_max_y < p2_min_y || p2_max_y < p1_min_y);
}

// Versão otimizada: usa stack allocation quando possível
double calculate_min_polygon_distance(Piece* p1, Point pos1, Piece* p2, Point pos2) {
    // Early exit: check bounding boxes primeiro
    if (!bounding_boxes_overlap(p1, pos1, p2, pos2, 0)) {
        double dx = max_double(p1->min_x + pos1.x, p2->min_x + pos2.x) -
                    min_double(p1->max_x + pos1.x, p2->max_x + pos2.x);
        double dy = max_double(p1->min_y + pos1.y, p2->min_y + pos2.y) -
                    min_double(p1->max_y + pos1.y, p2->max_y + pos2.y);
        if (dx > 0 && dy > 0) {
            return sqrt(dx*dx + dy*dy);
        } else if (dx > 0) {
            return dx;
        } else if (dy > 0) {
            return dy;
        }
    }

    double min_distance = DBL_MAX;

    // Usar stack allocation para arrays pequenos
    Point poly1_stack[32], poly2_stack[32];
    Point *poly1, *poly2;
    bool use_heap1 = p1->point_count > 32;
    bool use_heap2 = p2->point_count > 32;

    poly1 = use_heap1 ? malloc(sizeof(Point) * p1->point_count) : poly1_stack;
    poly2 = use_heap2 ? malloc(sizeof(Point) * p2->point_count) : poly2_stack;

    // Transformar pontos apenas uma vez
    for (int i = 0; i < p1->point_count; i++) {
        poly1[i].x = p1->points[i].x + pos1.x;
        poly1[i].y = p1->points[i].y + pos1.y;
    }

    for (int i = 0; i < p2->point_count; i++) {
        poly2[i].x = p2->points[i].x + pos2.x;
        poly2[i].y = p2->points[i].y + pos2.y;
    }

    // Calcular distâncias
    for (int i = 0; i < p1->point_count; i++) {
        for (int j = 0; j < p2->point_count; j++) {
            int next_j = (j + 1) % p2->point_count;
            double dist = point_to_segment_distance(poly1[i], poly2[j], poly2[next_j]);
            if (dist < min_distance) min_distance = dist;
        }
    }

    for (int i = 0; i < p2->point_count; i++) {
        for (int j = 0; j < p1->point_count; j++) {
            int next_j = (j + 1) % p1->point_count;
            double dist = point_to_segment_distance(poly2[i], poly1[j], poly1[next_j]);
            if (dist < min_distance) min_distance = dist;
        }
    }

    if (use_heap1) free(poly1);
    if (use_heap2) free(poly2);

    return min_distance;
}

// Versão otimizada: SAT com stack allocation
bool polygons_overlap_sat(Piece* p1, Point pos1, Piece* p2, Point pos2) {
    // Early rejection com bounding boxes
    if (!bounding_boxes_overlap(p1, pos1, p2, pos2, 0)) {
        return false;
    }

    Point poly1_stack[32], poly2_stack[32];
    Point *poly1, *poly2;
    bool use_heap1 = p1->point_count > 32;
    bool use_heap2 = p2->point_count > 32;

    poly1 = use_heap1 ? malloc(sizeof(Point) * p1->point_count) : poly1_stack;
    poly2 = use_heap2 ? malloc(sizeof(Point) * p2->point_count) : poly2_stack;

    for (int i = 0; i < p1->point_count; i++) {
        poly1[i].x = p1->points[i].x + pos1.x;
        poly1[i].y = p1->points[i].y + pos1.y;
    }

    for (int i = 0; i < p2->point_count; i++) {
        poly2[i].x = p2->points[i].x + pos2.x;
        poly2[i].y = p2->points[i].y + pos2.y;
    }

    bool overlaps = false;

    // Point in polygon tests
    for (int i = 0; i < p1->point_count; i++) {
        if (point_in_polygon(poly1[i], poly2, p2->point_count)) {
            overlaps = true;
            goto cleanup;
        }
    }

    for (int i = 0; i < p2->point_count; i++) {
        if (point_in_polygon(poly2[i], poly1, p1->point_count)) {
            overlaps = true;
            goto cleanup;
        }
    }

    // Segment intersection tests
    for (int i = 0; i < p1->point_count; i++) {
        int next_i = (i + 1) % p1->point_count;
        for (int j = 0; j < p2->point_count; j++) {
            int next_j = (j + 1) % p2->point_count;
            if (segments_intersect(poly1[i], poly1[next_i], poly2[j], poly2[next_j])) {
                overlaps = true;
                goto cleanup;
            }
        }
    }

cleanup:
    if (use_heap1) free(poly1);
    if (use_heap2) free(poly2);

    return overlaps;
}

bool polygons_collide(Piece* p1, Point pos1, Piece* p2, Point pos2, double min_distance) {
    // Early rejection: check bounding boxes primeiro
    if (!bounding_boxes_overlap(p1, pos1, p2, pos2, min_distance)) {
        return false;
    }

    if (polygons_overlap_sat(p1, pos1, p2, pos2)) {
        return true;
    }

    double actual_distance = calculate_min_polygon_distance(p1, pos1, p2, pos2);
    return actual_distance < min_distance;
}

bool piece_fits_in_board(Piece* piece, Point position, Board* board) {
    const double EPSILON = 2.0;
    double margin = input_data.distance_between_boards;

    double left_boundary = margin - EPSILON;
    double bottom_boundary = margin - EPSILON;
    double right_boundary = board->width - margin + EPSILON;
    double top_boundary = board->height - margin + EPSILON;

    if ((position.x + piece->min_x) < left_boundary ||
        (position.y + piece->min_y) < bottom_boundary ||
        (position.x + piece->max_x) > right_boundary ||
        (position.y + piece->max_y) > top_boundary) {
        return false;
    }

    for (int i = 0; i < board->piece_count; i++) {
        if (polygons_collide(piece, position, &board->placed_pieces[i].rotated_piece,
                           board->placed_pieces[i].position, input_data.distance_between_pieces)) {
            return false;
        }
    }

    return true;
}

// Versão otimizada da busca de posição
Point find_best_position_fast(Piece* piece, Board* board) {
    Point best_pos = {-1, -1};
    double best_score = DBL_MAX;

    double min_x = input_data.distance_between_boards;
    double min_y = input_data.distance_between_boards;

    double usable_width = board->width - 2 * input_data.distance_between_boards;
    double usable_height = board->height - 2 * input_data.distance_between_boards;

    if (piece->width > usable_width || piece->height > usable_height) {
        return best_pos;
    }

    // Primeira peça - canto inferior esquerdo
    if (board->piece_count == 0) {
        Point first_pos = {min_x, min_y};
        if (piece_fits_in_board(piece, first_pos, board)) {
            return first_pos;
        }
        return best_pos;
    }

    // Buscar posições de contato com peças existentes
    for (int i = 0; i < board->piece_count; i++) {
        PlacedPiece* existing = &board->placed_pieces[i];

        double ex_min_x = existing->rotated_piece.min_x + existing->position.x;
        double ex_min_y = existing->rotated_piece.min_y + existing->position.y;
        double ex_max_x = existing->rotated_piece.max_x + existing->position.x;
        double ex_max_y = existing->rotated_piece.max_y + existing->position.y;

        Point contact_positions[6] = {
            {ex_max_x + input_data.distance_between_pieces, ex_min_y},
            {ex_max_x + input_data.distance_between_pieces, ex_max_y - piece->height},
            {ex_min_x, ex_max_y + input_data.distance_between_pieces},
            {ex_max_x - piece->width, ex_max_y + input_data.distance_between_pieces},
            {ex_min_x - piece->width - input_data.distance_between_pieces, ex_min_y},
            {ex_min_x, ex_min_y - piece->height - input_data.distance_between_pieces}
        };

        for (int j = 0; j < 6; j++) {
            Point pos = contact_positions[j];

            if (piece_fits_in_board(piece, pos, board)) {
                // MODIFICADO: Empilhamento esquerda-direita
                // Peso alto em X (3.0) prioriza posicionamento à esquerda
                // Peso baixo em Y (0.5) permite empilhamento vertical
                // Resultado: peças se acumulam à esquerda, deixando espaço livre à direita
                double score = pos.x * 3.0 + pos.y * 0.5;
                if (score < best_score) {
                    best_score = score;
                    best_pos = pos;
                }
            }
        }
    }

    // Se não encontrou posição de contato, busca em grid
    if (best_pos.x < 0) {
        double max_x = board->width - piece->width - input_data.distance_between_boards;
        double max_y = board->height - piece->height - input_data.distance_between_boards;

        double step = max_double(piece->width, piece->height) * 0.3;
        if (step < 10.0) step = 10.0;
        if (step > 40.0) step = 40.0;

        int attempts = 0;
        const int max_attempts = 1000;

        for (double x = min_x; x <= max_x && attempts < max_attempts; x += step) {
            for (double y = min_y; y <= max_y && attempts < max_attempts; y += step) {
                attempts++;
                Point pos = {x, y};

                if (piece_fits_in_board(piece, pos, board)) {
                    // MODIFICADO: Empilhamento esquerda-direita (consistente com busca de contato)
                    // Mesma heurística: prioriza X (esquerda) com peso 2.5, Y com peso 0.5
                    double score = x * 2.5 + y * 0.5;
                    if (score < best_score) {
                        best_score = score;
                        best_pos = pos;
                    }
                }
            }
        }
    }

    return best_pos;
}

bool place_piece_on_board_fast(int piece_id, int rotation_idx, Board* board) {
    Piece* original_piece = &input_data.pieces[piece_id];

    int angle = original_piece->allowed_angles[rotation_idx];
    Piece rotated = rotate_piece(original_piece, angle);

    Point best_pos = find_best_position_fast(&rotated, board);

    // CORRIGIDO: NÃO tentar outras rotações! Respeitar o genoma!
    // Se a rotação sugerida não cabe, falha e tenta nova placa.
    // Isso força o GA a encontrar boas combinações de sequência + rotação.
    if (best_pos.x < 0) {
        free(rotated.points);
        return false;
    }

    PlacedPiece* placed = &board->placed_pieces[board->piece_count];
    placed->position = best_pos;
    placed->angle = angle;
    placed->piece_id = piece_id;
    placed->rotated_piece = rotated;

    board->used_area += original_piece->area;
    board->piece_count++;

    return true;
}

// ==================== GENETIC ALGORITHM FUNCTIONS ====================

// Implementacao thread-safe de gerador de numeros aleatorios cross-platform
// Windows nao tem rand_r(), entao implementamos um LCG (Linear Congruential Generator)
static inline int thread_safe_rand(unsigned int* seed_ptr) {
    #ifdef _OPENMP
        #ifdef _WIN32
            // Windows: Implementar LCG thread-safe (algoritmo do glibc)
            // Formula: next = (seed * 1103515245 + 12345) & 0x7fffffff
            // Este algoritmo garante thread-safety sem locks
            unsigned int next = *seed_ptr;
            next = next * 1103515245 + 12345;
            *seed_ptr = next;
            return (int)((next >> 16) & 0x7fff);  // Retorna 15 bits (0-32767)
        #else
            // Linux/POSIX: usar rand_r() nativo
            return rand_r(seed_ptr);
        #endif
    #else
        // Modo serial: usar rand() padrao
        (void)seed_ptr;  // Evitar warning de parametro nao usado
        return rand();
    #endif
}

// Obter ponteiro para seed da thread atual
static inline unsigned int* get_thread_seed() {
    #ifdef _OPENMP
        int tid = omp_get_thread_num();
        if (tid < max_threads && thread_seeds != NULL) {
            return &thread_seeds[tid];
        }
        // Fallback: usar seed estatica (nao deveria acontecer)
        static unsigned int fallback_seed = 0;
        return &fallback_seed;
    #else
        // Modo serial: seed dummy
        static unsigned int dummy_seed = 0;
        return &dummy_seed;
    #endif
}

Genome create_random_genome() {
    Genome genome;
    genome.piece_sequence = malloc(sizeof(int) * input_data.piece_count);
    genome.rotation_choices = malloc(sizeof(int) * input_data.piece_count);
    genome.fitness = 0.0;
    genome.board_count = 0;
    genome.total_efficiency = 0.0;

    unsigned int* seed = get_thread_seed();

    for (int i = 0; i < input_data.piece_count; i++) {
        genome.piece_sequence[i] = i;
    }

    // Fisher-Yates shuffle otimizado - THREAD-SAFE
    for (int i = input_data.piece_count - 1; i > 0; i--) {
        int j = thread_safe_rand(seed) % (i + 1);
        int temp = genome.piece_sequence[i];
        genome.piece_sequence[i] = genome.piece_sequence[j];
        genome.piece_sequence[j] = temp;
    }

    // CORRIGIDO: rotation_choices indexado por piece_id - THREAD-SAFE
    for (int piece_id = 0; piece_id < input_data.piece_count; piece_id++) {
        int angle_count = input_data.pieces[piece_id].angle_count;
        genome.rotation_choices[piece_id] = thread_safe_rand(seed) % angle_count;
    }

    return genome;
}

Genome create_greedy_genome() {
    Genome genome;
    genome.piece_sequence = malloc(sizeof(int) * input_data.piece_count);
    genome.rotation_choices = malloc(sizeof(int) * input_data.piece_count);
    genome.fitness = 0.0;
    genome.board_count = 0;
    genome.total_efficiency = 0.0;

    typedef struct { int id; double area; } PieceArea;
    PieceArea* pieces_by_area = malloc(sizeof(PieceArea) * input_data.piece_count);

    for (int i = 0; i < input_data.piece_count; i++) {
        pieces_by_area[i].id = i;
        pieces_by_area[i].area = input_data.pieces[i].area;
    }

    // Quicksort seria melhor, mas mantendo bubble sort por simplicidade
    for (int i = 0; i < input_data.piece_count - 1; i++) {
        for (int j = 0; j < input_data.piece_count - i - 1; j++) {
            if (pieces_by_area[j].area < pieces_by_area[j + 1].area) {
                PieceArea temp = pieces_by_area[j];
                pieces_by_area[j] = pieces_by_area[j + 1];
                pieces_by_area[j + 1] = temp;
            }
        }
    }

    for (int i = 0; i < input_data.piece_count; i++) {
        genome.piece_sequence[i] = pieces_by_area[i].id;
    }

    free(pieces_by_area);

    // CORRIGIDO: rotation_choices indexado por piece_id
    for (int piece_id = 0; piece_id < input_data.piece_count; piece_id++) {
        genome.rotation_choices[piece_id] = 0;
    }

    return genome;
}

void evaluate_genome(Genome* genome) {
    // Thread-safe: cada thread usa sua própria estrutura Result local
    Result local_result;
    local_result.boards = malloc(sizeof(Board) * MAX_BOARDS);
    local_result.board_count = 0;

    bool* placed = calloc(input_data.piece_count, sizeof(bool));
    int placed_count = 0;

    for (int seq_idx = 0; seq_idx < input_data.piece_count; seq_idx++) {
        int piece_id = genome->piece_sequence[seq_idx];
        int rotation_idx = genome->rotation_choices[piece_id];  // CORRIGIDO: usar piece_id como índice!

        if (placed[piece_id]) continue;

        bool piece_placed = false;

        for (int board_idx = 0; board_idx < local_result.board_count; board_idx++) {
            if (place_piece_on_board_fast(piece_id, rotation_idx, &local_result.boards[board_idx])) {
                placed[piece_id] = true;
                placed_count++;
                piece_placed = true;
                break;
            }
        }

        if (!piece_placed && local_result.board_count < MAX_BOARDS) {
            Board* new_board = &local_result.boards[local_result.board_count];
            new_board->width = input_data.board_x;
            new_board->height = input_data.board_y;
            new_board->placed_pieces = malloc(sizeof(PlacedPiece) * MAX_PIECES);
            new_board->piece_count = 0;
            new_board->used_area = 0;

            if (place_piece_on_board_fast(piece_id, rotation_idx, new_board)) {
                placed[piece_id] = true;
                placed_count++;
                piece_placed = true;
                local_result.board_count++;
            }
        }
    }

    double total_used_area = 0;
    for (int i = 0; i < local_result.board_count; i++) {
        double board_area = local_result.boards[i].width * local_result.boards[i].height;
        local_result.boards[i].efficiency = (local_result.boards[i].used_area / board_area) * 100.0;
        total_used_area += local_result.boards[i].used_area;
    }

    double total_board_area = local_result.board_count * input_data.board_x * input_data.board_y;
    local_result.total_efficiency = total_board_area > 0 ? (total_used_area / total_board_area) * 100.0 : 0.0;

    genome->fitness = local_result.total_efficiency * 2.0 - local_result.board_count * 5.0;
    genome->board_count = local_result.board_count;
    genome->total_efficiency = local_result.total_efficiency;

    if (placed_count < input_data.piece_count) {
        genome->fitness -= (input_data.piece_count - placed_count) * 1000.0;

        #ifdef _OPENMP
            #pragma omp critical
        #endif
        {
            static bool logged = false;
            if (!logged) {
                printf("\n[AVISO] Pecas nao colocadas: ");
                for (int i = 0; i < input_data.piece_count; i++) {
                    if (!placed[i]) {
                        printf("%d ", i);
                    }
                }
                printf("(total: %d)\n\n", input_data.piece_count - placed_count);
                logged = true;
            }
        }
    }

    // Limpar resultado local
    for (int i = 0; i < local_result.board_count; i++) {
        for (int j = 0; j < local_result.boards[i].piece_count; j++) {
            free(local_result.boards[i].placed_pieces[j].rotated_piece.points);
        }
        free(local_result.boards[i].placed_pieces);
    }
    free(local_result.boards);
    free(placed);
}

int tournament_selection(Genome* population, int pop_size) {
    unsigned int* seed = get_thread_seed();

    int best_idx = thread_safe_rand(seed) % pop_size;
    double best_fitness = population[best_idx].fitness;

    for (int i = 1; i < TOURNAMENT_SIZE; i++) {
        int candidate_idx = thread_safe_rand(seed) % pop_size;
        if (population[candidate_idx].fitness > best_fitness) {
            best_idx = candidate_idx;
            best_fitness = population[candidate_idx].fitness;
        }
    }

    return best_idx;
}

Genome order_crossover(Genome* parent1, Genome* parent2) {
    Genome child;
    child.piece_sequence = malloc(sizeof(int) * input_data.piece_count);
    child.rotation_choices = malloc(sizeof(int) * input_data.piece_count);
    child.fitness = 0.0;
    child.board_count = 0;
    child.total_efficiency = 0.0;

    unsigned int* seed = get_thread_seed();

    for (int i = 0; i < input_data.piece_count; i++) {
        child.piece_sequence[i] = -1;
    }

    int cut1 = thread_safe_rand(seed) % input_data.piece_count;
    int cut2 = thread_safe_rand(seed) % input_data.piece_count;
    if (cut1 > cut2) {
        int temp = cut1;
        cut1 = cut2;
        cut2 = temp;
    }

    for (int i = cut1; i <= cut2; i++) {
        child.piece_sequence[i] = parent1->piece_sequence[i];
    }

    int child_idx = (cut2 + 1) % input_data.piece_count;
    for (int parent2_idx = 0; parent2_idx < input_data.piece_count; parent2_idx++) {
        int gene = parent2->piece_sequence[(cut2 + 1 + parent2_idx) % input_data.piece_count];

        bool already_in = false;
        for (int i = cut1; i <= cut2; i++) {
            if (child.piece_sequence[i] == gene) {
                already_in = true;
                break;
            }
        }

        if (!already_in) {
            child.piece_sequence[child_idx] = gene;
            child_idx = (child_idx + 1) % input_data.piece_count;
        }
    }

    // CORRIGIDO: rotation_choices é indexado por piece_id, então herda diretamente dos pais - THREAD-SAFE
    for (int piece_id = 0; piece_id < input_data.piece_count; piece_id++) {
        if (thread_safe_rand(seed) % 2 == 0) {
            child.rotation_choices[piece_id] = parent1->rotation_choices[piece_id];
        } else {
            child.rotation_choices[piece_id] = parent2->rotation_choices[piece_id];
        }
    }

    return child;
}

void mutate_genome(Genome* genome) {
    unsigned int* seed = get_thread_seed();

    // CORRIGIDO: Mutação mais agressiva para manter diversidade - THREAD-SAFE
    // Swap mutation: sempre fazer pelo menos 1 swap, às vezes mais
    int num_swaps = 2 + thread_safe_rand(seed) % 3;  // 2-4 swaps
    for (int m = 0; m < num_swaps; m++) {
        if ((double)thread_safe_rand(seed) / RAND_MAX < MUTATION_RATE) {
            int pos1 = thread_safe_rand(seed) % input_data.piece_count;
            int pos2 = thread_safe_rand(seed) % input_data.piece_count;

            int temp = genome->piece_sequence[pos1];
            genome->piece_sequence[pos1] = genome->piece_sequence[pos2];
            genome->piece_sequence[pos2] = temp;
        }
    }

    // Rotation mutation: mudar rotação de várias peças - THREAD-SAFE
    int num_rotations = 3 + thread_safe_rand(seed) % 4;  // 3-6 rotações
    for (int m = 0; m < num_rotations; m++) {
        if ((double)thread_safe_rand(seed) / RAND_MAX < MUTATION_RATE) {
            int piece_id = thread_safe_rand(seed) % input_data.piece_count;
            int angle_count = input_data.pieces[piece_id].angle_count;
            if (angle_count > 1) {
                genome->rotation_choices[piece_id] = thread_safe_rand(seed) % angle_count;
            }
        }
    }
}

Genome copy_genome(Genome* source) {
    Genome copy;
    copy.piece_sequence = malloc(sizeof(int) * input_data.piece_count);
    copy.rotation_choices = malloc(sizeof(int) * input_data.piece_count);

    memcpy(copy.piece_sequence, source->piece_sequence, sizeof(int) * input_data.piece_count);
    memcpy(copy.rotation_choices, source->rotation_choices, sizeof(int) * input_data.piece_count);

    copy.fitness = source->fitness;
    copy.board_count = source->board_count;
    copy.total_efficiency = source->total_efficiency;

    return copy;
}

void free_genome(Genome* genome) {
    free(genome->piece_sequence);
    free(genome->rotation_choices);
}

// Avalia um genoma e salva o resultado na estrutura global 'result'
void evaluate_genome_to_global(Genome* genome) {
    if (result.boards) {
        for (int i = 0; i < result.board_count; i++) {
            for (int j = 0; j < result.boards[i].piece_count; j++) {
                free(result.boards[i].placed_pieces[j].rotated_piece.points);
            }
            free(result.boards[i].placed_pieces);
        }
        free(result.boards);
    }

    result.boards = malloc(sizeof(Board) * MAX_BOARDS);
    result.board_count = 0;

    bool* placed = calloc(input_data.piece_count, sizeof(bool));
    int placed_count = 0;

    for (int seq_idx = 0; seq_idx < input_data.piece_count; seq_idx++) {
        int piece_id = genome->piece_sequence[seq_idx];
        int rotation_idx = genome->rotation_choices[piece_id];

        if (placed[piece_id]) continue;

        bool piece_placed = false;

        for (int board_idx = 0; board_idx < result.board_count; board_idx++) {
            if (place_piece_on_board_fast(piece_id, rotation_idx, &result.boards[board_idx])) {
                placed[piece_id] = true;
                placed_count++;
                piece_placed = true;
                break;
            }
        }

        if (!piece_placed && result.board_count < MAX_BOARDS) {
            Board* new_board = &result.boards[result.board_count];
            new_board->width = input_data.board_x;
            new_board->height = input_data.board_y;
            new_board->placed_pieces = malloc(sizeof(PlacedPiece) * MAX_PIECES);
            new_board->piece_count = 0;
            new_board->used_area = 0;

            if (place_piece_on_board_fast(piece_id, rotation_idx, new_board)) {
                placed[piece_id] = true;
                placed_count++;
                piece_placed = true;
                result.board_count++;
            }
        }
    }

    double total_used_area = 0;
    for (int i = 0; i < result.board_count; i++) {
        double board_area = result.boards[i].width * result.boards[i].height;
        result.boards[i].efficiency = (result.boards[i].used_area / board_area) * 100.0;
        total_used_area += result.boards[i].used_area;
    }

    double total_board_area = result.board_count * input_data.board_x * input_data.board_y;
    result.total_efficiency = total_board_area > 0 ? (total_used_area / total_board_area) * 100.0 : 0.0;

    genome->fitness = result.total_efficiency * 2.0 - result.board_count * 5.0;
    genome->board_count = result.board_count;
    genome->total_efficiency = result.total_efficiency;

    free(placed);
}

void save_best_result() {
    if (best_result.boards) {
        for (int i = 0; i < best_result.board_count; i++) {
            for (int j = 0; j < best_result.boards[i].piece_count; j++) {
                free(best_result.boards[i].placed_pieces[j].rotated_piece.points);
            }
            free(best_result.boards[i].placed_pieces);
        }
        free(best_result.boards);
    }

    best_result.board_count = result.board_count;
    best_result.total_efficiency = result.total_efficiency;
    best_result.boards = malloc(sizeof(Board) * result.board_count);

    for (int i = 0; i < result.board_count; i++) {
        best_result.boards[i].width = result.boards[i].width;
        best_result.boards[i].height = result.boards[i].height;
        best_result.boards[i].used_area = result.boards[i].used_area;
        best_result.boards[i].efficiency = result.boards[i].efficiency;
        best_result.boards[i].piece_count = result.boards[i].piece_count;
        best_result.boards[i].placed_pieces = malloc(sizeof(PlacedPiece) * result.boards[i].piece_count);

        for (int j = 0; j < result.boards[i].piece_count; j++) {
            best_result.boards[i].placed_pieces[j] = result.boards[i].placed_pieces[j];

            PlacedPiece* src = &result.boards[i].placed_pieces[j];
            PlacedPiece* dst = &best_result.boards[i].placed_pieces[j];

            dst->rotated_piece.points = malloc(sizeof(Point) * src->rotated_piece.point_count);
            memcpy(dst->rotated_piece.points, src->rotated_piece.points,
                   sizeof(Point) * src->rotated_piece.point_count);
        }
    }
}

#if ENABLE_CONCAVE_NESTING
// ==================== CONCAVE NESTING OPTIMIZATION (PHASE 3) ====================

/**
 * Calculate concavity ratio for a piece.
 * Returns ratio of empty space in bounding box (1.0 - polygon_area/bbox_area)
 * Higher values indicate more concavity.
 */
double calculate_concavity_ratio(Piece* piece) {
    double bbox_area = piece->width * piece->height;
    if (bbox_area < 1e-10) return 0.0;

    double polygon_area = piece->area;
    double ratio = 1.0 - (polygon_area / bbox_area);

    return (ratio < 0.0) ? 0.0 : ratio;
}

/**
 * Sample concave regions using grid-based approach.
 * Finds points inside bounding box but outside polygon (concavity).
 * Returns ConcavityInfo with candidate points or NULL if no significant concavity.
 */
ConcavityInfo* sample_concave_regions(Piece* piece, PlacedPiece* placed, int grid_res) {
    double concavity_ratio = calculate_concavity_ratio(piece);

    // Early exit if concavity below threshold
    if (concavity_ratio < CONCAVITY_THRESHOLD) {
        return NULL;
    }

    ConcavityInfo* info = malloc(sizeof(ConcavityInfo));
    if (!info) return NULL;

    info->concavity_ratio = concavity_ratio;

    // Allocate for worst case (all grid points)
    info->points = malloc(sizeof(ConcavePoint) * grid_res * grid_res);
    if (!info->points) {
        free(info);
        return NULL;
    }
    info->num_points = 0;

    // Grid sampling: check each grid point
    double step_x = piece->width / (grid_res - 1);
    double step_y = piece->height / (grid_res - 1);

    for (int iy = 0; iy < grid_res; iy++) {
        for (int ix = 0; ix < grid_res; ix++) {
            // Local coordinates relative to piece
            double local_x = piece->min_x + ix * step_x;
            double local_y = piece->min_y + iy * step_y;

            Point test_point = {local_x, local_y};

            // Check if point is in bbox but NOT in polygon
            bool in_bbox = (local_x >= piece->min_x && local_x <= piece->max_x &&
                           local_y >= piece->min_y && local_y <= piece->max_y);

            bool in_polygon = point_in_polygon(test_point, piece->points, piece->point_count);

            // This is a concave point (in bbox but outside polygon)
            if (in_bbox && !in_polygon) {
                // Convert to world coordinates
                info->points[info->num_points].x = local_x + placed->position.x;
                info->points[info->num_points].y = local_y + placed->position.y;
                info->num_points++;
            }
        }
    }

    // If no concave points found, free and return NULL
    if (info->num_points == 0) {
        free(info->points);
        free(info);
        return NULL;
    }

    // Shrink allocation to actual size
    ConcavePoint* resized = realloc(info->points, sizeof(ConcavePoint) * info->num_points);
    if (resized) {
        info->points = resized;
    }

    return info;
}

/**
 * Try to fit a small piece into a concavity region with sub-grid refinement.
 * Tests multiple positions using ONLY the piece's allowed_angles (respects input_shapes.json constraints).
 * Returns true if piece was successfully repositioned, false otherwise.
 */
bool try_fit_in_concavity(Board* board, int small_piece_idx, ConcavityInfo* concavity, Piece* large_piece) {
    PlacedPiece* small_placed = &board->placed_pieces[small_piece_idx];
    Piece* small_original = &input_data.pieces[small_placed->piece_id];

    #if DEBUG_CONCAVE_NESTING
    int attempts = 0;
    #endif

    // CRITICAL: Use ONLY the piece's allowed rotation angles from input_shapes.json
    // This respects the same constraints used by the genetic algorithm
    int num_allowed_rotations = small_original->angle_count;

    // Try each candidate point in the concavity
    for (int pt_idx = 0; pt_idx < concavity->num_points; pt_idx++) {
        Point candidate_pos = {concavity->points[pt_idx].x, concavity->points[pt_idx].y};

        // Try ONLY allowed rotations for this piece (respects allowed_angles[])
        for (int rot_idx = 0; rot_idx < num_allowed_rotations; rot_idx++) {
            int test_angle = small_original->allowed_angles[rot_idx];

            #if DEBUG_CONCAVE_NESTING
            attempts++;
            #endif

            // Create rotated piece for testing
            Piece test_rotated = rotate_piece(small_original, test_angle);

            // Test if piece fits at this position
            // Need to temporarily remove the piece from board to avoid self-collision
            int original_count = board->piece_count;
            board->piece_count--;

            // Validate fit
            bool fits = piece_fits_in_board(&test_rotated, candidate_pos, board);

            // Restore board state
            board->piece_count = original_count;

            if (fits) {
                // Success! Update the piece position
                free(small_placed->rotated_piece.points);
                small_placed->position = candidate_pos;
                small_placed->angle = test_angle;
                small_placed->rotated_piece = test_rotated;

                #if DEBUG_CONCAVE_NESTING
                printf("      [SUCESSO] Peca %d encaixada em (%.1f, %.1f) com rotacao %d graus\n",
                       small_placed->piece_id, candidate_pos.x, candidate_pos.y, test_angle);
                printf("                Tentativas: %d, Angulos permitidos para esta peca: %d\n",
                       attempts, num_allowed_rotations);
                #endif

                return true;
            } else {
                // Try sub-grid refinement around this position
                double step_size = min_double(large_piece->width, large_piece->height) / (GRID_RESOLUTION * 2.0);

                for (int sub_x = -SUBGRID_RESOLUTION/2; sub_x <= SUBGRID_RESOLUTION/2; sub_x++) {
                    for (int sub_y = -SUBGRID_RESOLUTION/2; sub_y <= SUBGRID_RESOLUTION/2; sub_y++) {
                        if (sub_x == 0 && sub_y == 0) continue; // Already tested

                        Point refined_pos = {
                            candidate_pos.x + sub_x * step_size,
                            candidate_pos.y + sub_y * step_size
                        };

                        board->piece_count--;
                        bool refined_fits = piece_fits_in_board(&test_rotated, refined_pos, board);
                        board->piece_count = original_count;

                        if (refined_fits) {
                            // Found better position with sub-grid refinement!
                            free(small_placed->rotated_piece.points);
                            small_placed->position = refined_pos;
                            small_placed->angle = test_angle;
                            small_placed->rotated_piece = test_rotated;

                            #if DEBUG_CONCAVE_NESTING
                            printf("      [SUCESSO - REFINADO] Peca %d encaixada em (%.1f, %.1f) com rotacao %d graus\n",
                                   small_placed->piece_id, refined_pos.x, refined_pos.y, test_angle);
                            printf("                           Ajuste sub-grid: (%d, %d) offset=(%.1f, %.1f)\n",
                                   sub_x, sub_y, sub_x * step_size, sub_y * step_size);
                            #endif

                            return true;
                        }
                    }
                }

                // Clean up test rotation
                free(test_rotated.points);
            }
        }
    }

    #if DEBUG_CONCAVE_NESTING
    printf("      [FALHA] Peca %d nao encaixou apos %d tentativas\n",
           small_placed->piece_id, attempts);
    printf("              Angulos permitidos testados: %d, Pontos candidatos testados: %d\n",
           num_allowed_rotations, concavity->num_points);
    #endif

    return false;
}

/**
 * Main optimization function for concave nesting (Phase 3).
 * Identifies large pieces with concavities and attempts to fit smaller pieces inside.
 */
void optimize_concave_nesting(Board* board) {
    printf("Analisando concavidades...\n");

    // Track statistics
    int large_pieces_found = 0;
    int repositioning_attempts = 0;
    int successful_repositions = 0;

    // Phase 1: Identify large pieces with concavities
    typedef struct {
        int piece_idx;
        double concavity_ratio;
        double area;
    } LargePiece;

    LargePiece* large_pieces = malloc(sizeof(LargePiece) * board->piece_count);
    int large_count = 0;

    for (int i = 0; i < board->piece_count; i++) {
        PlacedPiece* placed = &board->placed_pieces[i];
        Piece* piece = &placed->rotated_piece;

        double ratio = calculate_concavity_ratio(piece);

        if (ratio >= CONCAVITY_THRESHOLD) {
            large_pieces[large_count].piece_idx = i;
            large_pieces[large_count].concavity_ratio = ratio;
            large_pieces[large_count].area = piece->area;
            large_count++;
            large_pieces_found++;
        }
    }

    printf("  Encontradas %d pecas com concavidades significativas (>%.0f%%)\n",
           large_pieces_found, CONCAVITY_THRESHOLD * 100);

    if (large_count == 0) {
        free(large_pieces);
        printf("  Nenhuma otimizacao possivel.\n");
        return;
    }

    // Sort large pieces by concavity ratio (descending)
    for (int i = 0; i < large_count - 1; i++) {
        for (int j = 0; j < large_count - i - 1; j++) {
            if (large_pieces[j].concavity_ratio < large_pieces[j + 1].concavity_ratio) {
                LargePiece temp = large_pieces[j];
                large_pieces[j] = large_pieces[j + 1];
                large_pieces[j + 1] = temp;
            }
        }
    }

    // Calculate initial efficiency
    double initial_efficiency = board->efficiency;

    // Phase 2: For each large piece, try to fit small pieces in concavity
    for (int lp_idx = 0; lp_idx < large_count; lp_idx++) {
        int large_idx = large_pieces[lp_idx].piece_idx;
        PlacedPiece* large_placed = &board->placed_pieces[large_idx];

        printf("  Analisando peca %d (concavidade: %.1f%%, area: %.0f)...\n",
               large_placed->piece_id,
               large_pieces[lp_idx].concavity_ratio * 100,
               large_pieces[lp_idx].area);

        // Sample concave regions
        ConcavityInfo* concavity = sample_concave_regions(&large_placed->rotated_piece,
                                                          large_placed,
                                                          GRID_RESOLUTION);

        if (!concavity) {
            printf("    Nao foi possivel amostrar pontos candidatos.\n");
            continue;
        }

        printf("    Encontrados %d pontos candidatos na concavidade.\n", concavity->num_points);

        // Find small pieces to try (area < MAX_SMALL_PIECE_RATIO * large_area)
        typedef struct { int idx; double area; } SmallPiece;
        SmallPiece* small_pieces = malloc(sizeof(SmallPiece) * board->piece_count);
        int small_count = 0;

        double max_small_area = large_pieces[lp_idx].area * MAX_SMALL_PIECE_RATIO;

        for (int i = 0; i < board->piece_count; i++) {
            if (i == large_idx) continue; // Skip self

            PlacedPiece* placed = &board->placed_pieces[i];
            Piece* original = &input_data.pieces[placed->piece_id];

            if (original->area <= max_small_area) {
                small_pieces[small_count].idx = i;
                small_pieces[small_count].area = original->area;
                small_count++;
            }
        }

        printf("    Encontradas %d pecas pequenas candidatas (area < %.0f).\n",
               small_count, max_small_area);

        // Sort small pieces by area (ascending - smallest first)
        for (int i = 0; i < small_count - 1; i++) {
            for (int j = 0; j < small_count - i - 1; j++) {
                if (small_pieces[j].area > small_pieces[j + 1].area) {
                    SmallPiece temp = small_pieces[j];
                    small_pieces[j] = small_pieces[j + 1];
                    small_pieces[j + 1] = temp;
                }
            }
        }

        // Try to fit small pieces
        for (int sp_idx = 0; sp_idx < small_count; sp_idx++) {
            int small_idx = small_pieces[sp_idx].idx;
            repositioning_attempts++;

            #if DEBUG_CONCAVE_NESTING
            printf("    Tentando encaixar peca %d (area=%.0f, %.1f%% da peca grande)...\n",
                   board->placed_pieces[small_idx].piece_id,
                   small_pieces[sp_idx].area,
                   (small_pieces[sp_idx].area / large_pieces[lp_idx].area) * 100.0);
            #endif

            if (try_fit_in_concavity(board, small_idx, concavity, &large_placed->rotated_piece)) {
                successful_repositions++;
                #if !DEBUG_CONCAVE_NESTING
                printf("      [OK] Peca %d reposicionada na concavidade!\n",
                       board->placed_pieces[small_idx].piece_id);
                #endif
            }
        }

        free(small_pieces);
        free(concavity->points);
        free(concavity);
    }

    free(large_pieces);

    // Recalculate board efficiency after optimization
    double board_area = board->width * board->height;
    board->efficiency = (board->used_area / board_area) * 100.0;

    printf("\nResultados da otimizacao de concavidades:\n");
    printf("  Pecas com concavidades analisadas: %d\n", large_pieces_found);
    printf("  Tentativas de reposicionamento: %d\n", repositioning_attempts);
    printf("  Reposicionamentos bem-sucedidos: %d\n", successful_repositions);

    if (repositioning_attempts > 0) {
        printf("  Taxa de sucesso: %.1f%%\n",
               (successful_repositions * 100.0) / repositioning_attempts);
    }

    printf("  Eficiencia inicial: %.2f%%\n", initial_efficiency);
    printf("  Eficiencia final: %.2f%%\n", board->efficiency);

    if (board->efficiency > initial_efficiency) {
        printf("  Melhoria: +%.2f%%\n", board->efficiency - initial_efficiency);
    } else if (board->efficiency < initial_efficiency) {
        printf("  [AVISO] Eficiencia reduziu em %.2f%% (possivel bug)\n",
               initial_efficiency - board->efficiency);
    } else {
        printf("  Nenhuma melhoria alcancada nesta placa.\n");
    }
}

#endif // ENABLE_CONCAVE_NESTING

// ==================== PARSING AND OUTPUT ====================

char* read_file(const char* filename) {
    // CORRIGIDO: Usar modo binario "rb" para evitar problemas com conversao CRLF no Windows
    // Isso garante que ftell() retorna o tamanho exato do arquivo
    FILE* file = fopen(filename, "rb");
    if (!file) {
        // CORRIGIDO: Fornecer diagnostico detalhado do erro
        printf("ERRO ao abrir arquivo: %s\n", filename);
        printf("Detalhes: ");

        #ifdef _WIN32
            // Windows: verificar se o arquivo existe
            DWORD attrs = GetFileAttributesA(filename);
            if (attrs == INVALID_FILE_ATTRIBUTES) {
                DWORD error = GetLastError();
                if (error == ERROR_FILE_NOT_FOUND) {
                    printf("Arquivo nao encontrado.\n");
                } else if (error == ERROR_PATH_NOT_FOUND) {
                    printf("Caminho nao encontrado.\n");
                } else if (error == ERROR_ACCESS_DENIED) {
                    printf("Acesso negado (permissao).\n");
                } else {
                    printf("Codigo de erro Windows: %lu\n", error);
                }
            }
        #else
            // Linux/Unix: usar errno para diagnostico
            printf("%s\n", strerror(errno));
        #endif

        // Mostrar diretorio de trabalho atual para debug
        char cwd[1024];
        #ifdef _WIN32
            GetCurrentDirectoryA(sizeof(cwd), cwd);
        #else
            if (getcwd(cwd, sizeof(cwd)) == NULL) {
                strcpy(cwd, "(nao foi possivel determinar)");
            }
        #endif
        printf("Diretorio de trabalho atual: %s\n", cwd);
        printf("\nVERIFIQUE:\n");
        printf("1. O arquivo '%s' existe no mesmo diretorio que o executavel?\n", filename);
        printf("2. O nome do arquivo esta correto (maiusculas/minusculas)?\n");
        printf("3. Voce esta executando o programa do diretorio correto?\n");
        printf("\n");

        return NULL;
    }

    // CORRIGIDO: Verificar se fseek foi bem-sucedido
    if (fseek(file, 0, SEEK_END) != 0) {
        printf("ERRO: Falha ao posicionar no final do arquivo %s\n", filename);
        fclose(file);
        return NULL;
    }

    long size = ftell(file);
    if (size < 0) {
        printf("ERRO: Falha ao obter tamanho do arquivo %s\n", filename);
        fclose(file);
        return NULL;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        printf("ERRO: Falha ao retornar ao inicio do arquivo %s\n", filename);
        fclose(file);
        return NULL;
    }

    // CORRIGIDO: Verificar se malloc foi bem-sucedido
    char* content = malloc(size + 1);
    if (!content) {
        printf("ERRO: Falha ao alocar memoria para ler %s (tamanho: %ld bytes)\n", filename, size);
        fclose(file);
        return NULL;
    }

    size_t bytes_read = fread(content, 1, size, file);
    fclose(file);

    // CORRIGIDO: Em modo binario "rb", bytes_read deve ser exatamente igual a size
    // Se nao for, algo deu errado na leitura
    if (bytes_read != (size_t)size) {
        printf("ERRO: Leitura incompleta do arquivo %s\n", filename);
        printf("Esperado: %ld bytes, Lido: %zu bytes\n", size, bytes_read);
        free(content);
        return NULL;
    }

    content[size] = '\0';
    return content;
}

double parse_number(const char** json) {
    while (**json && (**json == ' ' || **json == '\t' || **json == '\n' || **json == '\r')) (*json)++;

    char* end;
    double value = strtod(*json, &end);
    *json = end;
    return value;
}

void skip_whitespace(const char** json) {
    while (**json && (**json == ' ' || **json == '\t' || **json == '\n' || **json == '\r')) (*json)++;
}

bool parse_input_json(const char* filename) {
    char* json_content = read_file(filename);
    if (!json_content) {
        printf("Erro: Nao foi possivel ler o arquivo %s\n", filename);
        return false;
    }

    const char* json = json_content;

    char* board_x_pos = strstr(json, "\"board_x\"");
    if (!board_x_pos) return false;
    json = board_x_pos + strlen("\"board_x\"");
    while (*json && *json != ':') json++;
    json++;
    input_data.board_x = parse_number(&json);

    char* board_y_pos = strstr(json, "\"board_y\"");
    if (!board_y_pos) return false;
    json = board_y_pos + strlen("\"board_y\"");
    while (*json && *json != ':') json++;
    json++;
    input_data.board_y = parse_number(&json);

    char* dist_boards_pos = strstr(json, "\"distance_between_boards\"");
    if (!dist_boards_pos) return false;
    json = dist_boards_pos + strlen("\"distance_between_boards\"");
    while (*json && *json != ':') json++;
    json++;
    input_data.distance_between_boards = parse_number(&json);

    char* dist_pieces_pos = strstr(json, "\"distance_between_peaces\"");
    if (!dist_pieces_pos) return false;
    json = dist_pieces_pos + strlen("\"distance_between_peaces\"");
    while (*json && *json != ':') json++;
    json++;
    input_data.distance_between_pieces = parse_number(&json);

    char* pieces_pos = strstr(json, "\"peaces\"");
    if (!pieces_pos) return false;
    json = pieces_pos + strlen("\"peaces\"");
    while (*json && *json != '[') json++;
    json++;

    input_data.pieces = malloc(sizeof(Piece) * MAX_PIECES);
    input_data.piece_count = 0;

    while (*json && *json != ']') {
        skip_whitespace(&json);
        if (*json == ']') break;
        if (*json == ',') json++;
        skip_whitespace(&json);
        if (*json == ']') break;
        if (*json != '{') json++;

        Piece* piece = &input_data.pieces[input_data.piece_count];
        piece->id = input_data.piece_count;
        piece->points = malloc(sizeof(Point) * MAX_POINTS);
        piece->point_count = 0;
        piece->allowed_angles = malloc(sizeof(int) * MAX_ANGLES);
        piece->angle_count = 0;

        char* angle_pos = strstr(json, "\"angle\"");
        if (angle_pos) {
            json = angle_pos + strlen("\"angle\"");
            while (*json && *json != '[') json++;
            json++;

            while (*json && *json != ']') {
                skip_whitespace(&json);
                if (*json == ']') break;
                if (*json == ',') json++;
                skip_whitespace(&json);
                if (*json == ']') break;

                piece->allowed_angles[piece->angle_count++] = (int)parse_number(&json);
            }
            json++;
        }

        char* data_pos = strstr(json, "\"data\"");
        if (data_pos) {
            json = data_pos + strlen("\"data\"");
            while (*json && *json != '[') json++;
            json++;

            while (*json && *json != ']') {
                skip_whitespace(&json);
                if (*json == ']') break;
                if (*json == ',') json++;
                skip_whitespace(&json);
                if (*json == ']') break;
                if (*json == '[') json++;

                Point* point = &piece->points[piece->point_count];
                point->x = parse_number(&json);

                skip_whitespace(&json);
                if (*json == ',') json++;
                skip_whitespace(&json);

                point->y = parse_number(&json);
                piece->point_count++;

                while (*json && *json != ']' && *json != '[' && *json != ',') json++;
                if (*json == ']') json++;
            }
        }

        double min_x, min_y, max_x, max_y;
        if (piece->point_count > 0) {
            min_x = max_x = piece->points[0].x;
            min_y = max_y = piece->points[0].y;

            for (int i = 1; i < piece->point_count; i++) {
                if (piece->points[i].x < min_x) min_x = piece->points[i].x;
                if (piece->points[i].x > max_x) max_x = piece->points[i].x;
                if (piece->points[i].y < min_y) min_y = piece->points[i].y;
                if (piece->points[i].y > max_y) max_y = piece->points[i].y;
            }

            for (int i = 0; i < piece->point_count; i++) {
                piece->points[i].x -= min_x;
                piece->points[i].y -= min_y;
            }
        }

        calculate_bounding_box_cached(piece);
        piece->width = piece->max_x - piece->min_x;
        piece->height = piece->max_y - piece->min_y;
        piece->area = calculate_polygon_area(piece->points, piece->point_count);

        input_data.piece_count++;

        int brace_count = 1;
        while (*json && brace_count > 0) {
            if (*json == '{') brace_count++;
            else if (*json == '}') brace_count--;
            json++;
        }
    }

    free(json_content);
    return true;
}

void write_output_json(const char* filename) {
    // CORRIGIDO: Usar modo "wb" para garantir escrita binaria consistente
    FILE* file = fopen(filename, "wb");
    if (!file) {
        printf("ERRO: Nao foi possivel criar/escrever o arquivo %s\n", filename);
        printf("Detalhes: ");

        #ifdef _WIN32
            DWORD error = GetLastError();
            if (error == ERROR_ACCESS_DENIED) {
                printf("Acesso negado. Verifique permissoes de escrita.\n");
            } else if (error == ERROR_PATH_NOT_FOUND) {
                printf("Caminho nao encontrado.\n");
            } else {
                printf("Codigo de erro Windows: %lu\n", error);
            }
        #else
            printf("%s\n", strerror(errno));
        #endif

        return;
    }

    fprintf(file, "{\n");
    fprintf(file, "  \"board_count\": %d,\n", best_result.board_count);
    fprintf(file, "  \"board_x\": %.2f,\n", input_data.board_x);
    fprintf(file, "  \"board_y\": %.2f,\n", input_data.board_y);
    fprintf(file, "  \"total_efficiency\": %.2f,\n", best_result.total_efficiency);
    fprintf(file, "  \"execution_time\": %.3f,\n", best_result.execution_time);
    fprintf(file, "  \"boards\": [\n");

    for (int i = 0; i < best_result.board_count; i++) {
        Board* board = &best_result.boards[i];
        fprintf(file, "    {\n");
        fprintf(file, "      \"board_id\": %d,\n", i);
        fprintf(file, "      \"efficiency\": %.2f,\n", board->efficiency);
        fprintf(file, "      \"piece_count\": %d,\n", board->piece_count);
        fprintf(file, "      \"pieces\": [\n");

        for (int j = 0; j < board->piece_count; j++) {
            PlacedPiece* piece = &board->placed_pieces[j];
            fprintf(file, "        {\n");
            fprintf(file, "          \"piece_id\": %d,\n", piece->piece_id);
            fprintf(file, "          \"position_x\": %.2f,\n", piece->position.x);
            fprintf(file, "          \"position_y\": %.2f,\n", piece->position.y);
            fprintf(file, "          \"angle\": %d,\n", piece->angle);

            fprintf(file, "          \"data\": [\n");
            for (int k = 0; k < piece->rotated_piece.point_count; k++) {
                double world_x = piece->rotated_piece.points[k].x + piece->position.x;
                double world_y = piece->rotated_piece.points[k].y + piece->position.y;

                fprintf(file, "            [\n");
                fprintf(file, "                %.6f,\n", world_x);
                fprintf(file, "                %.6f\n", world_y);
                fprintf(file, "            ]%s\n",
                       (k < piece->rotated_piece.point_count - 1) ? "," : "");
            }
            fprintf(file, "          ]\n");

            fprintf(file, "        }%s\n", (j < board->piece_count - 1) ? "," : "");
        }

        fprintf(file, "      ]\n");
        fprintf(file, "    }%s\n", (i < best_result.board_count - 1) ? "," : "");
    }

    fprintf(file, "  ]\n");
    fprintf(file, "}\n");

    fclose(file);
}

// ==================== MAIN ====================

int main(int argc, char* argv[]) {
    printf("========================================\n");
    printf("  ALGORITMO GENETICO OTIMIZADO - NESTING\n");
    printf("========================================\n\n");

    // Informações sobre paralelização OpenMP
    #ifdef _OPENMP
        int num_threads = omp_get_max_threads();
        printf("OpenMP ATIVADO: %d threads disponiveis\n", num_threads);
        printf("Versao OpenMP: %d\n\n", _OPENMP);
    #else
        printf("OpenMP DESATIVADO: execucao serial\n\n");
    #endif

    // Inicialização melhorada do gerador de números aleatórios
    unsigned int seed;

    if (argc > 1) {
        // Se passar um argumento, usa como seed fixa para reprodutibilidade
        seed = (unsigned int)atoi(argv[1]);
        printf("MODO REPRODUTIVEL: usando seed fixa = %u\n\n", seed);
    } else {
        // Caso contrário, usa método mais robusto para aleatoriedade verdadeira
        // Combina tempo em microsegundos + PID para garantir unicidade
        #ifdef _WIN32
            // Windows: usa GetTickCount64 + PID + time
            LARGE_INTEGER counter;
            QueryPerformanceCounter(&counter);
            seed = (unsigned int)(counter.QuadPart ^ time(NULL) ^ (getpid() << 16));
        #else
            // Linux: usa clock_gettime
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            seed = (unsigned int)(ts.tv_sec ^ ts.tv_nsec ^ (getpid() << 16));
        #endif
        printf("MODO ALEATORIO: seed gerada = %u\n", seed);
        printf("(Para reproduzir este resultado, execute: %s %u)\n\n", argv[0], seed);
    }

    srand(seed);
    init_trig_cache();

    // Inicializar seeds thread-local para OpenMP
    #ifdef _OPENMP
        max_threads = omp_get_max_threads();
        thread_seeds = malloc(sizeof(unsigned int) * max_threads);

        // Cada thread recebe uma seed unica derivada da seed principal
        for (int i = 0; i < max_threads; i++) {
            // Usar uma combinacao da seed principal + thread ID para garantir unicidade
            // mas ainda permitir reprodutibilidade se a seed principal for fixa
            thread_seeds[i] = seed + (i * 1234567891u);
        }

        printf("Seeds das threads inicializadas: %d threads\n\n", max_threads);
    #endif

    clock_t start_time = clock();

    if (!parse_input_json("input_shapes.json")) {
        printf("Erro: Falha ao carregar input_shapes.json\n");
        return 1;
    }

    printf("Carregado: %d pecas\n", input_data.piece_count);
    printf("Dimensoes da placa: %.2f x %.2f\n", input_data.board_x, input_data.board_y);
    printf("Distancia entre pecas: %.2f\n", input_data.distance_between_pieces);
    printf("Margem da placa: %.2f\n\n", input_data.distance_between_boards);

    printf("Parametros do AG:\n");
    printf("  Populacao: %d\n", POPULATION_SIZE);
    printf("  Geracoes: %d\n", GENERATIONS);
    printf("  Taxa de mutacao: %.2f%%\n", MUTATION_RATE * 100);
    printf("  Tamanho do torneio: %d\n", TOURNAMENT_SIZE);
    printf("  Elite preservada: %d\n\n", ELITE_SIZE);

    printf("Inicializando populacao...\n");
    Genome* population = malloc(sizeof(Genome) * POPULATION_SIZE);

    int greedy_count = POPULATION_SIZE / 10;
    for (int i = 0; i < greedy_count; i++) {
        population[i] = create_greedy_genome();
    }
    for (int i = greedy_count; i < POPULATION_SIZE; i++) {
        population[i] = create_random_genome();
    }

    printf("Avaliando populacao inicial...\n");
    #ifdef _OPENMP
        #pragma omp parallel for schedule(dynamic)
    #endif
    for (int i = 0; i < POPULATION_SIZE; i++) {
        #ifdef _OPENMP
            #pragma omp critical
        #endif
        {
            printf("  Avaliando individuo %d/%d...\r", i+1, POPULATION_SIZE);
            fflush(stdout);
        }
        evaluate_genome(&population[i]);
    }
    printf("\n");

    int best_idx = 0;
    double min_fitness = population[0].fitness;
    double max_fitness = population[0].fitness;
    for (int i = 1; i < POPULATION_SIZE; i++) {
        if (population[i].fitness > population[best_idx].fitness) {
            best_idx = i;
        }
        if (population[i].fitness < min_fitness) min_fitness = population[i].fitness;
        if (population[i].fitness > max_fitness) max_fitness = population[i].fitness;
    }

    printf("\nMelhor inicial: %d placas, %.2f%% eff, fitness=%.2f\n",
           population[best_idx].board_count,
           population[best_idx].total_efficiency,
           population[best_idx].fitness);
    printf("Range de fitness: min=%.2f, max=%.2f, diff=%.2f\n\n",
           min_fitness, max_fitness, max_fitness - min_fitness);

    evaluate_genome_to_global(&population[best_idx]);
    save_best_result();

    printf("Iniciando evolucao...\n");
    printf("=========================================\n");

    for (int gen = 0; gen < GENERATIONS; gen++) {
        // Ordenar populacao por fitness (decrescente) - bubble sort
        for (int i = 0; i < POPULATION_SIZE - 1; i++) {
            for (int j = 0; j < POPULATION_SIZE - i - 1; j++) {
                if (population[j].fitness < population[j + 1].fitness) {
                    Genome temp = population[j];
                    population[j] = population[j + 1];
                    population[j + 1] = temp;
                }
            }
        }

        // CORRIGIDO: Comparação direta de fitness
        double current_best_fitness = best_result.total_efficiency * 2.0 - best_result.board_count * 5.0;
        if (population[0].fitness > current_best_fitness) {
            evaluate_genome_to_global(&population[0]);
            save_best_result();
        }

        // Mostrar progresso a cada 5 gerações ou na última
        if (gen % 5 == 0 || gen == GENERATIONS - 1) {
            double avg_fitness = 0;
            double min_gen_fit = population[0].fitness;
            double max_gen_fit = population[0].fitness;

            #ifdef _OPENMP
                #pragma omp parallel for reduction(+:avg_fitness) reduction(min:min_gen_fit) reduction(max:max_gen_fit)
            #endif
            for (int i = 0; i < POPULATION_SIZE; i++) {
                avg_fitness += population[i].fitness;
                if (population[i].fitness < min_gen_fit) min_gen_fit = population[i].fitness;
                if (population[i].fitness > max_gen_fit) max_gen_fit = population[i].fitness;
            }
            avg_fitness /= POPULATION_SIZE;

            printf("Geracao %4d: Melhor=%d placas, %.2f%% eff, fitness=%.2f | Media=%.2f\n",
                   gen,
                   population[0].board_count,
                   population[0].total_efficiency,
                   population[0].fitness,
                   avg_fitness);
        }

        Genome* new_population = malloc(sizeof(Genome) * POPULATION_SIZE);

        for (int i = 0; i < ELITE_SIZE; i++) {
            new_population[i] = copy_genome(&population[i]);
        }

        #ifdef _OPENMP
            #pragma omp parallel for schedule(dynamic)
        #endif
        for (int i = ELITE_SIZE; i < POPULATION_SIZE; i++) {
            int parent1_idx, parent2_idx;

            #ifdef _OPENMP
                #pragma omp critical
            #endif
            {
                parent1_idx = tournament_selection(population, POPULATION_SIZE);
                parent2_idx = tournament_selection(population, POPULATION_SIZE);
            }

            Genome child = order_crossover(&population[parent1_idx], &population[parent2_idx]);
            mutate_genome(&child);
            evaluate_genome(&child);

            new_population[i] = child;
        }

        for (int i = 0; i < POPULATION_SIZE; i++) {
            free_genome(&population[i]);
        }
        free(population);

        population = new_population;
    }

    printf("=========================================\n\n");

    clock_t end_time = clock();
    best_result.execution_time = ((double)(end_time - start_time)) / CLOCKS_PER_SEC;

    printf("\n========================================\n");
    printf("  RESULTADO FINAL\n");
    printf("========================================\n");
    printf("Placas utilizadas: %d\n", best_result.board_count);
    printf("Eficiencia total: %.2f%%\n", best_result.total_efficiency);
    printf("Tempo de execucao: %.2f segundos\n", best_result.execution_time);
    printf("\nDetalhamento por placa:\n");

    for (int i = 0; i < best_result.board_count; i++) {
        printf("  Placa %d: %d pecas, %.2f%% eficiencia\n",
               i + 1,
               best_result.boards[i].piece_count,
               best_result.boards[i].efficiency);
    }

    write_output_json("genetic_nesting_optimized_result.json");
    printf("\nResultado salvo em: genetic_nesting_optimized_result.json\n");

#if ENABLE_CONCAVE_NESTING
    // ==================== PHASE 3: CONCAVE NESTING OPTIMIZATION ====================
    printf("\n========================================\n");
    printf("  FASE 3: OTIMIZACAO DE CONCAVIDADES\n");
    printf("========================================\n\n");

    printf("Parametros de precisao configurados:\n");
    printf("  Grid principal: %dx%d (%d pontos candidatos por peca)\n",
           GRID_RESOLUTION, GRID_RESOLUTION, GRID_RESOLUTION * GRID_RESOLUTION);
    printf("  Sub-grid de refinamento: %dx%d pontos\n",
           SUBGRID_RESOLUTION, SUBGRID_RESOLUTION);
    printf("  Rotacoes: Usa allowed_angles de cada peca (respeita input_shapes.json)\n");
    printf("  Threshold de concavidade: %.0f%% de espaco vazio\n",
           CONCAVITY_THRESHOLD * 100);
    printf("  Tamanho maximo de peca pequena: %.0f%% da peca grande\n\n",
           MAX_SMALL_PIECE_RATIO * 100);

    // Optimization is applied to each board independently
    double total_initial_efficiency = best_result.total_efficiency;

    for (int board_idx = 0; board_idx < best_result.board_count; board_idx++) {
        printf("Otimizando Placa %d/%d:\n", board_idx + 1, best_result.board_count);
        optimize_concave_nesting(&best_result.boards[board_idx]);
        printf("\n");
    }

    // Recalculate total efficiency after all boards optimized
    double total_used_area = 0;
    for (int i = 0; i < best_result.board_count; i++) {
        total_used_area += best_result.boards[i].used_area;
    }
    double total_board_area = best_result.board_count * input_data.board_x * input_data.board_y;
    best_result.total_efficiency = total_board_area > 0 ? (total_used_area / total_board_area) * 100.0 : 0.0;

    printf("========================================\n");
    printf("  RESUMO DA FASE 3\n");
    printf("========================================\n");
    printf("Eficiencia total inicial: %.2f%%\n", total_initial_efficiency);
    printf("Eficiencia total final: %.2f%%\n", best_result.total_efficiency);

    if (best_result.total_efficiency > total_initial_efficiency) {
        printf("Melhoria total: +%.2f%%\n", best_result.total_efficiency - total_initial_efficiency);

        // Save optimized result
        write_output_json("genetic_nesting_optimized_result.json");
        printf("\nResultado otimizado salvo em: genetic_nesting_optimized_result.json\n");
    } else {
        printf("Nenhuma melhoria significativa obtida.\n");
    }

    printf("========================================\n\n");
#endif // ENABLE_CONCAVE_NESTING

    for (int i = 0; i < POPULATION_SIZE; i++) {
        free_genome(&population[i]);
    }
    free(population);

    for (int i = 0; i < input_data.piece_count; i++) {
        free(input_data.pieces[i].points);
        free(input_data.pieces[i].allowed_angles);
    }
    free(input_data.pieces);

    for (int i = 0; i < best_result.board_count; i++) {
        for (int j = 0; j < best_result.boards[i].piece_count; j++) {
            free(best_result.boards[i].placed_pieces[j].rotated_piece.points);
        }
        free(best_result.boards[i].placed_pieces);
    }
    free(best_result.boards);

    // Liberar seeds das threads
    #ifdef _OPENMP
        if (thread_seeds != NULL) {
            free(thread_seeds);
            thread_seeds = NULL;
        }
    #endif

    printf("\n========================================\n");
    printf("  EXECUCAO CONCLUIDA COM SUCESSO\n");
    printf("========================================\n");

    return 0;
}
