// Фильтр Собела в несколько потоков
// Запуск:  sobel.exe вход.bmp выход.bmp [потоки] [повторы]
//          потоки = 0 (или не указано) - прогон для 1,2,4,8,16,32 потоков и таблица времени
//          повторы - сколько раз повторить замер (берётся среднее), по умолчанию 5
// Вход: несжатый BMP 24 или 32 бит/пиксель. Выход: BMP с границами (оттенки серого).
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct {
    int w, h;
    unsigned char *gray;  // исходное изображение в оттенках серого, w*h
    unsigned char *out;   // результат, w*h
} Image;

typedef struct {
    const Image *img;
    int row0, row1;       // обрабатываемые строки [row0, row1)
} Job;

static int load_bmp(const char *path, Image *img)
{
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "Не удалось открыть %s\n", path); return 0; }

    unsigned char hdr[54];
    if (fread(hdr, 1, 54, f) != 54 || hdr[0] != 'B' || hdr[1] != 'M') {
        fprintf(stderr, "Это не BMP файл\n"); fclose(f); return 0;
    }
    unsigned offset = *(unsigned *)(hdr + 10);
    int w = *(int *)(hdr + 18), h = *(int *)(hdr + 22);
    int bpp = *(unsigned short *)(hdr + 28);
    unsigned comp = *(unsigned *)(hdr + 30);
    int topDown = h < 0;
    if (topDown) h = -h;
    if ((bpp != 24 && bpp != 32) || (comp != 0 && comp != 3) || w <= 0 || h <= 0) {
        fprintf(stderr, "Поддерживается только несжатый BMP 24/32 бит\n"); fclose(f); return 0;
    }

    int bytes = bpp / 8;
    size_t stride = ((size_t)w * bytes + 3) & ~(size_t)3;
    unsigned char *row = malloc(stride);
    img->w = w; img->h = h;
    img->gray = malloc((size_t)w * h);
    img->out = calloc((size_t)w * h, 1);

    fseek(f, (long)offset, SEEK_SET);
    for (int r = 0; r < h; r++) {
        if (fread(row, 1, stride, f) != stride) { fprintf(stderr, "Файл обрезан\n"); fclose(f); return 0; }
        int y = topDown ? r : h - 1 - r;  // в BMP строки обычно идут снизу вверх
        for (int x = 0; x < w; x++) {
            const unsigned char *p = row + x * bytes; // порядок B, G, R
            img->gray[(size_t)y * w + x] = (unsigned char)(0.299 * p[2] + 0.587 * p[1] + 0.114 * p[0]);
        }
    }
    free(row);
    fclose(f);
    return 1;
}

static int save_bmp(const char *path, const Image *img)
{
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "Не удалось создать %s\n", path); return 0; }
    int w = img->w, h = img->h;
    size_t stride = ((size_t)w * 3 + 3) & ~(size_t)3;
    unsigned size = (unsigned)(54 + stride * h);
    unsigned char hdr[54] = { 'B', 'M' };
    *(unsigned *)(hdr + 2) = size;
    *(unsigned *)(hdr + 10) = 54;
    *(unsigned *)(hdr + 14) = 40;
    *(int *)(hdr + 18) = w;
    *(int *)(hdr + 22) = h;
    *(unsigned short *)(hdr + 26) = 1;
    *(unsigned short *)(hdr + 28) = 24;
    fwrite(hdr, 1, 54, f);
    unsigned char *row = calloc(stride, 1);
    for (int r = h - 1; r >= 0; r--) {
        for (int x = 0; x < w; x++) {
            unsigned char v = img->out[(size_t)r * w + x];
            row[x * 3] = row[x * 3 + 1] = row[x * 3 + 2] = v;
        }
        fwrite(row, 1, stride, f);
    }
    free(row);
    fclose(f);
    return 1;
}

