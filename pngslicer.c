#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <glob.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wand/MagickWand.h>

// If the above still fails in some environments, you might need:
// #include <MagickWand/MagickWand.h>

#define VERSION "1.0.0"
#define OPT_HELP 300
#define OPT_OVERWRITE 301

typedef struct _CCObjectInfo {
  size_t id;
  RectangleInfo offset;
  double width, height, area;
  PointInfo centroid;
  double color_error;
  struct _CCObjectInfo *next;
} CCObjectInfo;

typedef struct {
  int *buf;
  size_t n, cap;
} IntBag;

typedef struct {
  char *input_file;
  char output_dir_storage[1024];
  const char *output_dir;
  char format_storage[1024];
  char *format_template;
  char legacy_stem[256];
  int legacy_stem_set;
  int min_width, min_height, min_area;
  int w_set, h_set, a_set, json_mode;
  int start_at;
  int overwrite;
  int zero_padding;
} Config;

static void bag_init(IntBag *b) {
  b->buf = NULL;
  b->n = 0;
  b->cap = 0;
}

static void bag_free(IntBag *b) {
  free(b->buf);
  b->buf = NULL;
  b->n = b->cap = 0;
}

static int bag_has(const IntBag *b, int v) {
  for (size_t i = 0; i < b->n; i++)
    if (b->buf[i] == v)
      return 1;
  return 0;
}

static void bag_add(IntBag *b, int v) {
  if (bag_has(b, v))
    return;
  if (b->n >= b->cap) {
    size_t nc = b->cap ? b->cap * 2 : 32;
    int *nb = (int *)realloc(b->buf, nc * sizeof(int));
    if (!nb)
      return;
    b->buf = nb;
    b->cap = nc;
  }
  b->buf[b->n++] = v;
}

static void format_index_num(char *buf, size_t buflen, int idx, int zero_padding) {
  if (zero_padding <= 0)
    snprintf(buf, buflen, "%d", idx);
  else {
    int w = zero_padding + 1;
    snprintf(buf, buflen, "%0*d", w, idx);
  }
}

static void strip_trailing_slashes(char *s) {
  size_t n = strlen(s);
  while (n > 1 && s[n - 1] == '/')
    s[--n] = '\0';
}

static int mkdir_p_path(const char *path, int json_mode) {
  char tmp[1024];
  if (strlen(path) >= sizeof(tmp)) {
    if (!json_mode)
      fprintf(stderr,
              "pngslicer: output path too long (directory create failed)\n");
    return -1;
  }
  snprintf(tmp, sizeof(tmp), "%s", path);
  strip_trailing_slashes(tmp);
  if (tmp[0] == '\0')
    return 0;

  for (char *p = tmp + 1; *p; p++) {
    if (*p == '/') {
      *p = '\0';
      if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        if (!json_mode)
          fprintf(stderr, "pngslicer: could not create directory \"%s\": %s\n",
                  tmp, strerror(errno));
        return -1;
      }
      *p = '/';
    }
  }
  if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
    if (!json_mode)
      fprintf(stderr, "pngslicer: could not create directory \"%s\": %s\n", tmp,
              strerror(errno));
    return -1;
  }
  return 0;
}

static int ensure_parent_of_file(const char *filepath, int json_mode) {
  char tmp[1024];
  if (strlen(filepath) >= sizeof(tmp))
    return 0;
  snprintf(tmp, sizeof(tmp), "%s", filepath);
  char *slash = strrchr(tmp, '/');
  if (!slash)
    return 0;
  *slash = '\0';
  if (tmp[0])
    return mkdir_p_path(tmp, json_mode);
  return 0;
}

