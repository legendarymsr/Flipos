/*
 * flipos-shell — the phone shell: a status bar and a home screen.
 *
 * A plain Wayland client using wlr-layer-shell:
 *   - status bar: TOP layer, anchored to the top edge, reserves BAR_H pixels
 *     (exclusive zone) so apps are laid out below it. Clock on the left; network
 *     and (only if the system has one) battery icons on the right. Tap it or
 *     swipe down on it to pull down the quick-settings panel: big clock, Lock /
 *     Terminal / Restart / Power off tiles and a live system card (IP, memory,
 *     storage, uptime, CPU). Tap outside a tile or swipe up to close it.
 *   - lock screen: ext-session-lock (the compositor enforces it: nothing else
 *     is shown or gets input while locked, even if this process dies). PIN is
 *     stored as an Argon2id hash; failed attempts back off exponentially.
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

#include <argon2.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <fcft/fcft.h>
#include <pixman.h>
#include <wayland-client.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "ext-session-lock-v1-client-protocol.h"

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
#define C_CARD     0x24283b
#define C_RED      0xf7768e

#define GESTURE_H  28      /* compositor's gesture strip at the bottom (flipos-wm) */
#define SWIPE      40      /* px of vertical travel that counts as a swipe */

static const uint32_t tile_colors[] = {
	0x7aa2f7, 0xbb9af7, 0x7dcfff, 0x9ece6a, 0xe0af68, 0xf7768e, 0x2ac3de, 0xff9e64,
};

struct app {
	char name[64];
	char entry[512];
};

enum kind { KIND_BAR, KIND_HOME, KIND_LOCK };

struct surf {
	enum kind kind;
	struct wl_surface *surface;
	struct zwlr_layer_surface_v1 *layer;
	struct ext_session_lock_surface_v1 *lock_surface;
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
static struct ext_session_lock_manager_v1 *lock_mgr;
static struct wl_output *output;
static struct wl_keyboard *keyboard;

static struct surf bar = { .kind = KIND_BAR };
/* quick-settings panel: the bar's own surface, grown to (almost) full height */
static bool shade_open;
static int shade_armed = -1;        /* tile awaiting a confirming second tap */
static int output_h;                /* from wl_output.mode */
static double press_y;
static struct wl_surface *press_surface;
static struct surf home = { .kind = KIND_HOME };
static struct surf lock = { .kind = KIND_LOCK };

/* ---- lock state ---- */
#define PIN_FILE   "/data/system/lock.pin"     /* Argon2id encoded hash */
#define STATE_FILE "/data/system/lock.state"   /* "<fails> <locked_until>" */
#define PIN_MIN 4
#define PIN_MAX 8
enum lock_mode { LM_ENTER, LM_SET, LM_CONFIRM };
static struct ext_session_lock_v1 *session_lock;
static bool session_locked;                    /* compositor confirmed */
static enum lock_mode lmode;
static char pin[PIN_MAX + 1], first_pin[PIN_MAX + 1];
static int pin_len;
static char lock_msg[64];
static volatile sig_atomic_t lock_requested;
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
		signal(SIGCHLD, SIG_DFL);   /* don't pass our SIG_IGN on to the app */
		setsid();
		execl("/bin/sh", "sh", "-c", a->entry, (char *)NULL);
		_exit(127);
	}
}

/* ---------- rendering the two surfaces ---------- */

static bool read_line(const char *path, char *out, size_t n) {
	FILE *f = fopen(path, "r");
	if (!f) return false;
	bool ok = fgets(out, (int)n, f) != NULL;
	fclose(f);
	return ok;
}

/* First non-loopback IPv4 address that is up, e.g. "10.0.2.15". */
static bool ipv4_addr(char *out, size_t n) {
	struct ifaddrs *ifa, *i;
	bool found = false;
	if (getifaddrs(&ifa) != 0) return false;
	for (i = ifa; i; i = i->ifa_next) {
		if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET) continue;
		if (strcmp(i->ifa_name, "lo") == 0) continue;
		inet_ntop(AF_INET, &((struct sockaddr_in *)i->ifa_addr)->sin_addr, out, (socklen_t)n);
		found = true;
		break;
	}
	freeifaddrs(ifa);
	return found;
}

