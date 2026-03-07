#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="${ROOT_DIR}/data/demos"

mkdir -p "${OUT_DIR}"

line() {
  local first="$1"
  shift || true
  printf '%s' "${first}"
  for field in "$@"; do
    printf '\t%s' "${field}"
  done
  printf '\n'
}

begin_show() {
  local file="$1"
  local title="$2"
  {
    line title "${title}"
    line focused_deck 0
    line focused_output 0
    line focused_group 0
    line layer_names BG LayerA LayerB LayerC LayerD
    line advanced_mode 1
    line ui_sounds 1
    line ui_transitions 1
    line jump_mode trigger
    line jump_transition 1
    line panic_profile outputs_off
    line panic_fade_seconds 0.9
    line panic_auto_restore 0
    line master_volume 1
    line master_dimmer 1
    line output_follow_display 0
    line output_render_width 1920
    line output_render_height 1080
    line output_refresh_hz 0
    line output_bit_depth 0
    line output_canvas_enabled 0
    line output_canvas_width 3840
    line output_canvas_height 2160
    line output_target 0 "Program Out" 0 0 0 0 srt "srt://127.0.0.1:9000?mode=caller&transtype=live&streamid=output1" 6000 window -1 out-demo-main
  } > "${file}"
}

append_layer_assignment() {
  local file="$1"
  local deck_index="$2"
  local layer_index="$3"
  line layer_assignment "${deck_index}" 0 "${layer_index}" 1 out-demo-main "lay-demo-${deck_index}-${layer_index}" >> "${file}"
}

append_group_preset() {
  local file="$1"
  local preset_index="$2"
  local name="$3"
  line group_preset "${preset_index}" "${name}" >> "${file}"
}

append_group_slot() {
  local file="$1"
  local preset_index="$2"
  local deck_index="$3"
  local bypass="$4"
  local cue_id="$5"
  line group_slot "${preset_index}" "${deck_index}" "${bypass}" "${cue_id}" >> "${file}"
}

append_deck() {
  local file="$1"
  local deck_index="$2"
  local name="$3"
  local selected_index="${4:-0}"
  local active_index="${5:--1}"
  line deck \
    "${deck_index}" "${name}" "${selected_index}" "${active_index}" \
    0 0 "" 0 0 "Deckboy - ${name}" 0 0 crossfade 0 0 1 30 0 0 0 "Deckboy - ${name} Key" \
    >> "${file}"
}

append_pattern_cue() {
  local file="$1"
  local deck_index="$2"
  local cue_id="$3"
  local cue_number="$4"
  local cue_name="$5"
  local pattern_type="$6"
  local scale_x="$7"
  local scale_y="$8"
  local offset_x="$9"
  local offset_y="${10}"
  local notes="${11:-}"

  line cue \
    "${deck_index}" "pattern://${pattern_type}" "${cue_name}" pattern \
    0 1920 1080 30 generated "" "" 0 0 "#306230" \
    0 0 0 0 "${cue_id}" 0 0 -1 inherit -1 \
    "" "" "" 180 0 0 1 "" "${notes}" \
    "${scale_x}" "${scale_y}" 0 "${offset_x}" "${offset_y}" "${cue_number}" "" \
    0 0 0 0 0 0 "#00ff00" 60 20 1 1 1 0 \
    >> "${file}"
}