static void print_help(const char *prog) {
  printf("PNGSlicer v%s - High-performance sprite extraction tool\n", VERSION);
  printf("Developed for JREAM - https://jream.com\n\n");
  printf("Usage: %s <input.png> [options]\n", prog);
  printf("\n");
  printf("Connected sub-images that pass the filter are exported. By default "
         "each\n");
  printf("region must be at least --min-width AND --min-height pixels. If you "
         "set\n");
  printf("--min-area instead, only area is used (you cannot combine --min-area "
         "with\n");
  printf("--min-width or --min-height).\n");
  printf("\nOptions:\n");
  printf("  -o, --output <path>    Output directory (e.g. ./ out/ sprites) or a "
         "legacy\n");
  printf("                        file path (e.g. dir/base.png --> dir/base-1.png); "
         "mkdir -p\n");
  printf("                        as needed. Default directory: ./pngslicer/\n");
  printf("  -d <path>              Same as a directory-only -o: \".\", "
         "\"./dir\", \"dir/\" ...\n");
  printf("  -f, --format <pat>    Filename under the output dir; must include "
         "one %%d\n");
  printf("                        (e.g. %%d-sprite.png). With -d ./out writes "
         "./out/1-sprite.png ...\n");
  printf("  -w, --min-width <n>    Minimum width (default: 50; with -a, "
         "ignored)\n");
  printf("  -h, --min-height <n>   Minimum height (default: 50; with -a, "
         "ignored)\n");
  printf("  -a, --min-area <n>     Minimum pixel area; replaces width×height "
         "filter\n");
  printf("  -s, --start-at <n>     First index with --overwrite (default: 1). "
         "Otherwise\n");
  printf("                        picks unused indices (gap-fill). With "
         "--zero-padding, names\n");
  printf("                        use that width (e.g. --start-at 1 N=1 --> "
         "\"...-01.png\").\n");
  printf("  -z, --zero-padding <n> Pad index digits: 1-->01, 2-->001, 3-->0001; "
         "0=off (default)\n");
  printf("      --overwrite        Overwrite existing files without prompting; ""\n");
  printf("  -j, --json             Output results in JSON (no interactive "
         "prompts)\n");
  printf("  -v, --version          Show version and exit\n");
  printf("      --help             Show this help menu and exit\n");
  printf("\nExamples:\n");
  printf("  %s sheet.png\n", prog);
  printf("      # outputs: ./pngslicer/sheet-1.png, sheet-2.png, ... (default folder)\n\n");
  printf("  %s sheet.png -d deep/out -f \"tile-%%d.png\"\n", prog);
  printf("      # outputs: deep/out/tile-1.png, deep/out/tile-2.png, ...\n\n");
  printf("  %s in.png -o assets/base.png --json\n", prog);
  printf("      # outputs: assets/base-1.png, ... with JSON reporting\n");
}

static int count_percent_d(const char *s) {
  int n = 0;
  for (; *s; s++)
    if (s[0] == '%' && s[1] == 'd')
      n++;
  return n;
}

static int path_is_directory_spec(const char *arg) {
  size_t n = strlen(arg);
  if (n == 0)
    return 0;
  if (strcmp(arg, ".") == 0)
    return 1;
  if (arg[n - 1] == '/')
    return 1;
  const char *base = strrchr(arg, '/');
  base = base ? base + 1 : arg;
  if (strchr(base, '.'))
    return 0;
  if (strstr(arg, "%d"))
    return 0;
  return 1;
}

static void set_dir_from_arg(Config *cfg, const char *arg) {
  size_t n = strlen(arg);
  if (n >= sizeof(cfg->output_dir_storage) - 4)
    return;
  if (strcmp(arg, ".") == 0) {
    snprintf(cfg->output_dir_storage, sizeof(cfg->output_dir_storage), "./");
  } else if (n > 0 && arg[n - 1] == '/') {
    snprintf(cfg->output_dir_storage, sizeof(cfg->output_dir_storage), "%s",
             arg);
  } else {
    snprintf(cfg->output_dir_storage, sizeof(cfg->output_dir_storage), "%s/",
             arg);
  }
  cfg->output_dir = cfg->output_dir_storage;
}