static bool network_up(void) {
	char ip[INET_ADDRSTRLEN];
	return ipv4_addr(ip, sizeof(ip));
}

/* Battery percentage, or -1 when there is none. Source, in order:
 *   1. /run/flipos/battery - the PHONE's real battery, sent by the Android host
 *      over virtio-serial (flipos-hostd); ignored if older than 3 minutes, so a
 *      stopped sender never leaves a stale number on screen;
 *   2. a battery the guest kernel itself reports (none under QEMU).
 * With neither, no icon is drawn - never a fake number. */
static int battery_pct(bool *charging) {
	int pct = -1;
	*charging = false;
	struct stat st;
	char hb[64];
	if (stat("/run/flipos/battery", &st) == 0 && time(NULL) - st.st_mtime < 180 &&
			read_line("/run/flipos/battery", hb, sizeof(hb))) {
		char state[32] = "";
		if (sscanf(hb, "%d %31[^\n]", &pct, state) >= 1 && pct >= 0 && pct <= 100) {
			*charging = strncmp(state, "Charging", 8) == 0 || strncmp(state, "Full", 4) == 0;
			return pct;
		}
		pct = -1;
	}
	DIR *d = opendir("/sys/class/power_supply");
	if (!d) return -1;
	struct dirent *e;
	while ((e = readdir(d)) && pct < 0) {
		char path[300], buf[32];
		snprintf(path, sizeof(path), "/sys/class/power_supply/%s/type", e->d_name);
		if (!read_line(path, buf, sizeof(buf)) || strncmp(buf, "Battery", 7) != 0) continue;
		snprintf(path, sizeof(path), "/sys/class/power_supply/%s/capacity", e->d_name);
		if (read_line(path, buf, sizeof(buf))) pct = atoi(buf);
		snprintf(path, sizeof(path), "/sys/class/power_supply/%s/status", e->d_name);
		if (read_line(path, buf, sizeof(buf))) *charging = strncmp(buf, "Charging", 8) == 0;
	}
	closedir(d);
	return pct;
}

/* Rounded rect into a pixman image (uses the raw buffer). */
static void rrect(pixman_image_t *img, uint32_t c, int x, int y, int w, int h, int r) {
	fill_rounded(pixman_image_get_data(img), pixman_image_get_stride(img) / 4,
		pixman_image_get_width(img), pixman_image_get_height(img), c, x, y, w, h, r);
}

/* Network icon: four rising bars; dimmed with a slash when offline.
 * Returns its width. */
static int draw_net_icon(pixman_image_t *img, int x, int cy, bool up) {
	uint32_t c = up ? C_FG : C_DIM;
	for (int i = 0; i < 4; i++) {
		int bh = 4 + i * 3;
		rrect(img, c, x + i * 5, cy + 7 - bh, 3, bh, 1);
	}
	if (!up) {
		for (int i = 0; i < 16; i++) fill(img, C_RED, x + i, cy - 6 + (i * 13) / 16, 2, 2);
	}
	return 18;
}

/* Battery icon with fill level and percentage. Returns its width. */
static int draw_batt_icon(pixman_image_t *img, int x, int cy, int pct, bool charging) {
	char t[16];
	snprintf(t, sizeof(t), "%d%%", pct);
	int tw = text_width(font_small, t);
	draw_text(img, font_small, t, x, cy + 5, C_FG);
	x += tw + 4;
	uint32_t c = charging ? C_OK : pct <= 15 ? C_RED : C_FG;
	rrect(img, C_FG, x, cy - 6, 22, 12, 3);
	rrect(img, C_BAR, x + 1, cy - 5, 20, 10, 2);
	rrect(img, C_FG, x + 22, cy - 2, 2, 4, 1);
	int fw = (18 * (pct < 0 ? 0 : pct > 100 ? 100 : pct)) / 100;
	if (fw > 0) rrect(img, c, x + 2, cy - 4, fw, 8, 1);
	return tw + 4 + 24;
}

