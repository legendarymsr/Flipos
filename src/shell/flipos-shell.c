/*
 * flipos-shell — the phone shell: a status bar and a home screen.
 *
 * A plain Wayland client using wlr-layer-shell:
 *   - status bar: TOP layer, anchored to the top edge, reserves BAR_H pixels
 *     (exclusive zone) so apps are laid out below it. Shows the time and the
 *     network state.
 *   - home screen: BACKGROUND layer filling the remaining area. A grid of the
 *     installed apps, read from their flipos manifests; tap an icon to launch.
 * Apps open on top of the home screen; the compositor's gesture bar (or Alt+H)
 * hides them again. Text is drawn with fcft + pixman into wl_shm buffers — the
 * same libraries foot uses, so the shell adds no new dependencies.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <fcft/fcft.h>
#include <pixman.h>
#include <wayland-client.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#define BAR_H 36
#define MAX_APPS 64
#define COLS 4
#define ICON 76
#define CELL_H 132

/* Tokyo Night palette, matching the rest of the system. */
#define C_BG       0x1a1b26
#define C_BAR      0x11121b
#define C_FG       0xc0caf5
#define C_DIM      0x565f89
#define C_ACCENT   0x7aa2f7
#define C_OK       0x9ece6a

static const uint32_t tile_colors[] = {
	0x7aa2f7, 0xbb9af7, 0x7dcfff, 0x9ece6a, 0xe0af68, 0xf7768e, 0x2ac3de, 0xff9e64,
};

struct app {
	char name[64];
	char entry[512];
};

enum kind { KIND_BAR, KIND_HOME };

struct surf {
	enum kind kind;
	struct wl_surface *surface;
	struct zwlr_layer_surface_v1 *layer;
	int w, h;
	bool configured;
};

struct buf {
	struct wl_buffer *buffer;
	void *data;
	size_t size;
};

static struct wl_display *display;
static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct wl_seat *seat;
static struct wl_pointer *pointer;
static struct zwlr_layer_shell_v1 *layer_shell;

static struct surf bar = { .kind = KIND_BAR };
static struct surf home = { .kind = KIND_HOME };
static struct fcft_font *font, *font_small, *font_icon;

static struct app apps[MAX_APPS];
static int n_apps;

static struct wl_surface *pointer_surface;
static double pointer_x, pointer_y;
static bool running = true;

/* ---------- drawing ---------- */

static pixman_color_t rgb(uint32_t c) {
	return (pixman_color_t){
		.red = ((c >> 16) & 0xff) * 0x101,
		.green = ((c >> 8) & 0xff) * 0x101,
		.blue = (c & 0xff) * 0x101,
		.alpha = 0xffff,
	};
}

static void fill(pixman_image_t *img, uint32_t c, int x, int y, int w, int h) {
	pixman_color_t col = rgb(c);
	pixman_box32_t box = { x, y, x + w, y + h };
	pixman_image_fill_boxes(PIXMAN_OP_SRC, img, &col, 1, &box);
}

/* Rounded square, written straight into the ARGB buffer. */
static void fill_rounded(uint32_t *px, int stride_px, int bw, int bh,
		uint32_t c, int x, int y, int w, int h, int r) {
	for (int j = 0; j < h; j++) {
		int py = y + j;
		if (py < 0 || py >= bh) {
			continue;
		}
		for (int i = 0; i < w; i++) {
			int pxx = x + i;
			if (pxx < 0 || pxx >= bw) {
				continue;
			}
			int dx = i < r ? r - i : (i >= w - r ? i - (w - r - 1) : 0);
			int dy = j < r ? r - j : (j >= h - r ? j - (h - r - 1) : 0);
			if (dx * dx + dy * dy > r * r) {
				continue;
			}
			px[py * stride_px + pxx] = 0xff000000 | c;
		}
	}
}

/* Minimal UTF-8 decoder (enough for manifest names). */
static uint32_t next_cp(const char **s) {
	const unsigned char *p = (const unsigned char *)*s;
	uint32_t cp;
	int n;
	if (p[0] < 0x80) { cp = p[0]; n = 1; }
	else if ((p[0] & 0xe0) == 0xc0) { cp = p[0] & 0x1f; n = 2; }
	else if ((p[0] & 0xf0) == 0xe0) { cp = p[0] & 0x0f; n = 3; }
	else { cp = p[0] & 0x07; n = 4; }
	for (int i = 1; i < n; i++) {
		if ((p[i] & 0xc0) != 0x80) { *s += 1; return 0xfffd; }
		cp = (cp << 6) | (p[i] & 0x3f);
	}
	*s += n;
	return cp;
}