static void apply_o_path(Config *cfg, const char *arg) {
  if (path_is_directory_spec(arg)) {
    set_dir_from_arg(cfg, arg);
    cfg->legacy_stem_set = 0;
    return;
  }
  char tmp[1024];
  snprintf(tmp, sizeof(tmp), "%s", arg);
  char *slash = strrchr(tmp, '/');
  if (slash) {
    *slash = '\0';
    const char *dirpart = tmp[0] ? tmp : ".";
    set_dir_from_arg(cfg, dirpart);
    const char *base = slash + 1;
    if (strstr(base, "%d")) {
      snprintf(cfg->format_storage, sizeof(cfg->format_storage), "%s", base);
      cfg->format_template = cfg->format_storage;
      cfg->legacy_stem_set = 0;
    } else {
      char stem[256];
      snprintf(stem, sizeof(stem), "%s", base);
      char *dot = strrchr(stem, '.');
      if (dot)
        *dot = '\0';
      strncpy(cfg->legacy_stem, stem, sizeof(cfg->legacy_stem) - 1);
      cfg->legacy_stem[sizeof(cfg->legacy_stem) - 1] = '\0';
      cfg->legacy_stem_set = 1;
      cfg->format_template = NULL;
    }
  } else {
    set_dir_from_arg(cfg, ".");
    if (strstr(arg, "%d")) {
      snprintf(cfg->format_storage, sizeof(cfg->format_storage), "%s", arg);
      cfg->format_template = cfg->format_storage;
      cfg->legacy_stem_set = 0;
    } else {
      char stem[256];
      snprintf(stem, sizeof(stem), "%s", arg);
      char *dot = strrchr(stem, '.');
      if (dot)
        *dot = '\0';
      strncpy(cfg->legacy_stem, stem, sizeof(cfg->legacy_stem) - 1);
      cfg->legacy_stem[sizeof(cfg->legacy_stem) - 1] = '\0';
      cfg->legacy_stem_set = 1;
      cfg->format_template = NULL;
    }
  }
}

static int try_parse_index_dir(const char *fname, const char *bname) {
  size_t bl = strlen(bname);
  if (strncmp(fname, bname, bl) != 0 || fname[bl] != '-')
    return -1;
  const char *num = fname + bl + 1;
  char *dot = strrchr(fname, '.');
  if (!dot || strcmp(dot, ".png") != 0)
    return -1;
  char *end = NULL;
  long v = strtol(num, &end, 10);
  if (end != dot || v < 0 || v > (long)INT_MAX)
    return -1;
  return (int)v;
}

static int try_parse_template_file(const char *basename,
                                   const char *prefix,
                                   const char *suffix) {
  size_t pl = strlen(prefix), sl = strlen(suffix);
  size_t bl = strlen(basename);
  if (bl < pl + sl || strncmp(basename, prefix, pl) != 0 ||
      strcmp(basename + bl - sl, suffix) != 0)
    return -1;
  const char *mid = basename + pl;
  char *end = NULL;
  long v = strtol(mid, &end, 10);
  if (end != basename + bl - sl || v < 0 || v > (long)INT_MAX)
    return -1;
  return (int)v;
}

static void collect_indices_dir_mode(IntBag *bag,
                                     const char *dir,
                                     const char *bname) {
  char globpat[4096];
  snprintf(globpat, sizeof(globpat), "%s%s-*.png", dir, bname);
  glob_t g;
  if (glob(globpat, 0, NULL, &g) != 0)
    return;
  for (size_t i = 0; i < g.gl_pathc; i++) {
    char *slash = strrchr(g.gl_pathv[i], '/');
    const char *fname = slash ? slash + 1 : g.gl_pathv[i];
    int idx = try_parse_index_dir(fname, bname);
    if (idx >= 0)
      bag_add(bag, idx);
  }
  globfree(&g);
}

static void collect_indices_pctd(IntBag *bag, const char *template_path) {
  const char *pct = strstr(template_path, "%d");
  if (!pct)
    return;
  char globpat[4096];
  size_t before = (size_t)(pct - template_path);
  snprintf(globpat, sizeof(globpat), "%.*s*%s", (int)before, template_path,
           pct + 2);
  const char *slash = strrchr(template_path, '/');
  const char *base_start = slash ? slash + 1 : template_path;
  const char *pct_base = strstr(base_start, "%d");
  if (!pct_base)
    return;
  char prefix[512], suffix[512];
  snprintf(prefix, sizeof(prefix), "%.*s", (int)(pct_base - base_start),
           base_start);
  snprintf(suffix, sizeof(suffix), "%s", pct_base + 2);

  glob_t g;
  if (glob(globpat, 0, NULL, &g) != 0)
    return;
  for (size_t i = 0; i < g.gl_pathc; i++) {
    char *sl = strrchr(g.gl_pathv[i], '/');
    const char *fname = sl ? sl + 1 : g.gl_pathv[i];
    int idx = try_parse_template_file(fname, prefix, suffix);
    if (idx >= 0)
      bag_add(bag, idx);
  }
  globfree(&g);
}

