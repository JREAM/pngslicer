#!/usr/bin/env bats

setup() {
  # BATS_TEST_DIRNAME is automatically provided by BATS and points to the folder containing this test
  BIN="$BATS_TEST_DIRNAME/../pngslicer"
  FIXTURES="$BATS_TEST_DIRNAME/fixtures"
  OUT="$BATS_TEST_DIRNAME/../out/bats-test"

  # Ensure the binary is built
  make -C "$BATS_TEST_DIRNAME/.."

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