static int text_width(struct fcft_font *f, const char *s) {
	int w = 0;
	while (*s) {
		const struct fcft_glyph *g = fcft_rasterize_char_utf32(f, next_cp(&s), FCFT_SUBPIXEL_NONE);
		if (g) {
			w += g->advance.x;
		}
	}
	return w;
}

static void draw_text(pixman_image_t *img, struct fcft_font *f, const char *s,
		int x, int baseline, uint32_t c) {
	pixman_color_t col = rgb(c);
	pixman_image_t *src = pixman_image_create_solid_fill(&col);
	while (*s) {
		const struct fcft_glyph *g = fcft_rasterize_char_utf32(f, next_cp(&s), FCFT_SUBPIXEL_NONE);
		if (!g) {
			continue;
		}
		if (g->is_color_glyph) {
			pixman_image_composite32(PIXMAN_OP_OVER, g->pix, NULL, img, 0, 0, 0, 0,
				x + g->x, baseline - g->y, g->width, g->height);
		} else {
			pixman_image_composite32(PIXMAN_OP_OVER, src, g->pix, img, 0, 0, 0, 0,
				x + g->x, baseline - g->y, g->width, g->height);
		}
		x += g->advance.x;
	}
	pixman_image_unref(src);
}

/* Draw text centred in [x, x+w), shortened with "..." if it doesn't fit. */
static void draw_text_centered(pixman_image_t *img, struct fcft_font *f, const char *s,
		int x, int w, int baseline, uint32_t c) {
	char tmp[96];
	snprintf(tmp, sizeof(tmp), "%s", s);
	while (text_width(f, tmp) > w - 6 && strlen(tmp) > 3) {
		size_t n = strlen(tmp);
		tmp[n - 4] = '\0';
		strcat(tmp, "...");
	}
	draw_text(img, f, tmp, x + (w - text_width(f, tmp)) / 2, baseline, c);
}

/* ---------- shm buffers ---------- */

static void buffer_release(void *data, struct wl_buffer *wl_buffer) {
	struct buf *b = data;
	wl_buffer_destroy(b->buffer);
	munmap(b->data, b->size);
	free(b);
}

static const struct wl_buffer_listener buffer_listener = { .release = buffer_release };

static struct buf *new_buffer(int w, int h) {
	int stride = w * 4;
	size_t size = (size_t)stride * h;
	int fd = memfd_create("flipos-shell", MFD_CLOEXEC);
	if (fd < 0 || ftruncate(fd, size) < 0) {
		if (fd >= 0) close(fd);
		return NULL;
	}
	void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (data == MAP_FAILED) {
		close(fd);
		return NULL;
	}
	struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
	struct buf *b = calloc(1, sizeof(*b));
	b->buffer = wl_shm_pool_create_buffer(pool, 0, w, h, stride, WL_SHM_FORMAT_ARGB8888);
	b->data = data;
	b->size = size;
	wl_shm_pool_destroy(pool);
	close(fd);
	wl_buffer_add_listener(b->buffer, &buffer_listener, b);
	return b;
}

/* ---------- apps ---------- */

/* Read `key = "value"` from a flipos manifest (a tiny TOML subset). */
static bool manifest_get(const char *path, const char *key, char *out, size_t outlen) {
	FILE *f = fopen(path, "r");
	if (!f) {
		return false;
	}
	char line[1024];
	bool found = false;
	size_t klen = strlen(key);
	while (fgets(line, sizeof(line), f)) {
		char *p = line;
		while (isspace((unsigned char)*p)) p++;
		if (*p == '[') {
			break;      /* top-level keys only */
		}
		if (strncmp(p, key, klen) != 0) continue;
		p += klen;
		while (isspace((unsigned char)*p)) p++;
		if (*p != '=') continue;
		p++;
		while (isspace((unsigned char)*p)) p++;
		if (*p != '"') continue;
		p++;
		char *end = strchr(p, '"');
		if (!end) continue;
		*end = '\0';
		snprintf(out, outlen, "%s", p);
		found = true;
		break;
	}
	fclose(f);
	return found;
}