static void collect_indices_for_cfg(IntBag *bag,
                                    const Config *cfg,
                                    const char *bname) {
  if (cfg->format_template) {
    char fullpat[2048];
    snprintf(fullpat, sizeof(fullpat), "%s%s", cfg->output_dir,
             cfg->format_template);
    collect_indices_pctd(bag, fullpat);
  } else {
    const char *stem = cfg->legacy_stem_set ? cfg->legacy_stem : bname;
    collect_indices_dir_mode(bag, cfg->output_dir, stem);
  }
}

static void build_final_path(char *final_name,
                             size_t fn_size,
                             const Config *cfg,
                             const char *bname,
                             int idx) {
  char numbuf[32];
  format_index_num(numbuf, sizeof(numbuf), idx, cfg->zero_padding);

  if (cfg->format_template) {
    char rel[1024];
    const char *pct = strstr(cfg->format_template, "%d");
    if (cfg->zero_padding <= 0)
      snprintf(rel, sizeof(rel), cfg->format_template, idx);
    else
      snprintf(rel, sizeof(rel), "%.*s%s%s",
               (int)(pct - cfg->format_template), cfg->format_template, numbuf,
               pct + 2);
    snprintf(final_name, fn_size, "%s%s", cfg->output_dir, rel);
    return;
  }
  const char *stem = cfg->legacy_stem_set ? cfg->legacy_stem : bname;
  snprintf(final_name, fn_size, "%s%s-%s.png", cfg->output_dir, stem, numbuf);
}

static int next_gap_index(IntBag *bag, int start_at) {
  for (int n = start_at; n < INT_MAX - 1; n++) {
    if (!bag_has(bag, n)) {
      bag_add(bag, n);
      return n;
    }
  }
  return start_at;
}