static void clock_str(char *out, size_t n, const char *fmt) {
	time_t now = time(NULL);
	struct tm tm;
	localtime_r(&now, &tm);
	strftime(out, n, fmt, &tm);
}

/* ---------- quick-settings panel ---------- */

enum { T_LOCK, T_TERM, T_REBOOT, T_POWEROFF, N_TILES };
static const char *tile_title[N_TILES] = { "Lock", "Terminal", "Restart", "Power off" };
static const char *tile_sub[N_TILES] = { "Lock the screen now", "Open foot", "Reboot flipos", "Shut down the VM" };

static void tile_rect(int w, int i, int *x, int *y, int *tw, int *th) {
	int m = 16, gap = 12;
	*tw = (w - 2 * m - gap) / 2;
	*th = 78;
	*x = m + (i % 2) * (*tw + gap);
	*y = BAR_H + 112 + (i / 2) * (*th + gap);
}

static void info_row(pixman_image_t *img, int x, int w, int y, const char *k, const char *v) {
	draw_text(img, font_small, k, x, y, C_DIM);
	draw_text(img, font_small, v, x + w - text_width(font_small, v), y, C_FG);
}

static void draw_shade(pixman_image_t *img, int w, int h) {
	fill(img, C_BG, 0, BAR_H, w, h - BAR_H);
	char big[16], date[64];
	clock_str(big, sizeof(big), "%H:%M");
	clock_str(date, sizeof(date), "%A %d %B");
	draw_text(img, font_icon, big, 20, BAR_H + 52, C_FG);
	draw_text(img, font_small, date, 22, BAR_H + 78, C_DIM);

	for (int i = 0; i < N_TILES; i++) {
		int x, y, tw, th;
		tile_rect(w, i, &x, &y, &tw, &th);
		bool armed = shade_armed == i;
		uint32_t bg = armed ? C_RED : i == T_LOCK ? C_ACCENT : C_CARD;
		uint32_t fg = armed || i == T_LOCK ? C_BG : C_FG;
		rrect(img, bg, x, y, tw, th, 20);
		draw_text(img, font, tile_title[i], x + 18, y + 34, fg);
		draw_text(img, font_small, armed ? "Tap again to confirm" : tile_sub[i],
			x + 18, y + 56, armed ? C_BG : (i == T_LOCK ? C_BG : C_DIM));
	}

	/* live system card */
	int cx = 16, cw = w - 32, cy = BAR_H + 112 + 2 * (78 + 12) + 4, rowh = 30;
	rrect(img, C_CARD, cx, cy, cw, 24 + 7 * rowh, 20);
	int x = cx + 18, iw = cw - 36, y = cy + 34;
	char v[96], line[128];

	draw_text(img, font, "System", x, y, C_ACCENT);
	y += rowh;
	if (!ipv4_addr(v, sizeof(v))) snprintf(v, sizeof(v), "offline");
	info_row(img, x, iw, y, "Network", v);
	y += rowh;

	long total = 0, avail = 0;
	FILE *f = fopen("/proc/meminfo", "r");
	if (f) {
		while (fgets(line, sizeof(line), f)) {
			sscanf(line, "MemTotal: %ld kB", &total);
			sscanf(line, "MemAvailable: %ld kB", &avail);
		}
		fclose(f);
	}
	snprintf(v, sizeof(v), "%ld / %ld MB", (total - avail) / 1024, total / 1024);
	info_row(img, x, iw, y, "Memory", v);
	y += rowh;

	struct statvfs sv;
	if (statvfs("/data", &sv) == 0) {
		double tot = (double)sv.f_blocks * sv.f_frsize, fr = (double)sv.f_bavail * sv.f_frsize;
		snprintf(v, sizeof(v), "%.1f / %.1f GB", (tot - fr) / 1e9, tot / 1e9);
	} else {
		snprintf(v, sizeof(v), "not mounted");
	}
	info_row(img, x, iw, y, "Storage (/data)", v);
	y += rowh;

	double up = 0;
	if (read_line("/proc/uptime", line, sizeof(line))) up = atof(line);
	long mins = (long)up / 60;
	snprintf(v, sizeof(v), "%ldh %02ldm", mins / 60, mins % 60);
	info_row(img, x, iw, y, "Uptime", v);
	y += rowh;

	double load = 0;
	if (read_line("/proc/loadavg", line, sizeof(line))) load = atof(line);
	snprintf(v, sizeof(v), "%ld cores, load %.2f", sysconf(_SC_NPROCESSORS_ONLN), load);
	info_row(img, x, iw, y, "CPU", v);
	y += rowh;

	bool chg;
	int bp = battery_pct(&chg);
	if (bp >= 0) snprintf(v, sizeof(v), "%d%%%s", bp, chg ? ", charging" : "");
	else snprintf(v, sizeof(v), "not reported");
	info_row(img, x, iw, y, "Battery", v);

	/* close handle */
	rrect(img, C_DIM, (w - 44) / 2, h - 18, 44, 5, 2);
}

