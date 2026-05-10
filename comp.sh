#!/usr/bin/env bash
set -euo pipefail

# 1. Find OSS-BIN (behold din eksisterende logik her)
if [ -d "$HOME/Dokumenter/oss-cad-suite/bin" ]; then
  OSS_BIN="$HOME/Dokumenter/oss-cad-suite/bin"
elif [ -d "$HOME/Dokumenter/oss-cad-suite/bin" ]; then
  OSS_BIN="$HOME/Dokumenter/oss-cad-suite/bin"
else
  echo "FEJL: Kan ikke finde oss-cad-suite"
  exit 1
fi

# 2. DEFINER VARIABLER HER (Vigtigt: Ingen mellemrum omkring '=')
TOP="main"
PCF="pins.pcf"
SRC="main.v \
		8bitparalle.v"

YOSYS="$OSS_BIN/yosys"
NEXTPNR="$OSS_BIN/nextpnr-ice40"
ICEPACK="$OSS_BIN/icepack"
ICETIME="$OSS_BIN/icetime"
ICEPROG="$OSS_BIN/iceprog"

# Check that tools exist
for tool in "$YOSYS" "$NEXTPNR" "$ICEPACK" "$ICEPROG"; do
    if [ ! -x "$tool" ]; then
        echo "ERROR: Cannot find $tool"
        exit 1
    fi
done

# Settings for Seed Sweep
NUM_SEEDS=256
RESULT_FILE="seed_results.txt"

# Variables to keep track of the winner
BEST_SEED=0
BEST_FREQ="0.00"

echo "=== Synthesizing with Yosys ==="
"$YOSYS" -p "read_verilog $SRC; synth_ice40 -abc9 -top $TOP -json $TOP.json"

echo "=== Starting Place & Route Seed Sweep ($NUM_SEEDS runs) ==="
# Prepare the text file
echo "Seed Sweep Results for $TOP (Target: 100 MHz)" > "$RESULT_FILE"
echo "---------------------------------------------------------" >> "$RESULT_FILE"

for i in $(seq 1 $NUM_SEEDS); do
  echo -n "Running nextpnr with seed $i... "
  
  # Run nextpnr
  OUTPUT=$("$NEXTPNR" --hx8k --package cb132 \
    --json "$TOP.json" \
    --pcf "$PCF" \
    --asc "${TOP}_${i}.asc" \
    --sdc pins.sdc \
    --freq 100 \
    --seed $i 2>&1 | grep "Max frequency" | tail -n 1 || true)
  
  # Extract the frequency number
  FREQ=$(echo "$OUTPUT" | awk '{print $7}')
  
  if [ -n "$FREQ" ]; then
      echo "Result: $FREQ MHz"
      echo "seed $i = $FREQ MHz" >> "$RESULT_FILE"
      
      # Compare floating point numbers with awk: Returns 1 if FREQ > BEST_FREQ
      IS_NEW_BEST=$(awk -v current="$FREQ" -v best="$BEST_FREQ" 'BEGIN { if (current > best) print 1; else print 0 }')
      
      if [ "$IS_NEW_BEST" -eq 1 ]; then
          BEST_FREQ=$FREQ
          BEST_SEED=$i
      fi
  else
      echo "Result: Routing failed"
      echo "seed $i = Failed" >> "$RESULT_FILE"
  fi
done

echo ""
echo "=== Sweep finished! ==="
echo ""
echo "========================================="
echo "🏆 WINNING SEED FOUND!"
echo "   Seed number  : $BEST_SEED"
echo "   Max frequency: $BEST_FREQ MHz"
echo "========================================="
echo ""

if [ "$BEST_SEED" -gt 0 ]; then
    echo "=== Packing and Flashing the Winner ==="
    # Pack the winning ASC file into a BIN file
    "$ICEPACK" "${TOP}_${BEST_SEED}.asc" "$TOP.bin"
    
    # Upload to the FPGA
   "$ICEPROG" "$TOP.bin"

    echo ""
    echo "=== Cleaning up ==="
    echo "Deleting all temporary .asc files to save space..."
    rm -f *.asc
    
    echo "Cleanup complete! The results log '$RESULT_FILE' and '$TOP.bin' have been saved."
else
    echo "ERROR: No successful seeds found. Cannot flash."
fi