generate_demo_70_30_4pip_bg_5deck() {
  local file="${OUT_DIR}/demo_70_30_4pip_bg_5deck.playboy"
  begin_show "${file}" "Demo - 70/30 + 4 PiP Over BG (5 Decks)"

  append_layer_assignment "${file}" 0 0
  append_layer_assignment "${file}" 1 1
  append_layer_assignment "${file}" 2 2
  append_layer_assignment "${file}" 3 3
  append_layer_assignment "${file}" 4 4

  append_group_preset "${file}" 0 "Open - BG + 4 PiP"
  append_group_slot "${file}" 0 0 0 cue-demo-7030-bg
  append_group_slot "${file}" 0 1 0 cue-demo-7030-p1
  append_group_slot "${file}" 0 2 0 cue-demo-7030-p2
  append_group_slot "${file}" 0 3 0 cue-demo-7030-p3
  append_group_slot "${file}" 0 4 0 cue-demo-7030-p4

  append_group_preset "${file}" 1 "BG Only"
  append_group_slot "${file}" 1 0 0 cue-demo-7030-bg
  append_group_slot "${file}" 1 1 1 ""
  append_group_slot "${file}" 1 2 1 ""
  append_group_slot "${file}" 1 3 1 ""
  append_group_slot "${file}" 1 4 1 ""

  append_group_preset "${file}" 2 "PiP Motion Sweep"
  append_group_slot "${file}" 2 0 0 cue-demo-7030-bg
  append_group_slot "${file}" 2 1 0 cue-demo-7030-p1b
  append_group_slot "${file}" 2 2 0 cue-demo-7030-p2b
  append_group_slot "${file}" 2 3 0 cue-demo-7030-p3b
  append_group_slot "${file}" 2 4 0 cue-demo-7030-p4b

  append_deck "${file}" 0 "Deck 1 BG"
  append_pattern_cue "${file}" 0 cue-demo-7030-bg BG "BG Pocket Day" pocket-day 1 1 0 0 "Animated background layer."

  append_deck "${file}" 1 "Deck 2 PiP-1"
  append_pattern_cue "${file}" 1 cue-demo-7030-p1 P1 "PiP 1 Bars" smpte-bars 0.22 0.22 670 -360 "Upper-right PiP."
  append_pattern_cue "${file}" 1 cue-demo-7030-p1b P1M "PiP 1 Bars Motion" smpte-bars-motion 0.22 0.22 670 -360 "Upper-right PiP motion alt."

  append_deck "${file}" 2 "Deck 3 PiP-2"
  append_pattern_cue "${file}" 2 cue-demo-7030-p2 P2 "PiP 2 Crosshatch" crosshatch 0.22 0.22 670 -120 "Mid-upper-right PiP."
  append_pattern_cue "${file}" 2 cue-demo-7030-p2b P2M "PiP 2 Crosshatch Motion" crosshatch-motion 0.22 0.22 670 -120 "Mid-upper-right PiP motion alt."

  append_deck "${file}" 3 "Deck 4 PiP-3"
  append_pattern_cue "${file}" 3 cue-demo-7030-p3 P3 "PiP 3 Checkerboard" checkerboard 0.22 0.22 670 120 "Mid-lower-right PiP."
  append_pattern_cue "${file}" 3 cue-demo-7030-p3b P3M "PiP 3 Checkerboard Motion" checkerboard-motion 0.22 0.22 670 120 "Mid-lower-right PiP motion alt."

  append_deck "${file}" 4 "Deck 5 PiP-4"
  append_pattern_cue "${file}" 4 cue-demo-7030-p4 P4 "PiP 4 Full Blue" full-blue 0.22 0.22 670 360 "Lower-right PiP."
  append_pattern_cue "${file}" 4 cue-demo-7030-p4b P4M "PiP 4 Full Blue Motion" full-blue-motion 0.22 0.22 670 360 "Lower-right PiP motion alt."
}