static void draw_bar(pixman_image_t *img, int w, int h) {
	fill(img, C_BAR, 0, 0, w, BAR_H);
	int base = (BAR_H + font->ascent - font->descent) / 2;
	char clock[16];
	clock_str(clock, sizeof(clock), "%H:%M");
	draw_text(img, font, clock, 18, base, C_FG);

	int cy = BAR_H / 2, x = w - 18;
	bool charging;
	int pct = battery_pct(&charging);
	if (pct >= 0) {
		char t[16];
		snprintf(t, sizeof(t), "%d%%", pct);
		x -= text_width(font_small, t) + 4 + 24;
		draw_batt_icon(img, x, cy, pct, charging);
		x -= 12;
	}
	x -= 18;
	draw_net_icon(img, x, cy, network_up());

	if (shade_open) {
		draw_shade(img, w, h);
	}
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

/* ---------- lock screen ---------- */

static const char *keys[12] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "<", "0", "OK" };

static void key_rect(int w, int h, int i, int *x, int *y, int *kw, int *kh) {
	int cell = w / 3;
	*kw = cell - 36;
	*kh = 74;
	*x = (i % 3) * cell + 18;
	*y = (int)(h * 0.40) + (i / 3) * 90;
}

static bool pin_exists(void) {
	return access(PIN_FILE, R_OK) == 0;
}

static void load_state(int *fails, long *until) {
	*fails = 0;
	*until = 0;
	FILE *f = fopen(STATE_FILE, "r");
	if (f) {
		if (fscanf(f, "%d %ld", fails, until) != 2) {
			*fails = 0;
			*until = 0;
		}
		fclose(f);
	}
}

static bool write_private(const char *path, const char *data) {
	char tmp[256];
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	mkdir("/data/system", 0700);
	int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	if (fd < 0) {
		return false;
	}
	size_t n = strlen(data);
	bool ok = write(fd, data, n) == (ssize_t)n && fsync(fd) == 0;
	close(fd);
	return ok && rename(tmp, path) == 0;
}

static void save_state(int fails, long until) {
	char buf[64];
	snprintf(buf, sizeof(buf), "%d %ld\n", fails, until);
	write_private(STATE_FILE, buf);
}

/* Argon2id: 3 passes, 32 MiB, 1 lane, 16-byte random salt, 32-byte hash. */
static bool save_pin(const char *p) {
	uint8_t salt[16];
	if (getrandom(salt, sizeof(salt), 0) != (ssize_t)sizeof(salt)) {
		return false;
	}
	char enc[256];
	if (argon2id_hash_encoded(3, 32 * 1024, 1, p, strlen(p), salt, sizeof(salt),
			32, enc, sizeof(enc)) != ARGON2_OK) {
		return false;
	}
	return write_private(PIN_FILE, enc);
}

