/* Mede separadamente o custo de neogeo_run_slice() (CPU 68k+Z80) e de
 * neogeo_render_frame() (video), pra achar onde o tempo real esta indo.
 * Uso: debug_perf <dir_cartucho> <dir_bios> <n_frames>
 */
#include "neocore/neogeo.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static double now_sec(void) {
    return (double)clock() / (double)CLOCKS_PER_SEC;
}

int main(int argc, char **argv) {
    if (argc < 4) { printf("uso: %s <dir_cartucho> <dir_bios> <n_frames>\n", argv[0]); return 1; }
    long n = atol(argv[3]);

    neogeo_romset_t rom;
    char err[256];
    if (!neogeo_romset_load_dir_ex(&rom, argv[1], argv[2], err, sizeof(err))) {
        printf("erro: %s\n", err); return 1;
    }
    neogeo_t ng;
    neogeo_init(&ng, &rom);
    neogeo_reset(&ng);

    static u8 fb[NEOGEO_SCREEN_W * NEOGEO_SCREEN_H * 3u];

    double t_cpu = 0.0, t_render = 0.0;
    double t0 = now_sec();
    for (long i = 0; i < n; i++) {
        double a = now_sec();
        neogeo_run_until_vblank(&ng); /* Achado #54 */
        double b = now_sec();
        neogeo_render_frame(&ng, fb);
        double c = now_sec();
        t_cpu += (b - a);
        t_render += (c - b);
    }
    double total = now_sec() - t0;
    printf("n=%ld frames  total=%.3fs (%.1f fps)  cpu=%.3fs (%.1f%%)  render=%.3fs (%.1f%%)\n",
           n, total, (double)n / total, t_cpu, 100.0*t_cpu/total, t_render, 100.0*t_render/total);
    neogeo_romset_free(&rom);
    return 0;
}