int main(int argc, char **argv) {
  if (argc <= 1) {
    print_help(argv[0]);
    return 0;
  }

  static const char default_out_dir[] = "./pngslicer/";
  Config cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.min_width = 50;
  cfg.min_height = 50;
  cfg.min_area = 1024;
  cfg.start_at = 1;
  cfg.output_dir = default_out_dir;

  static struct option long_options[] = {
      {"help", no_argument, 0, OPT_HELP},
      {"output", required_argument, 0, 'o'},
      {"dir", required_argument, 0, 'd'},
      {"min-width", required_argument, 0, 'w'},
      {"min-height", required_argument, 0, 'h'},
      {"min-area", required_argument, 0, 'a'},
      {"json", no_argument, 0, 'j'},
      {"version", no_argument, 0, 'v'},
      {"start-at", required_argument, 0, 's'},
      {"overwrite", no_argument, 0, OPT_OVERWRITE},
      {"format", required_argument, 0, 'f'},
      {"zero-padding", required_argument, 0, 'z'},
      {0, 0, 0, 0}};

  int opt;
  while ((opt = getopt_long(argc, argv, "o:d:w:h:a:jvs:z:f:", long_options,
                            NULL)) != -1) {
    switch (opt) {
    case OPT_HELP:
      print_help(argv[0]);
      return 0;
    case OPT_OVERWRITE:
      cfg.overwrite = 1;
      break;
    case 'v':
      printf("pngslicer version %s\n", VERSION);
      return 0;
    case 'o':
      apply_o_path(&cfg, optarg);
      break;
    case 'd':
      if (!path_is_directory_spec(optarg)) {
        char *m = "-d expects a directory (e.g. . ./out out/); use -o for "
                  "file patterns.";
        if (cfg.json_mode)
          printf("{\n  \"status\": \"error\",\n  \"message\": \"%s\"\n}\n", m);
        else
          printf("status: error\nmessage: %s\n", m);
        return 1;
      }
      if (strlen(optarg) >= sizeof(cfg.output_dir_storage) - 4) {
        char *m = "Output directory path too long.";
        if (cfg.json_mode)
          printf("{\n  \"status\": \"error\",\n  \"message\": \"%s\"\n}\n", m);
        else
          printf("status: error\nmessage: %s\n", m);
        return 1;
      }
      set_dir_from_arg(&cfg, optarg);
      cfg.legacy_stem_set = 0;
      break;
    case 'w':
      cfg.min_width = atoi(optarg);
      cfg.w_set = 1;
      break;
    case 'h':
      cfg.min_height = atoi(optarg);
      cfg.h_set = 1;
      break;
    case 'a':
      cfg.min_area = atoi(optarg);
      cfg.a_set = 1;
      break;
    case 'j':
      cfg.json_mode = 1;
      break;
    case 's':
      cfg.start_at = atoi(optarg);
      break;
    case 'f': {
      int nd = count_percent_d(optarg);
      if (nd != 1) {
        char *m = "--format / -f must contain exactly one %d.";
        if (cfg.json_mode)
          printf("{\n  \"status\": \"error\",\n  \"message\": \"%s\"\n}\n", m);
        else
          printf("status: error\nmessage: %s\n", m);
        return 1;
      }
      if (strlen(optarg) >= sizeof(cfg.format_storage)) {
        char *m = "Format string too long.";
        if (cfg.json_mode)
          printf("{\n  \"status\": \"error\",\n  \"message\": \"%s\"\n}\n", m);
        else
          printf("status: error\nmessage: %s\n", m);
        return 1;
      }
      snprintf(cfg.format_storage, sizeof(cfg.format_storage), "%s", optarg);
      cfg.format_template = cfg.format_storage;
      cfg.legacy_stem_set = 0;
      break;
    }
    case 'z':
      cfg.zero_padding = atoi(optarg);
      break;
    default:
      print_help(argv[0]);
      return 0;
    }
  }

  if (cfg.zero_padding < 0 || cfg.zero_padding > 3) {
    char *m = "--zero-padding must be between 0 and 3.";
    if (cfg.json_mode)
      printf("{\n  \"status\": \"error\",\n  \"message\": \"%s\"\n}\n", m);
    else
      printf("status: error\nmessage: %s\n", m);
    return 1;
  }

  if (optind >= argc) {
    char *m = "Missing input PNG file.";
    if (cfg.json_mode)
      printf("{\n  \"status\": \"error\",\n  \"message\": \"%s\"\n}\n", m);
    else
      printf("status: error\nmessage: %s\n", m);
    return 1;
  }
  cfg.input_file = argv[optind];

  if (cfg.a_set && (cfg.w_set || cfg.h_set)) {
    char *m = "choose min-area without width and height";
    if (cfg.json_mode)
      printf("{\n  \"status\": \"error\",\n  \"message\": \"%s\"\n}\n", m);
    else
      printf("status: error\nmessage: %s\n", m);
    return 1;
  }

  char bname_stripped[1024];
  {
    char src_copy[1024];
    strncpy(src_copy, cfg.input_file, sizeof(src_copy) - 1);
    src_copy[sizeof(src_copy) - 1] = '\0';
    char *bn = basename(src_copy);
    strncpy(bname_stripped, bn, sizeof(bname_stripped) - 1);
    bname_stripped[sizeof(bname_stripped) - 1] = '\0';
    char *dot = strrchr(bname_stripped, '.');
    if (dot)
      *dot = '\0';
  }

  {
    char tdir[1024];
    strncpy(tdir, cfg.output_dir, sizeof(tdir) - 1);
    tdir[sizeof(tdir) - 1] = '\0';
    strip_trailing_slashes(tdir);
    if (tdir[0] && mkdir_p_path(tdir, cfg.json_mode) != 0) {
      if (cfg.json_mode)
        printf("{\n  \"status\": \"error\",\n  \"message\": \"Could not create "
               "output directory.\"\n}\n");
      return 1;
    }
  }

  MagickWandGenesis();
  MagickWand *mw = NewMagickWand();
  int status = 0;
  if (MagickReadImage(mw, cfg.input_file) == MagickFalse) {
    DestroyMagickWand(mw);
    MagickWandTerminus();
    return 1;
  }

  MagickWand *mask_mw = CloneMagickWand(mw);
  if (MagickGetImageAlphaChannel(mw) == MagickTrue) {
    MagickSetImageAlphaChannel(mask_mw, ExtractAlphaChannel);
  } else {
    MagickSetImageAlphaChannel(mask_mw, DeactivateAlphaChannel);
  }
  MagickThresholdImage(mask_mw, 0.5 * QuantumRange);

  ExceptionInfo *exception = AcquireExceptionInfo();
  Image *mask_image = GetImageFromMagickWand(mask_mw);
  Image *labeled_image = ConnectedComponentsImage(mask_image, 4, exception);
  if (!labeled_image) {
    if (cfg.json_mode)
      printf("{\n \"status\": \"error\", \"message\": \"Failed to label "
             "components\"\n}\n");
    else
      printf("status: error\nmessage: Failed to label components\n");
    DestroyMagickWand(mask_mw);
    DestroyMagickWand(mw);
    MagickWandTerminus();
    return 1;
  }

  size_t width = labeled_image->columns;
  size_t height = labeled_image->rows;
  CCObjectInfo *objects = NULL;

  for (ssize_t y = 0; y < (ssize_t)height; y++) {
    const PixelPacket *pixels =
        GetVirtualPixels(labeled_image, 0, y, width, 1, exception);
    if (!pixels)
      break;
    for (ssize_t x = 0; x < (ssize_t)width; x++) {
      size_t id = (size_t)GetPixelGray(pixels + x);
      CCObjectInfo *found = NULL, *prev = NULL;
      for (found = objects; found != NULL; prev = found, found = found->next) {
        if (found->id == id)
          break;
      }

      if (!found) {
        found = (CCObjectInfo *)calloc(1, sizeof(CCObjectInfo));
        found->id = id;
        found->offset.x = x;
        found->offset.y = y;
        found->offset.width = 1;
        found->offset.height = 1;
        found->width = x;
        found->height = y;
        found->area = 0;
        if (prev)
          prev->next = found;
        else
          objects = found;
      }

      if (x < found->width)
        found->width = x;
      if (x > (int)found->offset.width)
        found->offset.width = x;
      if (y < found->height)
        found->height = y;
      if (y > (int)found->offset.height)
        found->offset.height = y;
      found->area++;
      found->centroid.x += x;
      found->centroid.y += y;
    }
  }

  CCObjectInfo *curr;
  for (curr = objects; curr != NULL; curr = curr->next) {
    int min_x = (int)curr->width;
    int max_x = (int)curr->offset.width;
    int min_y = (int)curr->height;
    int max_y = (int)curr->offset.height;
    curr->width = (double)(max_x - min_x + 1);
    curr->height = (double)(max_y - min_y + 1);
    curr->offset.x = min_x;
    curr->offset.y = min_y;
    curr->offset.width = (size_t)curr->width;
    curr->offset.height = (size_t)curr->height;
    curr->centroid.x /= curr->area;
    curr->centroid.y /= curr->area;
  }

  size_t bg_id = 0;
  double max_a = -1;
  for (curr = objects; curr != NULL; curr = curr->next) {
    if (curr->area > max_a) {
      max_a = curr->area;
      bg_id = curr->id;
    }
  }

  int valid_total = 0;
  for (curr = objects; curr != NULL; curr = curr->next) {
    if (curr->id == bg_id)
      continue;
    int match = cfg.a_set ? (curr->area >= (double)cfg.min_area)
                          : (curr->width >= (double)cfg.min_width &&
                             curr->height >= (double)cfg.min_height);
    if (match)
      valid_total++;
  }

  if (valid_total >= 1) {
    IntBag used;
    bag_init(&used);
    if (!cfg.overwrite)
      collect_indices_for_cfg(&used, &cfg, bname_stripped);

    int *indices = (int *)malloc((size_t)valid_total * sizeof(int));
    if (!indices) {
      bag_free(&used);
      status = 1;
      goto cleanup_objects;
    }

    int pos = 0;
    for (curr = objects; curr != NULL; curr = curr->next) {
      if (curr->id == bg_id)
        continue;
      int match = cfg.a_set ? (curr->area >= (double)cfg.min_area)
                            : (curr->width >= (double)cfg.min_width &&
                               curr->height >= (double)cfg.min_height);
      if (!match)
        continue;
      if (cfg.overwrite)
        indices[pos] = cfg.start_at + pos;
      else
        indices[pos] = next_gap_index(&used, cfg.start_at);
      pos++;
    }

    bag_free(&used);

    if (!cfg.overwrite) {
      char tmp_path[2048];
      char **clist = (char **)calloc((size_t)valid_total, sizeof(char *));
      int nconf = 0;
      if (!clist) {
        free(indices);
        status = 1;
        goto cleanup_objects;
      }
      for (int i = 0; i < valid_total; i++) {
        build_final_path(tmp_path, sizeof(tmp_path), &cfg, bname_stripped,
                         indices[i]);
        if (access(tmp_path, F_OK) == 0) {
          clist[nconf] = strdup(tmp_path);
          if (clist[nconf])
            nconf++;
        }
      }
      if (nconf > 0) {
        if (cfg.json_mode) {
          printf("{\n  \"status\": \"error\",\n  \"message\": \"Output files "
                 "already exist. Use --overwrite or pick a different "
                 "output.\"\n}\n");
          for (int i = 0; i < nconf; i++)
            free(clist[i]);
          free(clist);
          free(indices);
          status = 1;
          goto cleanup_objects;
        }
        printf("These files already exist:\n");
        for (int i = 0; i < nconf; i++)
          printf("%s\n", clist[i]);
        printf("Overwrite all? [y/N] ");
        char response[16];
        if (fgets(response, sizeof(response), stdin) == NULL ||
            (response[0] != 'y' && response[0] != 'Y')) {
          printf("Aborted.\n");
          for (int i = 0; i < nconf; i++)
            free(clist[i]);
          free(clist);
          free(indices);
          status = 1;
          goto cleanup_objects;
        }
        for (int i = 0; i < nconf; i++)
          free(clist[i]);
      }
      free(clist);
    }

    if (cfg.json_mode)
      printf("{\n  \"status\": \"success\",\n  \"src\": \"%s\",\n  \"output\": "
             "[\n",
             cfg.input_file);
    else
      printf("status: success\n");

    int counter = 0;
    int write_aborted = 0;
    for (curr = objects; curr != NULL; curr = curr->next) {
      if (curr->id == bg_id)
        continue;
      int match = cfg.a_set ? (curr->area >= (double)cfg.min_area)
                            : (curr->width >= (double)cfg.min_width &&
                               curr->height >= (double)cfg.min_height);
      if (!match)
        continue;

      int idx = indices[counter];
      char final_name[2048];
      build_final_path(final_name, sizeof(final_name), &cfg, bname_stripped,
                       idx);

      if (ensure_parent_of_file(final_name, cfg.json_mode) != 0) {
        if (!cfg.json_mode)
          fprintf(stderr,
                  "pngslicer: could not create parent directory for \"%s\"\n",
                  final_name);
        write_aborted = 1;
        break;
      }

      MagickWand *crop = CloneMagickWand(mw);
      MagickCropImage(crop, (size_t)curr->width, (size_t)curr->height,
                      curr->offset.x, curr->offset.y);

      int existed = (access(final_name, F_OK) == 0);
      MagickWriteImage(crop, final_name);

      struct stat st;
      double file_kb = 0.0;
      if (stat(final_name, &st) == 0) {
        file_kb = (double)st.st_size / 1024.0;
      }

      if (cfg.json_mode) {
        printf("    { \"filename\": \"%s\", \"dimensions\": [%d, %d], "
               "\"size\": \"%.1fkb\"%s }%s\n",
               final_name, (int)curr->width, (int)curr->height, file_kb,
               (existed) ? ", \"overwritten\": true" : "",
               (counter + 1 < valid_total) ? "," : "");
      } else {
        printf("- %s (%d x %d) (%.1fkb)%s\n", final_name, (int)curr->width,
               (int)curr->height, file_kb,
               (existed) ? " (overwritten)" : "");
      }
      DestroyMagickWand(crop);
      counter++;
    }
    free(indices);
    status = write_aborted ? 1 : 0;
  } else {
    char err[256];
    snprintf(err, sizeof(err),
             "no sub-images with min-width %d, min-height %d, or area %d",
             cfg.min_width, cfg.min_height, cfg.min_area);
    if (cfg.json_mode) {
      printf("{\n  \"status\": \"error\",\n  \"src\": \"%s\",\n  \"output\": "
             "[],\n  \"message\": \"%s\"\n}\n",
             cfg.input_file, err);
    } else {
      printf("status: error\nmessage: %s\n", err);
    }
    status = 1;
  }

cleanup_objects:
  curr = objects;
  while (curr) {
    CCObjectInfo *next = curr->next;
    free(curr);
    curr = next;
  }

  if (labeled_image)
    DestroyImage(labeled_image);
  DestroyMagickWand(mask_mw);
  DestroyExceptionInfo(exception);
  DestroyMagickWand(mw);
  MagickWandTerminus();
  return status;
}
