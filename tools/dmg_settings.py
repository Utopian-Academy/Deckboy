# SPDX-License-Identifier: GPL-3.0-or-later
# Evaluated by dmgbuild; no Finder or AppleScript session is used.
from pathlib import Path

app = Path(defines["app"]).resolve()
art = Path(defines["art"]).resolve()
files = [str(app), (defines["readme"], "README.txt")]
symlinks = {"Applications": "/Applications"}
format = "UDZO"
filesystem = "HFS+"
background = str(art / "dmg-background.tiff")
icon = str(app / "Contents/Resources/Deckboy.icns")
window_rect = ((160, 160), (640, 480))
icon_locations = {"Deckboy.app": (170, 234), "Applications": (470, 234), "README.txt": (560, 394)}
icon_size = 96
text_size = 12
show_status_bar = False
show_tab_view = False
show_toolbar = False
show_pathbar = False
show_sidebar = False
default_view = "icon-view"
include_icon_view_settings = True
include_list_view_settings = False
arrange_by = None