generate_demo_quad_2x2_5deck() {
  local file="${OUT_DIR}/demo_quad_2x2_4pip_bg_5deck.playboy"
  begin_show "${file}" "Demo - 2x2 PiP Quad Over BG (5 Decks)"

  append_layer_assignment "${file}" 0 0
  append_layer_assignment "${file}" 1 1
  append_layer_assignment "${file}" 2 2
  append_layer_assignment "${file}" 3 3
  append_layer_assignment "${file}" 4 4

  append_group_preset "${file}" 0 "Quad Open"
  append_group_slot "${file}" 0 0 0 cue-demo-quad-bg
  append_group_slot "${file}" 0 1 0 cue-demo-quad-a
  append_group_slot "${file}" 0 2 0 cue-demo-quad-b
  append_group_slot "${file}" 0 3 0 cue-demo-quad-c
  append_group_slot "${file}" 0 4 0 cue-demo-quad-d

  append_group_preset "${file}" 1 "Quad Motion"
  append_group_slot "${file}" 1 0 0 cue-demo-quad-bg
  append_group_slot "${file}" 1 1 0 cue-demo-quad-am
  append_group_slot "${file}" 1 2 0 cue-demo-quad-bm
  append_group_slot "${file}" 1 3 0 cue-demo-quad-cm
  append_group_slot "${file}" 1 4 0 cue-demo-quad-dm

  append_deck "${file}" 0 "Deck 1 BG"
  append_pattern_cue "${file}" 0 cue-demo-quad-bg BG "BG Pocket Sunset" pocket-sunset 1 1 0 0 "Animated background layer."

  append_deck "${file}" 1 "Deck 2 PiP-A"
  append_pattern_cue "${file}" 1 cue-demo-quad-a A1 "Quad A" smpte-bars 0.40 0.40 -430 -250 "Top-left PiP."
  append_pattern_cue "${file}" 1 cue-demo-quad-am A1M "Quad A Motion" smpte-bars-motion 0.40 0.40 -430 -250 "Top-left PiP motion alt."

  append_deck "${file}" 2 "Deck 3 PiP-B"
  append_pattern_cue "${file}" 2 cue-demo-quad-b B1 "Quad B" crosshatch 0.40 0.40 430 -250 "Top-right PiP."
  append_pattern_cue "${file}" 2 cue-demo-quad-bm B1M "Quad B Motion" crosshatch-motion 0.40 0.40 430 -250 "Top-right PiP motion alt."

  append_deck "${file}" 3 "Deck 4 PiP-C"
  append_pattern_cue "${file}" 3 cue-demo-quad-c C1 "Quad C" checkerboard 0.40 0.40 -430 250 "Bottom-left PiP."
  append_pattern_cue "${file}" 3 cue-demo-quad-cm C1M "Quad C Motion" checkerboard-motion 0.40 0.40 -430 250 "Bottom-left PiP motion alt."

  append_deck "${file}" 4 "Deck 5 PiP-D"
  append_pattern_cue "${file}" 4 cue-demo-quad-d D1 "Quad D" full-green 0.40 0.40 430 250 "Bottom-right PiP."
  append_pattern_cue "${file}" 4 cue-demo-quad-dm D1M "Quad D Motion" full-green-motion 0.40 0.40 430 250 "Bottom-right PiP motion alt."
}

generate_demo_program_preview_clean_3deck() {
  local file="${OUT_DIR}/demo_program_preview_clean_3deck.playboy"
  begin_show "${file}" "Demo - Program + Preview + Bug (3 Decks)"

  append_layer_assignment "${file}" 0 0
  append_layer_assignment "${file}" 1 1
  append_layer_assignment "${file}" 2 2

  append_group_preset "${file}" 0 "Program + Preview + Bug"
  append_group_slot "${file}" 0 0 0 cue-demo-pp-bg
  append_group_slot "${file}" 0 1 0 cue-demo-pp-preview
  append_group_slot "${file}" 0 2 0 cue-demo-pp-bug

  append_group_preset "${file}" 1 "Program + Bug"
  append_group_slot "${file}" 1 0 0 cue-demo-pp-bg
  append_group_slot "${file}" 1 1 1 ""
  append_group_slot "${file}" 1 2 0 cue-demo-pp-bug

  append_group_preset "${file}" 2 "Program Clean"
  append_group_slot "${file}" 2 0 0 cue-demo-pp-bg
  append_group_slot "${file}" 2 1 1 ""
  append_group_slot "${file}" 2 2 1 ""

  append_deck "${file}" 0 "Deck 1 Program"
  append_pattern_cue "${file}" 0 cue-demo-pp-bg PGM "Program BG Pocket Night" pocket-night 1 1 0 0 "Main background feed."

  append_deck "${file}" 1 "Deck 2 Preview"
  append_pattern_cue "${file}" 1 cue-demo-pp-preview PRV "Preview Window" full-white-motion 0.34 0.34 560 -280 "Top-right preview PiP."

  append_deck "${file}" 2 "Deck 3 Bug"
  append_pattern_cue "${file}" 2 cue-demo-pp-bug BUG "Corner Bug" full-red 0.16 0.16 -810 430 "Bottom-left corner bug."
}

generate_demo_70_30_4pip_bg_5deck
generate_demo_quad_2x2_5deck
generate_demo_program_preview_clean_3deck

printf 'Generated demo shows in %s\n' "${OUT_DIR}"
ls -1 "${OUT_DIR}"/*.playboy
