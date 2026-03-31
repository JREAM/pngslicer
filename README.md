<div align="center">
<img alt="PNGSlicer" src="https://github.com/JREAM/pngslicer/blob/main/logo.png?raw=true)" width="50%" />
</div>

**PNGSlicer** is a fast, C-based command-line utility that intelligently analyzes a transparent PNG image and extracts individual, disjointed elements (like sprites or icons) into their own separate image files.

By leveraging ImageMagick's Connected Components labeling, it securely ignores transparent gaps and isolates solid blobs of pixels, automatically cropping and saving them.

![License](https://img.shields.io/badge/license-MIT-blue.svg)
![C](https://img.shields.io/badge/language-C-orange.svg)
[![C/C++ Build and Test](https://github.com/JREAM/pngslicer/actions/workflows/build.yml/badge.svg)](https://github.com/JREAM/pngslicer/actions/workflows/build.yml)

---

## Features

<details>
<summary><strong>⚡ Fast</strong></summary>
Written in C and optimized with OpenMP via ImageMagick for lightning-fast concurrent processing.
</details>

<details>
<summary><strong>📐 Auto-Cropping</strong></summary>
Intelligently isolates and crops individual sprites dynamically based on alpha transparency. No more manually slicing sprite sheets!
</details>

<details>
<summary><strong>🧹 Noise Filtering</strong></summary>
Easily filter out stray pixels or noise using `--min-width`, `--min-height`, or `--min-area` constraints.
</details>

<details>
<summary><strong>🔢 Flexible Numbering</strong></summary>
Supports prefix/suffix numbering through format strings (e.g., `asset_%d.png`) and custom starting indices with `--start-at`.
</details>

<details>
<summary><strong>💾 JSON Support</strong></summary>
Native `--json` output formatted perfectly for programmatic use, automation pipelines, or web APIs.
</details>

<details>
<summary><strong>🛡️ Overwrite Protection</strong></summary>
Prevents accidental data loss by prompting before overwriting existing files, unless silenced with the `--force` flag.
</details>

## Requirements

PNGSlicer is primarily designed for **ImageMagick 6** (`MagickWand`). While the Makefile attempts to find IM7 headers, the core logic relies on ImageMagick 6 data structures. If you are on a system with IM7, it is recommended to install the IM6 compatibility headers.

### Ubuntu / Debian
```sh
sudo apt-get update
sudo apt-get install build-essential pkg-config libmagickwand-dev
```

### Fedora / RHEL / CentOS
```sh
sudo dnf pkgconf-pkg-config ImageMagick-devel
# On some older CentOS systems, use `yum install autoconf ImageMagick-devel`
```

### Alpine Linux
```sh
apk add build-base pkgconf imagemagick-dev
```

### macOS (via Homebrew)
```sh
brew install pkg-config imagemagick@6
# You may need to export PKG_CONFIG_PATH for imagemagick@6
export PKG_CONFIG_PATH="/usr/local/opt/imagemagick@6/lib/pkgconfig"
```

## Build Instructions

A `Makefile` is provided to automatically detect the right bindings using `pkg-config`:

```sh
# Clone the repository
git clone https://github.com/yourusername/pngslicer.git
cd pngslicer

# Build the binary
make clean && make

# (Optional) Install globally
sudo cp pngslicer /usr/local/bin/
```

## Usage

```sh
pngslicer <input.png> [options]
```

### Naming Conventions

Default output directory is **`./pngslicer/`** (`input_basename-1.png`, …). Use **`-o`** for a directory (`-o out/`), a legacy path template (`-o sub/plop_%d.png`, `-o sub/base.png`), or **`-d`** for a directory only (`.`, `./out`, `out/`). Use **`-f` / `--format`** with exactly one **`%d`** for the filename under that directory (e.g. `-d ./export -f '%d-sprite.png'`).

- **Numbered Suffix (Legacy `-o`):** `pngslicer in.png -o asset.png` ➔ `asset-1.png`, `asset-2.png` (in the current directory unless combined with `-d`).
- **With `%d`:** `pngslicer in.png -o %d_asset.png` ➔ `1_asset.png`, `2_asset.png`
- **Directory Output:** `pngslicer in.png -o out/` ➔ `out/in-1.png`, `out/in-2.png`
- **Format:** `pngslicer in.png -d out -f '%d-tile.png'` ➔ `out/1-tile.png`, …

Run **`pngslicer --help`** for full options. **`--overwrite`** (long form only) skips the interactive prompt when files already exist; **`--json`** errors instead of prompting.

### Configuration Flags

| Flag | Description |
| :--- | :--- |
| `-o, --output` | Output directory or legacy file / `%d` path (optional; default dir `./pngslicer/`). |
| `-d`, `--dir` | Directory only (same as `-o` for paths like `.` or `out/`). |
| `-f`, `--format` | Filename under the output dir; must contain one `%d`. |
| `-w, --min-width` | Smallest allowed width for an extracted image (Filters noise/lines). Default is `50`. |
| `-h, --min-height`| Smallest allowed height for an extracted image. Default is `50`. |
| `-a, --min-area` | Smallest allowed absolute area (width * height). Set this instead of w/h to match by payload size. |
| `-s, --start-at` | First index with `--overwrite`; otherwise gap-fill still uses free indices from existing files. Default is `1`. |
| `--overwrite` | Overwrite existing files (no short flag); listing shows `(overwritten)`. |
| `-j, --json` | Changes terminal output to a structured JSON response (useful for pipelines). |

### Example Run

```sh
./pngslicer spritesheet.png -o sprite.png -w 64 -h 64 -j
```

**JSON Output:**
```json
{
  "status": "success",
  "src": "spritesheet.png",
  "output": [
    { "filename": "sprite-1.png", "dimensions": [240, 233], "size": "153.2kb" },
    { "filename": "sprite-2.png", "dimensions": [128, 128], "size": "45.1kb" }
  ]
}
```

## Testing

For a command-line tool like this, **Bash Automated Testing System ([BATS](https://github.com/bats-core/bats-core))** is the industry standard for integration tests.

```sh
bats  tests/extractions.bats

# With Nice output
bats --pretty tests/extractions.bats

# Only run tests with "12" in the name (Useful for Debugging)
bats -f "12" tests/extractions.bats
```



## License

This project is licensed under the [**MIT License**](LICENSE.md).

---

<div align="center">
  <img alt="JREAM" src="https://jream.com/jream.jpg" />

&copy;2016 Jesse Boyer [JREAM]([JREAM](https://jream.com))

</div>

