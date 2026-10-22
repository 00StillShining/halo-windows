"""The EP-40's measured shape (docs/redesign/riddim-3d-plan.md section 2), in
reference millimetres: x from the left edge, y from the port edge toward the
player, z above the bottom face. Every number carries its provenance:

  svg-path  measured from TE's drawing path coordinates
  svg-px    read from a rasterised drawing (provisional)
  plan      the plan's provisional value (section 2.1 to 2.3), itself read from
            TE's drawings, not yet re-measured from path coordinates
  estimate  a judgement, to be replaced

frames.py maps reference mm to model metres.
"""

from dataclasses import dataclass


@dataclass(frozen=True)
class Value:
    mm: float
    source: str


# ------------------------------------------------------------------ chassis
WIDTH = Value(176.0, "svg-path")          # 289.0 units x 0.6091 mm
DEPTH = Value(240.0, "svg-path")          # 394.0 units x 0.6091 mm
CORNER_RADIUS = Value(1.0, "plan")        # vertical edges, nearly sharp in plan
TOP_FILLET = Value(0.5, "plan")
# Heights below: measured from the left side drawing's path bounding boxes
# (6903405a..., 0.600 mm per unit; the device is drawn on its side, thickness
# along svg x from 124.0): shell 124.0-127.17, deck 136.83, upper 139.96,
# keys to 140.04 (3.21-3.24 above the deck, small keys included), the step
# at svg y 164.1 (device front edge at 60.1).
SHELL_TOP = Value(3.17, "svg-path")       # dark-green bottom shell (the plan's 3.8 was read off pixels)
SHELL_GROOVE = Value(0.2, "plan")         # V parting groove at its top
DECK_TOP = Value(12.83, "svg-path")       # key deck: a 3.13 mm step below the upper section
UPPER_TOP = Value(15.96, "svg-path")      # port strip, label plate, speaker, screen
STEP_Y = Value(104.0, "svg-path")         # the step wall between screen band and deck

# ------------------------------------------------------------------ keys
CELL = Value(16.0, "plan")                # 16 x 16 apertures at a 23.94 pitch
COLUMN_LEFT = (7.9, 31.7, 55.7, 79.7, 103.6, 127.6, 151.5)   # plan (svg-px)
ROW_TOP = (112.2, 136.3, 160.3, 184.2, 208.2)                # plan (svg-px)
KEY_HEIGHT = Value(3.21, "svg-path")      # above the deck; official travel 3.20 mm
KEY_TOP = Value(15.1, "plan")             # 16.0 base tapering to about 15.1
KEY_CORNER = Value(1.5, "plan")
KEY_FILLET = Value(0.7, "plan")
GAP = Value(0.6, "plan")                  # near-black aperture gap
SHORT_CAP = (15.0, 7.0)                   # plan: upper short cap of a function cell
PLATE = (16.0, 8.0)                       # plan: flat printed plate below it
PLATE_HEIGHT = Value(0.2, "plan")
SMALL_KEY = (15.6, 7.9)                   # plan
SMALL_KEY_X = (8.1, 23.7)                 # plan
SMALL_KEY_Y = {"button_keys": (136.6, 144.5), "fader_mode_button": (152.4, 160.3), "button_shift": (216.4, 224.3)}
SMALL_KEY_HEIGHT = Value(3.21, "svg-path")  # the side view draws them as tall as the pads

# (part, column, row) for full keys, then function cells (cap part, plate part, column, row).
FULL_KEYS = [
    ("group_a", 1, 1), ("pad_7", 2, 1), ("pad_8", 3, 1), ("pad_9", 4, 1),
    ("group_b", 1, 2), ("pad_4", 2, 2), ("pad_5", 3, 2), ("pad_6", 4, 2),
    ("group_c", 1, 3), ("pad_1", 2, 3), ("pad_2", 3, 3), ("pad_3", 4, 3), ("button_minus", 5, 3), ("button_plus", 6, 3),
    ("group_d", 1, 4), ("pad_dot", 2, 4), ("pad_0", 3, 4), ("pad_enter", 4, 4), ("button_record", 5, 4), ("button_play", 6, 4),
]
FUNCTION_CELLS = [
    ("button_sound", "button_sound_secondary", 1, 0), ("button_main", "button_main_secondary", 2, 0),
    ("button_tempo", "button_tempo_secondary", 3, 0), ("button_sample", "button_sample_secondary", 5, 1),
    ("button_timing", "button_timing_secondary", 6, 1), ("button_fx", "button_fx_secondary", 5, 2),
    ("button_erase", "button_erase_secondary", 6, 2),
]

# ------------------------------------------------------------------ knobs
KNOBS = {"knob_volume": (15.8, 120.4), "knob_x": (135.6, 120.4), "knob_y": (159.6, 120.4)}  # plan
KNOB_DISC = (16.0, 2.4)                   # plan: diameter, height
KNOB_STEM = (8.07, 1.8, 9.4)              # svg-path (left view, VOLUME): diameter, from, to above the deck
KNOB_CAP = (8.07, 11.2)                   # svg-path: cap diameter, top above the deck (plan said 11.45)

# ------------------------------------------------------------------ carried over
# Parts not rebuilt yet keep their Blender geometry, moved onto the surface they
# sit on: (old base z in model mm, new surface, z scale). Model z of the old
# plate top was 9.4 mm. Every row is listed; nothing relies on a default.
OLD_PLATE_TOP = 9.4
CARRY_OVER = {
    "display_bezel": ("upper", 0.5), "display_surface": ("upper", 0.5), "brand_panel": ("upper", 0.5),
    "speaker_panel": ("upper", 0.5), "speaker_grille": ("upper", 0.5), "mic_port": ("upper", 0.5),
    "port_output_tab": ("upper", 0.5), "port_input_tab": ("upper", 0.5), "port_sync_tab": ("upper", 0.5),
    "port_midi_tab": ("upper", 0.5), "port_usb_tab": ("upper", 0.5), "port_power_tab": ("upper", 0.5),
    "fader_track": ("deck", 1.0), "fader_cap": ("deck", 1.0),
    "ports_strip": ("none", 1.0), "ports_usb": ("none", 1.0), "port_power_switch": ("none", 1.0),
    "port_output_socket": ("none", 1.0), "port_input_socket": ("none", 1.0), "port_sync_socket": ("none", 1.0),
    "port_midi_socket": ("none", 1.0),
}
# The carried-over screen band reaches 3 mm past the step; it is pulled back to
# end at the step (its back edge, by the label plate, stays put).
SCREEN_FRONT_Y = 104.2
