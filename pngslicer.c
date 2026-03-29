#include <getopt.h>
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

typedef struct _CCObjectInfo {
  size_t id;
  RectangleInfo offset;
  double width, height, area;
  PointInfo centroid;
  double color_error;
  struct _CCObjectInfo *next;
} CCObjectInfo;

typedef struct {
  char *input_file;
  char *output_pattern;
  int min_width, min_height, min_area;
  int w_set, h_set, a_set, json_mode;
  int start_at;
  int force_overwrite;
} Config;

void prepare_path(const char *path) {
  char temp[1024];
  char *p = NULL;
  snprintf(temp, sizeof(temp), "%s", path);
  for (p = temp + 1; *p; p++) {
    if (*p == '/') {
      *p = 0;
      mkdir(temp, S_IRWXU);
      *p = '/';
    }
  }
}

int main(int argc, char **argv) {
  Config cfg = {NULL, NULL, 50, 50, 1024, 0, 0, 0, 0, 1, 0};
  int status = 0;
  static struct option long_options[] = {
      {"help", no_argument, 0, 'h'},
      {"output", required_argument, 0, 'o'},
      {"min-width", required_argument, 0, 'w'},
      {"min-height", required_argument, 0, 'e'},
      {"min-area", required_argument, 0, 'a'},
      {"json", no_argument, 0, 'j'},
      {"version", no_argument, 0, 'v'},
      {"start-at", required_argument, 0, 's'},
      {"force", no_argument, 0, 'f'},
      {0, 0, 0, 0}};

  int opt;
  while ((opt = getopt_long(argc, argv, "ho:w:e:a:jvs:f", long_options,
                            NULL)) != -1) {
    switch (opt) {
    case 'v':
      printf("pngslicer version %s\n", VERSION);
      return 0;
    case 'h':
      printf("PNGSlicer v%s - High-performance sprite extraction tool\n",
             VERSION);
      printf("Developed for JREAM - https://jream.com\n\n");
      printf("Usage: %s <input.png> -o <output_pattern> [options]\n", argv[0]);
      printf("\nOptions:\n");
      printf("  -o, --output <path>    Output pattern (e.g. dir/ or "
             "file-%%d.png)\n");
      printf(
          "  -w, --min-width <n>    Minimum width to extract (default: 50)\n");
      printf(
          "  -e, --min-height <n>   Minimum height to extract (default: 50)\n");
      printf("  -a, --min-area <n>     Minimum area to extract\n");
      printf("  -s, --start-at <n>     Starting index for numbering (default: "
             "1)\n");
      printf("  -f, --force            Overwrite existing files without "
             "prompting\n");
      printf("  -j, --json             Output results in JSON format\n");
      printf("  -v, --version          Show version and exit\n");
      printf("  -h, --help             Show this help menu and exit\n");
      printf("\nExamples:\n");
      printf("  %s sheet.png -o sprites/         # Extract all into sprites "
             "folder\n",
             argv[0]);
      printf("  %s in.png -o sp_%%d.png -s 10    # Start numbering at 10\n",
             argv[0]);
      printf("  %s in.png -o out.png --json      # Get JSON reporting\n",
             argv[0]);
      return 0;
    case 'o':
      cfg.output_pattern = optarg;
      break;
    case 'w':
      cfg.min_width = atoi(optarg);
      cfg.w_set = 1;
      break;
    case 'e':
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
    case 'f':
      cfg.force_overwrite = 1;
      break;
    }
  }

  if (optind < argc)
    cfg.input_file = argv[optind];

  // Make sure the user isn't mixing up incompatible flags.
  if (cfg.a_set && (cfg.w_set || cfg.h_set)) {
    char *m = "choose min-area without width and height";
    if (cfg.json_mode)
      printf("{\n  \"status\": \"error\",\n  \"message\": \"%s\"\n}\n", m);
    else
      printf("status: error\nmessage: %s\n", m);
    return 1;
  }

  if (!cfg.input_file || !cfg.output_pattern) {
    char *m = "Missing input file or output (-o) destination.";
    if (cfg.json_mode)
      printf("{\n  \"status\": \"error\",\n  \"message\": \"%s\"\n}\n", m);
    else
      printf("status: error\nmessage: %s\n", m);
    return 1;
  }

  // Figure out if they want to dump the output into a directory or use a
  // specific file name pattern.
  size_t out_len = strlen(cfg.output_pattern);
  int is_dir = (cfg.output_pattern[out_len - 1] == '/');
  if (!is_dir && !strrchr(cfg.output_pattern, '.')) {
    char *m = "If you want to output into a directory with the same "
              "filename(s) include a trailing slash (/)";
    if (cfg.json_mode)
      printf("{\n  \"status\": \"error\",\n  \"message\": \"%s\"\n}\n", m);
    else
      printf("status: error\nmessage: %s\n", m);
    return 1;
  }

  MagickWandGenesis();
  MagickWand *mw = NewMagickWand();
  if (MagickReadImage(mw, cfg.input_file) == MagickFalse)
    return 1;

  // We need a clean black and white mask to figure out where the separate
  // pieces (sprites) are located.
  MagickWand *mask_mw = CloneMagickWand(mw);
  if (MagickGetImageAlphaChannel(mw) == MagickTrue) {
    MagickSetImageAlphaChannel(mask_mw, ExtractAlphaChannel);
  } else {
    MagickSetImageAlphaChannel(mask_mw, DeactivateAlphaChannel);
  }
  MagickThresholdImage(mask_mw, 0.5 * QuantumRange);

  if (strrchr(cfg.output_pattern, '/'))
    prepare_path(cfg.output_pattern);

  ExceptionInfo *exception = AcquireExceptionInfo();
  Image *mask_image = GetImageFromMagickWand(mask_mw);

  // ImageMagick hands us back a 'labeled' image where every pixel's color
  // matches its component ID.
  Image *labeled_image = ConnectedComponentsImage(mask_image, 4, exception);
  if (!labeled_image) {
    if (cfg.json_mode)
      printf("{\n \"status\": \"error\", \"message\": \"Failed to label "
             "components\"\n}\n");
    else
      printf("status: error\nmessage: Failed to label components\n");
    return 1;
  }

  // Time to scan through the pixels. We have to map out the bounding box for
  // each piece manually.
  size_t width = labeled_image->columns;
  size_t height = labeled_image->rows;

  // We don't know exactly how many pieces there are yet, so we'll dynamically
  // build out a linked list as we find new ones.
  CCObjectInfo *objects = NULL;

  for (ssize_t y = 0; y < (ssize_t)height; y++) {
    const PixelPacket *pixels =
        GetVirtualPixels(labeled_image, 0, y, width, 1, exception);
    if (!pixels)
      break;
    for (ssize_t x = 0; x < (ssize_t)width; x++) {
      size_t id = (size_t)GetPixelGray(pixels + x);

      // Did we already find this piece? If not, we'll start tracking a new one.
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
        found->offset.width = 1;  // Used temporarily for max_x
        found->offset.height = 1; // Used temporarily for max_y
        found->width = x;         // min_x
        found->height = y;        // min_y
        found->area = 0;
        if (prev)
          prev->next = found;
        else
          objects = found;
      }

      // Expand the bounding box as we discover more pixels for this specific
      // piece.
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

  // Alright, we mapped everything. Let's calculate the final width and height
  // for each piece.
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
    if (curr->id ==
        bg_id) // The background is usually the biggest piece, let's skip it.

      continue;
    int match = cfg.a_set ? (curr->area >= (double)cfg.min_area)
                          : (curr->width >= (double)cfg.min_width &&
                             curr->height >= (double)cfg.min_height);
    if (match)
      valid_total++;
  }

  if (valid_total >= 1) {
    int counter = 0;
    int existing_count = 0;
    char **existing_files = (char **)calloc(valid_total, sizeof(char *));

    // First pass: Check which files already exist
    for (curr = objects; curr != NULL; curr = curr->next) {
      if (curr->id == bg_id)
        continue;
      int match = cfg.a_set ? (curr->area >= (double)cfg.min_area)
                            : (curr->width >= (double)cfg.min_width &&
                               curr->height >= (double)cfg.min_height);
      if (!match)
        continue;

      char final_name[2048];
      if (is_dir) {
        char src_copy[1024], *bname;
        strncpy(src_copy, cfg.input_file, 1023);
        bname = basename(src_copy);
        char *dot = strrchr(bname, '.');
        if (dot)
          *dot = '\0';
        snprintf(final_name, sizeof(final_name), "%s%s-%d.png",
                 cfg.output_pattern, bname, cfg.start_at + counter);
      } else if (strstr(cfg.output_pattern, "%d")) {
        snprintf(final_name, sizeof(final_name), cfg.output_pattern,
                 cfg.start_at + counter);
      } else {
        char base[1024];
        strncpy(base, cfg.output_pattern, 1023);
        char *dot = strrchr(base, '.');
        if (dot)
          *dot = '\0';
        snprintf(final_name, sizeof(final_name), "%s-%d.png", base,
                 cfg.start_at + counter);
      }

      if (access(final_name, F_OK) == 0) {
        existing_files[existing_count++] = strdup(final_name);
      }
      counter++;
    }

    if (existing_count > 0 && !cfg.force_overwrite) {
      if (cfg.json_mode) {
        printf("{\n  \"status\": \"error\",\n  \"message\": \"Files already "
               "exist. Use --force to overwrite.\"\n}\n");
        return 1;
      }
      printf("This will overwrite existing images named: ");
      for (int i = 0; i < existing_count; i++) {
        printf("%s%s", existing_files[i], (i + 1 < existing_count) ? ", " : "");
      }
      printf("\nDo you want to continue? [y/N] ");
      char response[10];
      if (fgets(response, sizeof(response), stdin) == NULL ||
          (response[0] != 'y' && response[0] != 'Y')) {
        printf("Aborted.\n");
        for (int i = 0; i < existing_count; i++)
          free(existing_files[i]);
        free(existing_files);
        return 1;
      }
    }
    for (int i = 0; i < existing_count; i++)
      free(existing_files[i]);
    free(existing_files);

    if (cfg.json_mode)
      printf("{\n  \"status\": \"success\",\n  \"src\": \"%s\",\n  \"output\": "
             "[\n",
             cfg.input_file);
    else
      printf("status: success\n");

    counter = 0;
    for (curr = objects; curr != NULL; curr = curr->next) {
      if (curr->id == bg_id)
        continue;
      int match = cfg.a_set ? (curr->area >= (double)cfg.min_area)
                            : (curr->width >= (double)cfg.min_width &&
                               curr->height >= (double)cfg.min_height);
      if (!match)
        continue;

      MagickWand *crop = CloneMagickWand(mw);
      MagickCropImage(crop, (size_t)curr->width, (size_t)curr->height,
                      curr->offset.x, curr->offset.y);

      char final_name[2048];
      if (is_dir) {
        char src_copy[1024], *bname;
        strncpy(src_copy, cfg.input_file, 1023);
        bname = basename(src_copy);
        char *dot = strrchr(bname, '.');
        if (dot)
          *dot = '\0';
        snprintf(final_name, sizeof(final_name), "%s%s-%d.png",
                 cfg.output_pattern, bname, cfg.start_at + counter);
      } else if (strstr(cfg.output_pattern, "%d")) {
        snprintf(final_name, sizeof(final_name), cfg.output_pattern,
                 cfg.start_at + counter);
      } else {
        char base[1024];
        strncpy(base, cfg.output_pattern, 1023);
        char *dot = strrchr(base, '.');
        if (dot)
          *dot = '\0';
        snprintf(final_name, sizeof(final_name), "%s-%d.png", base,
                 cfg.start_at + counter);
      }

      MagickWriteImage(crop, final_name);

      struct stat st;
      double file_kb = 0.0;
      if (stat(final_name, &st) == 0) {
        file_kb = (double)st.st_size / 1024.0;
      }

      if (cfg.json_mode) {
        printf("    { \"filename\": \"%s\", \"dimensions\": [%d, %d], "
               "\"size\": \"%.1fkb\" }%s\n",
               final_name, (int)curr->width, (int)curr->height, file_kb,
               (counter + 1 < valid_total) ? "," : "");
      } else {
        printf("- %s (%d x %d) (%.1fkb)\n", final_name, (int)curr->width,
               (int)curr->height, file_kb);
      }
      DestroyMagickWand(crop);
      counter++;
    }
    status = 0;
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

  // Clean up the linked list so we don't leak memory.
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
