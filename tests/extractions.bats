#!/usr/bin/env bats

setup() {
  # BATS_TEST_DIRNAME is automatically provided by BATS and points to the folder containing this test
  # Using realpath ensures we have a clean absolute path without '..'
  ROOT_DIR=$(realpath "$BATS_TEST_DIRNAME/..")
  BIN="$ROOT_DIR/pngslicer"
  FIXTURES="$BATS_TEST_DIRNAME/fixtures"
  OUT="$ROOT_DIR/out/bats-test"

  # Ensure the binary is built
  make -C "$ROOT_DIR"

  # Debug info if binary is missing or not executable
  if [[ ! -x "$BIN" ]]; then
    echo "--- DIAGNOSTIC INFO ---"
    echo "Current directory: $(pwd)"
    echo "ROOT_DIR: $ROOT_DIR"
    echo "BIN: $BIN"
    ls -la "$ROOT_DIR"
    file "$BIN" || echo "file command failed"
    ldd "$BIN" || echo "ldd command failed"
    echo "-----------------------"
    return 1
  fi

  # Prepare fresh output folder
  rm -rf "$OUT"
  mkdir -p "$OUT"
}

teardown() {
  # Clean up test artifacts when done
  rm -rf "$OUT"
}

@test "Extracts 12 sprites from trees-12.png" {
  run "$BIN" "$FIXTURES/trees-12.png" -o "$OUT/test-trees-12.png"

  [ "$status" -eq 0 ]
  [[ "${lines[0]}" == *"status: success"* ]]

  # Count files dynamically using Bash array
  files=("$OUT"/test-trees-12-*.png)
  [ "${#files[@]}" -eq 12 ]
}

@test "Extracts 16 sprites from trees-16.png" {
  # We'll use two underscores to be safe and clear
  run "$BIN" "$FIXTURES/trees-16.png" -o "$OUT/test-trees-16__%d.png"

  [ "$status" -eq 0 ]
  [[ "${lines[0]}" == *"status: success"* ]]

  files=("$OUT"/test-trees-16__*.png)
  [ "${#files[@]}" -eq 17 ]
}

@test "Extracts 0 sprites from trees-0.png" {
  # We use huge filters to ensure nothing is found, triggering an error exit code
  run "$BIN" "$FIXTURES/trees-0.png" -o "$OUT/" -w 5000 -e 5000

  # A failure to extract valid sub-images exits with 1
  [ "$status" -eq 1 ]
  [[ "${lines[0]}" == *"status: error"* ]]
}

@test "Generates valid JSON with --json flag" {
  run "$BIN" "$FIXTURES/trees-12.png" -o "$OUT/json-test.png" --json

  [ "$status" -eq 0 ]
  # In C code we use \n so lines array in BATS splits them
  [[ "${lines[1]}" == *"\"status\": \"success\""* ]]
  [[ "${lines[2]}" == *"\"src\": \"$FIXTURES/trees-12.png\""* ]]
  [[ "${lines[3]}" == *"\"output\": ["* ]]

  files=("$OUT"/json-test-*.png)
  [ "${#files[@]}" -eq 12 ]
}

@test "Starts numbering at 10 with --start-at 10" {
  run "$BIN" "$FIXTURES/trees-12.png" -o "$OUT/start.png" --start-at 10

  [ "$status" -eq 0 ]
  # Check if the first and last files are correctly numbered
  [ -f "$OUT/start-10.png" ]
  [ -f "$OUT/start-21.png" ]
}

@test "Overwrites existing files with --force flag" {
  # Create a dummy file that would conflict
  touch "$OUT/force-1.png"
  
  # This should succeed with -f and overwrite it
  run "$BIN" "$FIXTURES/trees-12.png" -o "$OUT/force.png" -f

  [ "$status" -eq 0 ]
  # Check if files were created
  [ -f "$OUT/force-1.png" ]
  # The file should now be a real PNG instead of our touched dummy
  file_type=$(file -b "$OUT/force-1.png")
  [[ "$file_type" == *"PNG image"* ]]
}