static bool verify_pin(const char *p) {
	char enc[256] = "";
	FILE *f = fopen(PIN_FILE, "r");
	if (!f) {
		return false;
	}
	if (!fgets(enc, sizeof(enc), f)) {
		enc[0] = '\0';
	}
	fclose(f);
	enc[strcspn(enc, "\n")] = '\0';
	return enc[0] != '\0' && argon2id_verify(enc, p, strlen(p)) == ARGON2_OK;
}

/* Seconds left in a back-off, or 0. */
static long backoff_left(void) {
	int fails;
	long until;
	load_state(&fails, &until);
	long left = until - (long)time(NULL);
	return left > 0 ? left : 0;
}

static void wipe_pins(void) {
	explicit_bzero(pin, sizeof(pin));
	explicit_bzero(first_pin, sizeof(first_pin));
	pin_len = 0;
}

static void draw_lock(pixman_image_t *img, uint32_t *px, int w, int h) {
	fill(img, C_BG, 0, 0, w, h);

	char clock[16], date[32];
	time_t now = time(NULL);
	struct tm tm;
	localtime_r(&now, &tm);
	strftime(clock, sizeof(clock), "%H:%M", &tm);
	strftime(date, sizeof(date), "%A %d %B", &tm);
	draw_text_centered(img, font_icon, clock, 0, w, (int)(h * 0.12), C_FG);
	draw_text_centered(img, font_small, date, 0, w, (int)(h * 0.12) + 28, C_DIM);

	const char *prompt = lmode == LM_SET ? "Set a PIN (4-8 digits)" :
		lmode == LM_CONFIRM ? "Confirm your PIN" : "Enter PIN";
	draw_text_centered(img, font, prompt, 0, w, (int)(h * 0.24), C_FG);
	if (lock_msg[0]) {
		draw_text_centered(img, font_small, lock_msg, 0, w, (int)(h * 0.24) + 26, 0xf7768e);
	}

	/* entered digits as dots */
	int dot = 14, gap = 26;
	int total = PIN_MAX * gap - (gap - dot);
	int dx = (w - total) / 2, dy = (int)(h * 0.32);
	for (int i = 0; i < PIN_MAX; i++) {
		fill_rounded(px, w, w, h, i < pin_len ? C_ACCENT : 0x2a2e42,
			dx + i * gap, dy, dot, dot, dot / 2);
	}

	for (int i = 0; i < 12; i++) {
		int x, y, kw, kh;
		key_rect(w, h, i, &x, &y, &kw, &kh);
		fill_rounded(px, w, w, h, i == 11 ? C_ACCENT : 0x24283b, x, y, kw, kh, 16);
		struct fcft_font *f = i == 11 ? font : font_icon;
		draw_text_centered(img, f, keys[i], x, kw,
			y + (kh + f->ascent - f->descent) / 2, i == 11 ? C_BG : C_FG);
	}
}

static void lock_cleanup(void) {
	if (lock.lock_surface) {
		ext_session_lock_surface_v1_destroy(lock.lock_surface);
		lock.lock_surface = NULL;
	}
	if (lock.surface) {
		wl_surface_destroy(lock.surface);
		lock.surface = NULL;
	}
	lock.configured = false;
	wipe_pins();
}

static void do_unlock(void) {
	if (!session_lock || !session_locked) {
		return;     /* protocol: only after the compositor said 'locked' */
	}
	lock_cleanup();
	ext_session_lock_v1_unlock_and_destroy(session_lock);
	session_lock = NULL;
	session_locked = false;
	wl_display_flush(display);
}

static void render(struct surf *s);