static void scan_dir(const char *dir, const char *root) {
	DIR *d = opendir(dir);
	if (!d) {
		return;
	}
	struct dirent *e;
	while ((e = readdir(d)) && n_apps < MAX_APPS) {
		size_t len = strlen(e->d_name);
		if (len < 6 || strcmp(e->d_name + len - 5, ".toml") != 0) continue;
		char path[512], entry[448];
		snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
		struct app *a = &apps[n_apps];
		if (!manifest_get(path, "name", a->name, sizeof(a->name)) ||
				!manifest_get(path, "entry", entry, sizeof(entry))) {
			continue;
		}
		/* Apps installed by pacman live under /data/appfs; their absolute
		 * entry paths are relative to that root. */
		if (entry[0] == '/' && root[0] != '\0') {
			snprintf(a->entry, sizeof(a->entry), "%s%s", root, entry);
		} else {
			snprintf(a->entry, sizeof(a->entry), "%s", entry);
		}
		n_apps++;
	}
	closedir(d);
}

static int cmp_app(const void *a, const void *b) {
	return strcasecmp(((const struct app *)a)->name, ((const struct app *)b)->name);
}

/* Returns true if the app list changed. */
static bool scan_apps(void) {
	struct app old[MAX_APPS];
	int old_n = n_apps;
	memcpy(old, apps, sizeof(apps));
	n_apps = 0;
	scan_dir("/usr/share/flipos/apps", "");
	scan_dir("/data/appfs/usr/share/flipos/apps", "/data/appfs");
	qsort(apps, n_apps, sizeof(apps[0]), cmp_app);
	return n_apps != old_n || memcmp(old, apps, sizeof(apps)) != 0;
}

static void launch(const struct app *a) {
	pid_t pid = fork();
	if (pid == 0) {
		setsid();
		execl("/bin/sh", "sh", "-c", a->entry, (char *)NULL);
		_exit(127);
	}
}

/* ---------- rendering the two surfaces ---------- */

static bool network_up(void) {
	FILE *f = fopen("/sys/class/net/eth0/operstate", "r");
	char st[16] = "";
	if (f) {
		if (!fgets(st, sizeof(st), f)) st[0] = '\0';
		fclose(f);
	}
	return strncmp(st, "up", 2) == 0;
}

static void draw_bar(pixman_image_t *img, int w, int h) {
	fill(img, C_BAR, 0, 0, w, h);
	int base = (h + font->ascent - font->descent) / 2;
	draw_text(img, font, "flipos", 14, base, C_ACCENT);

	char clock[16];
	time_t now = time(NULL);
	struct tm tm;
	localtime_r(&now, &tm);
	strftime(clock, sizeof(clock), "%H:%M", &tm);
	int cw = text_width(font, clock);
	draw_text(img, font, clock, w - 14 - cw, base, C_FG);

	bool up = network_up();
	const char *net = up ? "net" : "offline";
	int nw = text_width(font_small, net);
	int sbase = (h + font_small->ascent - font_small->descent) / 2;
	draw_text(img, font_small, net, w - 14 - cw - 16 - nw, sbase, up ? C_OK : C_DIM);
	/* No battery shown: QEMU has none, and the simulated one isn't built yet. */
}

static void draw_home(pixman_image_t *img, uint32_t *px, int w, int h) {
	fill(img, C_BG, 0, 0, w, h);
	if (n_apps == 0) {
		draw_text_centered(img, font, "No apps installed", 0, w, h / 2, C_DIM);
		return;
	}
	int cell_w = w / COLS;
	int top = 28;
	for (int i = 0; i < n_apps; i++) {
		int col = i % COLS, row = i / COLS;
		int cx = col * cell_w;
		int cy = top + row * CELL_H;
		int ix = cx + (cell_w - ICON) / 2;
		uint32_t color = tile_colors[(unsigned char)apps[i].name[0] % (sizeof(tile_colors) / sizeof(tile_colors[0]))];
		fill_rounded(px, w, w, h, color, ix, cy, ICON, ICON, 18);

		char letter[8];
		const char *nm = apps[i].name;
		uint32_t cp = next_cp(&nm);
		if (cp < 0x80) {
			letter[0] = (char)toupper((int)cp);
			letter[1] = '\0';
		} else {
			snprintf(letter, sizeof(letter), "%s", "?");
		}
		int lbase = cy + (ICON + font_icon->ascent - font_icon->descent) / 2;
		draw_text_centered(img, font_icon, letter, ix, ICON, lbase, C_BG);
		draw_text_centered(img, font_small, apps[i].name, cx, cell_w,
			cy + ICON + 10 + font_small->ascent, C_FG);
	}
}