/*
Фильтр Собела
Поток считает градиент для своего диапазона строк. Чтение исходного
изображения общее, запись идёт в непересекающиеся строки, поэтому
синхронизация не нужна. Граничные пиксели остаются нулевыми.
*/
static DWORD WINAPI sobel_worker(LPVOID arg)
{
    const Job *job = (const Job *)arg;
    const Image *img = job->img;
    int w = img->w, h = img->h;
    const unsigned char *g = img->gray;

    for (int y = job->row0; y < job->row1; y++) {
        if (y == 0 || y == h - 1) continue;
        for (int x = 1; x < w - 1; x++) {
            const unsigned char *p = g + (size_t)y * w + x;
            int a = p[-w - 1], b = p[-w], c = p[-w + 1];
            int d = p[-1],                e = p[1];
            int f = p[w - 1],  gg = p[w], hh = p[w + 1];
            int gx = (c + 2 * e + hh) - (a + 2 * d + f);
            int gy = (f + 2 * gg + hh) - (a + 2 * b + c);
            int m = (int)(sqrt((double)(gx * gx + gy * gy)) + 0.5);
            img->out[(size_t)y * w + x] = (unsigned char)(m > 255 ? 255 : m);
        }
    }
    return 0;
}

// один прогон на n потоках; возвращает время в миллисекундах.
// в измеряемое время входят создание потоков, вычисления и ожидание завершения.
static double run_sobel(Image *img, int n)
{
    if (n > img->h) n = img->h;
    HANDLE *th = malloc(sizeof(HANDLE) * n);
    Job *jobs = malloc(sizeof(Job) * n);

    int base = img->h / n, extra = img->h % n, row = 0;
    for (int i = 0; i < n; i++) {   // строки делятся поровну, остаток раздаётся по одной
        int cnt = base + (i < extra);
        jobs[i].img = img;
        jobs[i].row0 = row;
        jobs[i].row1 = row + cnt;
        row += cnt;
    }

    LARGE_INTEGER freq, t0, t1;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);
    for (int i = 0; i < n; i++)
        th[i] = CreateThread(NULL, 0, sobel_worker, &jobs[i], 0, NULL);
    for (int i = 0; i < n; i++) WaitForSingleObject(th[i], INFINITE);
    QueryPerformanceCounter(&t1);

    for (int i = 0; i < n; i++) CloseHandle(th[i]);
    free(th);
    free(jobs);
    return (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart;
}

static double average_time(Image *img, int n, int reps)
{
    run_sobel(img, n); // прогрев: подгрузка страниц, кэш
    double sum = 0;
    for (int i = 0; i < reps; i++) sum += run_sobel(img, n);
    return sum / reps;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        printf("Использование: %s вход.bmp выход.bmp [потоки|0] [повторы]\n", argv[0]);
        return 1;
    }
    int threads = argc > 3 ? atoi(argv[3]) : 0;
    int reps = argc > 4 ? atoi(argv[4]) : 5;
    if (reps < 1) reps = 1;

    Image img;
    if (!load_bmp(argv[1], &img)) return 1;
    SYSTEM_INFO si; GetSystemInfo(&si);
    printf("Изображение %dx%d, логических процессоров: %lu, повторов: %d\n",
           img.w, img.h, si.dwNumberOfProcessors, reps);

    if (threads > 0) {
        double ms = average_time(&img, threads, reps);
        printf("Потоков: %d, время: %.3f мс\n", threads, ms);
    } else {
        static const int counts[] = { 1, 2, 4, 8, 16, 32 };
        double t1 = 0;
        printf("\nПотоки | Время, мс | Ускорение\n");
        printf("-------+-----------+----------\n");
        for (int i = 0; i < 6; i++) {
            double ms = average_time(&img, counts[i], reps);
            if (i == 0) t1 = ms;
            printf("%6d | %9.3f | %8.2fx\n", counts[i], ms, t1 / ms);
        }
    }

    if (!save_bmp(argv[2], &img)) return 1;
    printf("Результат записан в %s\n", argv[2]);
    free(img.gray);
    free(img.out);
    return 0;
}

/* Результат
Потоки	Время, мс	Ускорение
1	    91.5        1.00×
2	    49.4	    1.85×
4	    29.4	    3.11×
8	    23.0	    3.98×
16	    18.4	    4.96×
32	    18.3	    5.01×
*/