static void submit_pin(void) {
	pin[pin_len] = '\0';
	if (pin_len < PIN_MIN) {
		snprintf(lock_msg, sizeof(lock_msg), "PIN must be %d-%d digits", PIN_MIN, PIN_MAX);
		return;
	}
	if (lmode == LM_SET) {
		memcpy(first_pin, pin, sizeof(pin));
		explicit_bzero(pin, sizeof(pin));
		pin_len = 0;
		lmode = LM_CONFIRM;
		lock_msg[0] = '\0';
		return;
	}
	if (lmode == LM_CONFIRM) {
		if (strcmp(pin, first_pin) == 0 && save_pin(pin)) {
			save_state(0, 0);
			do_unlock();
		} else {
			lmode = LM_SET;
			snprintf(lock_msg, sizeof(lock_msg), "PINs didn't match - try again");
			wipe_pins();
		}
		return;
	}
	/* LM_ENTER */
	long left = backoff_left();
	if (left > 0) {
		snprintf(lock_msg, sizeof(lock_msg), "Too many attempts - wait %lds", left);
		wipe_pins();
		return;
	}
	snprintf(lock_msg, sizeof(lock_msg), "Checking...");
	render(&lock);
	wl_display_flush(display);
	if (verify_pin(pin)) {
		save_state(0, 0);
		lock_msg[0] = '\0';
		do_unlock();
		return;
	}
	int fails;
	long until;
	load_state(&fails, &until);
	fails++;
	until = 0;
	if (fails >= 3) {
		long delay = 5L << (fails - 3 < 8 ? fails - 3 : 8);   /* 5s, 10s ... */
		if (delay > 900) delay = 900;                          /* max 15 min */
		until = (long)time(NULL) + delay;
		snprintf(lock_msg, sizeof(lock_msg), "Wrong PIN - wait %lds", delay);
	} else {
		snprintf(lock_msg, sizeof(lock_msg), "Wrong PIN");
	}
	save_state(fails, until);
	wipe_pins();
}

static void lock_key(const char *k) {
	if (!session_lock || !lock.configured) {
		return;
	}
	if (k[0] >= '0' && k[0] <= '9') {
		if (pin_len < PIN_MAX) {
			pin[pin_len++] = k[0];
		}
		if (lmode == LM_ENTER || lmode == LM_SET) lock_msg[0] = '\0';
	} else if (k[0] == '<') {
		if (pin_len > 0) {
			pin[--pin_len] = '\0';
		}
	} else {
		submit_pin();
	}
	if (session_lock) {
		render(&lock);
	}
}

static void session_locked_ev(void *d, struct ext_session_lock_v1 *l) {
	session_locked = true;
}

/* The compositor refused or ended the lock: drop our side of it. */
static void session_finished_ev(void *d, struct ext_session_lock_v1 *l) {
	lock_cleanup();
	ext_session_lock_v1_destroy(session_lock);
	session_lock = NULL;
	session_locked = false;
}

static const struct ext_session_lock_v1_listener session_lock_listener = {
	.locked = session_locked_ev,
	.finished = session_finished_ev,
};

static void lock_surface_configure(void *data, struct ext_session_lock_surface_v1 *ls,
		uint32_t serial, uint32_t w, uint32_t h) {
	struct surf *s = data;
	ext_session_lock_surface_v1_ack_configure(ls, serial);
	s->w = (int)w;
	s->h = (int)h;
	s->configured = true;
	render(s);
}

static const struct ext_session_lock_surface_v1_listener lock_surface_listener = {
	.configure = lock_surface_configure,
};

static void shade_set(bool open);

static void lock_start(void) {
	if (session_lock || !lock_mgr || !output) {
		return;
	}
	shade_set(false);
	wipe_pins();
	lock_msg[0] = '\0';
	lmode = pin_exists() ? LM_ENTER : LM_SET;
	session_lock = ext_session_lock_manager_v1_lock(lock_mgr);
	ext_session_lock_v1_add_listener(session_lock, &session_lock_listener, NULL);
	lock.surface = wl_compositor_create_surface(compositor);
	lock.lock_surface = ext_session_lock_v1_get_lock_surface(session_lock, lock.surface, output);
	ext_session_lock_surface_v1_add_listener(lock.lock_surface, &lock_surface_listener, &lock);
}