static void render(struct surf *s) {
	if (!s->configured || s->w <= 0 || s->h <= 0) {
		return;
	}
	struct buf *b = new_buffer(s->w, s->h);
	if (!b) {
		return;
	}
	pixman_image_t *img = pixman_image_create_bits(PIXMAN_a8r8g8b8, s->w, s->h,
		b->data, s->w * 4);
	if (s->kind == KIND_BAR) {
		draw_bar(img, s->w, s->h);
	} else {
		draw_home(img, b->data, s->w, s->h);
	}
	pixman_image_unref(img);
	wl_surface_attach(s->surface, b->buffer, 0, 0);
	wl_surface_damage_buffer(s->surface, 0, 0, s->w, s->h);
	wl_surface_commit(s->surface);
}

/* ---------- layer surfaces ---------- */

static void layer_configure(void *data, struct zwlr_layer_surface_v1 *l,
		uint32_t serial, uint32_t w, uint32_t h) {
	struct surf *s = data;
	zwlr_layer_surface_v1_ack_configure(l, serial);
	s->w = (int)w;
	s->h = (int)h;
	s->configured = true;
	render(s);
}

static void layer_closed(void *data, struct zwlr_layer_surface_v1 *l) {
	running = false;
}

static const struct zwlr_layer_surface_v1_listener layer_listener = {
	.configure = layer_configure,
	.closed = layer_closed,
};

static void make_surface(struct surf *s) {
	s->surface = wl_compositor_create_surface(compositor);
	if (s->kind == KIND_BAR) {
		s->layer = zwlr_layer_shell_v1_get_layer_surface(layer_shell, s->surface, NULL,
			ZWLR_LAYER_SHELL_V1_LAYER_TOP, "flipos-bar");
		zwlr_layer_surface_v1_set_anchor(s->layer, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
			ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
		zwlr_layer_surface_v1_set_size(s->layer, 0, BAR_H);
		zwlr_layer_surface_v1_set_exclusive_zone(s->layer, BAR_H);
	} else {
		s->layer = zwlr_layer_shell_v1_get_layer_surface(layer_shell, s->surface, NULL,
			ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND, "flipos-home");
		zwlr_layer_surface_v1_set_anchor(s->layer, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
			ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
			ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
		zwlr_layer_surface_v1_set_size(s->layer, 0, 0);
		zwlr_layer_surface_v1_set_exclusive_zone(s->layer, 0);
	}
	zwlr_layer_surface_v1_add_listener(s->layer, &layer_listener, s);
	wl_surface_commit(s->surface);   /* initial commit: ask for a configure */
}

/* ---------- pointer (the virtio tablet acts as touch) ---------- */

static void p_enter(void *d, struct wl_pointer *p, uint32_t serial, struct wl_surface *s,
		wl_fixed_t x, wl_fixed_t y) {
	pointer_surface = s;
	pointer_x = wl_fixed_to_double(x);
	pointer_y = wl_fixed_to_double(y);
	if (s == home.surface && scan_apps()) {
		render(&home);   /* pick up apps installed while we were hidden */
	}
}

static void p_leave(void *d, struct wl_pointer *p, uint32_t serial, struct wl_surface *s) {
	pointer_surface = NULL;
}

static void p_motion(void *d, struct wl_pointer *p, uint32_t t, wl_fixed_t x, wl_fixed_t y) {
	pointer_x = wl_fixed_to_double(x);
	pointer_y = wl_fixed_to_double(y);
}

static void p_button(void *d, struct wl_pointer *p, uint32_t serial, uint32_t t,
		uint32_t button, uint32_t state) {
	if (state != WL_POINTER_BUTTON_STATE_RELEASED || pointer_surface != home.surface) {
		return;
	}
	int cell_w = home.w / COLS;
	int col = (int)pointer_x / cell_w;
	int row = ((int)pointer_y - 28) / CELL_H;
	if (pointer_y < 28 || col < 0 || col >= COLS || row < 0) {
		return;
	}
	int i = row * COLS + col;
	int ix = col * cell_w + (cell_w - ICON) / 2;
	int iy = 28 + row * CELL_H;
	/* Hit the icon or its label, not empty space in the cell. */
	if (i < n_apps && pointer_x >= ix - 8 && pointer_x <= ix + ICON + 8 &&
			pointer_y >= iy && pointer_y <= iy + ICON + 30) {
		launch(&apps[i]);
	}
}

static void p_axis(void *d, struct wl_pointer *p, uint32_t t, uint32_t a, wl_fixed_t v) {}
static void p_frame(void *d, struct wl_pointer *p) {}
static void p_axis_source(void *d, struct wl_pointer *p, uint32_t s) {}
static void p_axis_stop(void *d, struct wl_pointer *p, uint32_t t, uint32_t a) {}
static void p_axis_discrete(void *d, struct wl_pointer *p, uint32_t a, int32_t v) {}
static void p_axis_value120(void *d, struct wl_pointer *p, uint32_t a, int32_t v) {}
static void p_axis_rel(void *d, struct wl_pointer *p, uint32_t a, uint32_t dir) {}

static const struct wl_pointer_listener pointer_listener = {
	.enter = p_enter, .leave = p_leave, .motion = p_motion, .button = p_button,
	.axis = p_axis, .frame = p_frame, .axis_source = p_axis_source,
	.axis_stop = p_axis_stop, .axis_discrete = p_axis_discrete,
	.axis_value120 = p_axis_value120, .axis_relative_direction = p_axis_rel,
};

static void seat_caps(void *d, struct wl_seat *s, uint32_t caps) {
	if ((caps & WL_SEAT_CAPABILITY_POINTER) && !pointer) {
		pointer = wl_seat_get_pointer(s);
		wl_pointer_add_listener(pointer, &pointer_listener, NULL);
	}
}

static void seat_name(void *d, struct wl_seat *s, const char *name) {}

static const struct wl_seat_listener seat_listener = {
	.capabilities = seat_caps, .name = seat_name,
};

/* ---------- registry ---------- */

static void reg_global(void *d, struct wl_registry *r, uint32_t name,
		const char *iface, uint32_t version) {
	if (strcmp(iface, wl_compositor_interface.name) == 0) {
		compositor = wl_registry_bind(r, name, &wl_compositor_interface, 4);
	} else if (strcmp(iface, wl_shm_interface.name) == 0) {
		shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
	} else if (strcmp(iface, wl_seat_interface.name) == 0) {
		seat = wl_registry_bind(r, name, &wl_seat_interface, version < 5 ? version : 5);
		wl_seat_add_listener(seat, &seat_listener, NULL);
	} else if (strcmp(iface, zwlr_layer_shell_v1_interface.name) == 0) {
		layer_shell = wl_registry_bind(r, name, &zwlr_layer_shell_v1_interface, 1);
	}
}

static void reg_remove(void *d, struct wl_registry *r, uint32_t name) {}

static const struct wl_registry_listener registry_listener = {
	.global = reg_global, .global_remove = reg_remove,
};

/* ---------- main ---------- */

static struct fcft_font *load_font(const char *spec) {
	const char *names[] = { spec };
	struct fcft_font *f = fcft_from_name(1, names, NULL);
	if (!f) {
		fprintf(stderr, "flipos-shell: cannot load font '%s'\n", spec);
		exit(1);
	}
	return f;
}

int main(void) {
	signal(SIGCHLD, SIG_IGN);   /* auto-reap launched apps */
	fcft_init(FCFT_LOG_COLORIZE_NEVER, false, FCFT_LOG_CLASS_ERROR);
	font = load_font("DejaVu Sans:pixelsize=16:weight=bold");
	font_small = load_font("DejaVu Sans:pixelsize=13");
	font_icon = load_font("DejaVu Sans:pixelsize=34:weight=bold");

	display = wl_display_connect(NULL);
	if (!display) {
		fprintf(stderr, "flipos-shell: cannot connect to Wayland\n");
		return 1;
	}
	struct wl_registry *reg = wl_display_get_registry(display);
	wl_registry_add_listener(reg, &registry_listener, NULL);
	wl_display_roundtrip(display);
	if (!compositor || !shm || !layer_shell) {
		fprintf(stderr, "flipos-shell: compositor lacks wl_shm or wlr-layer-shell\n");
		return 1;
	}

	scan_apps();
	make_surface(&home);
	make_surface(&bar);

	while (running) {
		/* Wake at the next minute so the clock stays right. */
		time_t now = time(NULL);
		struct tm tm;
		localtime_r(&now, &tm);
		int timeout_ms = (60 - tm.tm_sec) * 1000;

		while (wl_display_prepare_read(display) != 0) {
			wl_display_dispatch_pending(display);
		}
		wl_display_flush(display);
		struct pollfd pfd = { .fd = wl_display_get_fd(display), .events = POLLIN };
		int ret = poll(&pfd, 1, timeout_ms);
		if (ret > 0) {
			if (wl_display_read_events(display) < 0) {
				break;
			}
		} else {
			wl_display_cancel_read(display);
		}
		if (wl_display_dispatch_pending(display) < 0) {
			break;
		}
		if (ret == 0) {
			render(&bar);
			if (scan_apps()) {
				render(&home);
			}
		}
	}

	fcft_destroy(font_icon);
	fcft_destroy(font_small);
	fcft_destroy(font);
	fcft_fini();
	wl_display_disconnect(display);
	return 0;
}