static void on_sigusr1(int sig) {
	lock_requested = 1;
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
	} else if (s->kind == KIND_LOCK) {
		draw_lock(img, b->data, s->w, s->h);
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

static void shade_set(bool open) {
	if (open == shade_open || !bar.layer) {
		return;
	}
	if (open && output_h <= BAR_H + GESTURE_H) {
		return;                 /* output size not known yet */
	}
	shade_open = open;
	shade_armed = -1;
	/* Same anchors and exclusive zone; only the height changes, so apps under
	 * the panel are not re-laid out. */
	zwlr_layer_surface_v1_set_size(bar.layer, 0, open ? output_h - GESTURE_H : BAR_H);
	wl_surface_commit(bar.surface);      /* the compositor answers with a configure */
}

static void shade_tap(double x, double y) {
	for (int i = 0; i < N_TILES; i++) {
		int tx, ty, tw, th;
		tile_rect(bar.w, i, &tx, &ty, &tw, &th);
		if (x < tx || x >= tx + tw || y < ty || y >= ty + th) {
			continue;
		}
		if ((i == T_REBOOT || i == T_POWEROFF) && shade_armed != i) {
			shade_armed = i;           /* destructive: needs a second tap */
			render(&bar);
			return;
		}
		shade_set(false);
		if (i == T_LOCK) {
			lock_start();
		} else if (i == T_TERM) {
			struct app a = { .entry = "foot" };
			launch(&a);
		} else if (i == T_REBOOT) {
			struct app a = { .entry = "sync; reboot" };
			launch(&a);
		} else {
			struct app a = { .entry = "sync; poweroff" };
			launch(&a);
		}
		return;
	}
	shade_set(false);                    /* tap outside the tiles closes it */
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
	if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
		press_surface = pointer_surface;
		press_y = pointer_y;
		return;
	}
	if (bar.surface && press_surface == bar.surface) {
		double dy = pointer_y - press_y;
		press_surface = NULL;
		if (!shade_open) {
			if (dy > SWIPE || (dy > -12 && dy < 12)) {
				shade_set(true);           /* swipe down or tap */
			}
		} else if (dy < -SWIPE) {
			shade_set(false);              /* swipe up */
		} else if (dy > -12 && dy < 12) {
			shade_tap(pointer_x, pointer_y);
		}
		return;
	}
	press_surface = NULL;
	if (lock.surface && pointer_surface == lock.surface) {
		for (int i = 0; i < 12; i++) {
			int x, y, kw, kh;
			key_rect(lock.w, lock.h, i, &x, &y, &kw, &kh);
			if (pointer_x >= x && pointer_x < x + kw && pointer_y >= y && pointer_y < y + kh) {
				lock_key(keys[i]);
				break;
			}
		}
		return;
	}
	if (pointer_surface != home.surface) {
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

static void k_keymap(void *d, struct wl_keyboard *k, uint32_t fmt, int32_t fd, uint32_t size) {
	close(fd);   /* we only need digits, read as raw evdev keycodes */
}
static void k_enter(void *d, struct wl_keyboard *k, uint32_t serial, struct wl_surface *s,
		struct wl_array *keys_down) {}
static void k_leave(void *d, struct wl_keyboard *k, uint32_t serial, struct wl_surface *s) {}
static void k_key(void *d, struct wl_keyboard *k, uint32_t serial, uint32_t t,
		uint32_t key, uint32_t state) {
	if (state != WL_KEYBOARD_KEY_STATE_PRESSED || !session_lock) {
		return;
	}
	static const char *digits = "1234567890";         /* KEY_1 (2) .. KEY_0 (11) */
	if (key >= 2 && key <= 11) {
		char c[2] = { digits[key - 2], '\0' };
		lock_key(c);
		return;
	}
	switch (key) {
	case 79: lock_key("1"); break; case 80: lock_key("2"); break;  /* keypad */
	case 81: lock_key("3"); break; case 75: lock_key("4"); break;
	case 76: lock_key("5"); break; case 77: lock_key("6"); break;
	case 71: lock_key("7"); break; case 72: lock_key("8"); break;
	case 73: lock_key("9"); break; case 82: lock_key("0"); break;
	case 14: lock_key("<"); break;                          /* backspace */
	case 28: case 96: lock_key("OK"); break;                /* enter, kp enter */
	}
}
static void k_mods(void *d, struct wl_keyboard *k, uint32_t serial, uint32_t dep,
		uint32_t lat, uint32_t lck, uint32_t grp) {}
static void k_repeat(void *d, struct wl_keyboard *k, int32_t rate, int32_t delay) {}

static const struct wl_keyboard_listener keyboard_listener = {
	.keymap = k_keymap, .enter = k_enter, .leave = k_leave, .key = k_key,
	.modifiers = k_mods, .repeat_info = k_repeat,
};

static void seat_caps(void *d, struct wl_seat *s, uint32_t caps) {
	if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !keyboard) {
		keyboard = wl_seat_get_keyboard(s);
		wl_keyboard_add_listener(keyboard, &keyboard_listener, NULL);
	}
	if ((caps & WL_SEAT_CAPABILITY_POINTER) && !pointer) {
		pointer = wl_seat_get_pointer(s);
		wl_pointer_add_listener(pointer, &pointer_listener, NULL);
	}
}

static void seat_name(void *d, struct wl_seat *s, const char *name) {}

static const struct wl_seat_listener seat_listener = {
	.capabilities = seat_caps, .name = seat_name,
};

static void out_geometry(void *d, struct wl_output *o, int32_t x, int32_t y, int32_t pw,
		int32_t ph, int32_t sub, const char *make, const char *model, int32_t tr) {}
static void out_mode(void *d, struct wl_output *o, uint32_t flags, int32_t w, int32_t h,
		int32_t refresh) {
	if (flags & WL_OUTPUT_MODE_CURRENT) {
		output_h = h;
	}
}
static const struct wl_output_listener output_listener = {
	.geometry = out_geometry, .mode = out_mode,
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
	} else if (strcmp(iface, ext_session_lock_manager_v1_interface.name) == 0) {
		lock_mgr = wl_registry_bind(r, name, &ext_session_lock_manager_v1_interface, 1);
	} else if (strcmp(iface, wl_output_interface.name) == 0 && !output) {
		output = wl_registry_bind(r, name, &wl_output_interface, 1);
		wl_output_add_listener(output, &output_listener, NULL);
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

	struct sigaction sa = { .sa_handler = on_sigusr1 };   /* no SA_RESTART: poll wakes */
	sigemptyset(&sa.sa_mask);
	sigaction(SIGUSR1, &sa, NULL);

	if (!lock_mgr || !output) {
		fprintf(stderr, "flipos-shell: no ext-session-lock; lock screen unavailable\n");
	}
	lock_start();            /* boot straight into the lock screen */
	scan_apps();
	make_surface(&home);
	make_surface(&bar);

	while (running) {
		/* Wake at the next minute so the clock stays right. */
		time_t now = time(NULL);
		struct tm tm;
		localtime_r(&now, &tm);
		int timeout_ms = shade_open ? 2000 : (60 - tm.tm_sec) * 1000;   /* live stats */

		while (wl_display_prepare_read(display) != 0) {
			wl_display_dispatch_pending(display);
		}
		wl_display_flush(display);
		struct pollfd pfd = { .fd = wl_display_get_fd(display), .events = POLLIN };
		int ret = poll(&pfd, 1, timeout_ms);
		if (lock_requested) {
			lock_requested = 0;
			lock_start();
		}
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
			if (session_lock) {
				render(&lock);   /* clock */
			}
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
