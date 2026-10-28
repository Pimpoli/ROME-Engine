"""Original, consistent 24 px ribbon artwork for the native ROME editor."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / "editor/icons"
ICONS = {
    "Select": '<path d="m5 3 15 8-7 2-3 8z" fill="#c9cbd5"/>',
    "Move": '<path d="M12 2v20M2 12h20M9 5l3-3 3 3M9 19l3 3 3-3M5 9l-3 3 3 3M19 9l3 3-3 3"/>',
    "Rotate": '<path d="M19 8a8 8 0 1 0 1 7M19 3v5h-5"/><circle cx="12" cy="12" r="2" fill="#a8a0e8"/>',
    "Scale": '<path d="M3 12v9h9v-9zM14 3h7v7M21 3l-9 9"/>',
    "Transform": '<path d="m5 18 2-11h10l2 11zM7 7l-2-4M17 7l2-4"/><rect x="3" y="16" width="4" height="4" fill="#9497a7"/><rect x="17" y="16" width="4" height="4" fill="#9497a7"/><rect x="5" y="3" width="4" height="4" fill="#9497a7"/><rect x="15" y="3" width="4" height="4" fill="#9497a7"/>',
    "Part": '<path d="m12 2 9 5v10l-9 5-9-5V7z" fill="#63589d"/><path d="m3 7 9 5 9-5M12 12v10"/>',
    "Character": '<path d="M4 20v-5l3-6 5 2 5-2 3 6v5h-6v-5h-4v5z" fill="#42a398"/><circle cx="12" cy="5" r="3" fill="#e4d5ad"/>',
    "Terrain": '<path d="m2 17 6-10 5 4 3-6 6 12v4H2z" fill="#459f91"/><path d="m6 10 2-3 5 4 3-6 3 6"/>',
    "Gui": '<rect x="2" y="4" width="20" height="16" rx="1" fill="#b75591"/><path d="M2 9h20M7 13h4M7 16h9"/><path d="M5 6.5h.1M8 6.5h.1"/>',
    "Script": '<path d="M5 2h11l3 3v17H5z" fill="#8ea840"/><path d="M16 2v4h3M8 10h8M8 13h6M8 16h8"/>',
    "Import": '<path d="m12 2 9 5v10l-9 5-9-5V7zM3 7l9 5 9-5M12 12v10"/><path d="M12 8V2M9 5l3 3 3-3" fill="#9d96db"/>',
    "Material": '<circle cx="12" cy="12" r="9"/><path d="M6 5c-1 7 6 14 13 13M4 9l3 1M9 18l1 2M15 5l3 2"/>',
    "Color": '<path d="M12 3a9 9 0 1 0 0 18h2a3 3 0 0 0 0-6h-1a2 2 0 0 1 0-4h3c5 0 4-8-4-8z"/><circle cx="7" cy="10" r="1" fill="#f29d5d"/><circle cx="11" cy="7" r="1" fill="#dd7dba"/><circle cx="7" cy="15" r="1" fill="#76b59a"/>',
    "Group": '<rect x="3" y="3" width="7" height="7"/><rect x="14" y="14" width="7" height="7"/><path d="M14 3h7v7M3 14v7h7" stroke-dasharray="2 2"/>',
    "Lock": '<rect x="5" y="10" width="14" height="11" rx="1" fill="#777b8e"/><path d="M8 10V7a4 4 0 0 1 8 0v3M12 14v3"/>',
    "Anchor": '<circle cx="12" cy="5" r="3"/><path d="M12 8v13M8 11h8M3 14a9 9 0 0 0 18 0M3 14l-1 4M3 14l4 1M21 14l1 4M21 14l-4 1"/>',
    "Explorer": '<rect x="2" y="2" width="20" height="20" rx="1"/><path d="M8 3v18M3 8h18M11 12h7M11 16h7"/><path d="M5 5h.1M5 12h.1M5 16h.1"/>',
    "Properties": '<rect x="3" y="2" width="18" height="20" rx="1"/><path d="M7 7h.1M10 7h7M7 12h.1M10 12h7M7 17h.1M10 17h7"/>',
    "Assets": '<path d="M2 7V4h7l3 3h10v14H2z"/><path d="M2 10h20"/>',
    "Output": '<rect x="2" y="3" width="20" height="18" rx="1"/><path d="m6 8 4 4-4 4M13 16h5"/>',
    "HomeView": '<path d="m3 11 9-8 9 8M5 10v11h14V10M10 21v-7h4v7"/>',
    "Play": '<path d="m7 3 14 9-14 9z" fill="#5277ff" stroke="#5277ff"/>',
    "Stop": '<rect x="5" y="5" width="14" height="14" fill="#db626c" stroke="#db626c"/>',
    "Save": '<path d="M3 2h15l3 3v17H3zM7 2v7h10V2M7 22v-9h10v9"/>',
}

ICONS.update({
    "Geometry": '<path d="m12 2 9 5v10l-9 5-9-5V7zM3 7l9 5 9-5M12 12v10"/>',
    "Toolbox": '<path d="M3 6h18v15H3zM8 6V3h8v3M8 10v7M16 10v7M5 13h14"/>',
    "AvatarSettings": '<circle cx="10" cy="5" r="3" fill="#e89427"/><path d="M5 21v-7l-3 3M15 11l2 4M10 9v7l-4 6M10 16l5 6" stroke="#e89427"/><circle cx="19" cy="18" r="3"/>',
    "Rig": '<circle cx="12" cy="4" r="2"/><path d="m5 9 7-3 7 3-5 5 2 7M10 14l-2 7M6 10l-3 6" stroke="#e89427"/>',
    "Accessory": '<path d="m5 7 7-4 7 4v10l-7 4-7-4z" stroke-dasharray="2 2"/><circle cx="12" cy="12" r="4" fill="#e89427"/>',
    "Animation": '<path d="M3 5h18v16H3zM3 10h18M7 2v6M17 2v6M7 14h3M14 14h3M7 18h3" stroke="#e89427"/>',
    "Validate": '<rect x="3" y="4" width="16" height="14"/><path d="M3 8h16m-9 7 3 3 7-8" stroke="#e89427"/>',
    "Clips": '<path d="M3 5h7v14H3zM14 8h7v8h-7zM10 12h4" stroke="#e89427"/>',
    "AvatarEditor": '<path d="M4 3h6v18H4zM14 3h6v18h-6M10 8h4M10 16h4" stroke="#e89427"/>',
    "Frame": '<rect x="3" y="3" width="18" height="18" fill="#654452"/><path d="M3 8V3h5M16 3h5v5M21 16v5h-5M8 21H3v-5"/>',
    "Label": '<rect x="2" y="4" width="20" height="16" fill="#8b526d"/><path d="m4 17 5-6 4 4 3-3 5 5"/><circle cx="16" cy="8" r="2"/>',
    "Input": '<rect x="2" y="5" width="20" height="14" fill="#8b526d"/><path d="M6 9h8M6 14h5M16 8v8"/>',
    "Appearance": '<path d="M3 3h18v18H3zM7 7h10v10H7z" fill="#8b526d"/>',
    "Layout": '<rect x="2" y="4" width="20" height="16"/><path d="M7 4v16M17 4v16M11 8h2M11 12h2M11 16h2" stroke="#b16d91"/>',
    "Constraint": '<path d="M2 5h20v14H2zM6 9h12M6 15h12" stroke="#b16d91"/>',
    "UiEditor": '<path d="M5 2h11l4 4v16H5zM16 2v5h4M8 11h8M8 15h8M8 18h5"/>',
    "Back": '<path d="m15 4-8 8 8 8"/>',
    "Forward": '<path d="m9 4 8 8-8 8"/>',
    "Format": '<path d="M3 4h13M3 9h18M3 14h13M3 19h18"/><path d="M19 2v6" stroke="#9fb84c"/>',
    "Search": '<circle cx="10" cy="10" r="7"/><path d="m15 15 7 7" stroke="#9fb84c"/>',
    "Goto": '<path d="M7 5h10v14H7M3 12h10m-3-3 3 3-3 3"/>',
    "Command": '<rect x="2" y="4" width="20" height="16"/><path d="m6 8 4 4-4 4M13 16h5" stroke="#9fb84c"/>',
    "Breakpoint": '<rect x="4" y="3" width="16" height="18"/><path d="M9 8v8M15 8v8" stroke="#9fb84c"/>',
    "Stack": '<path d="M3 7h18v14H3zM6 3h13v4M6 11h12M6 15h12" stroke="#9fb84c"/>',
    "Watch": '<path d="M2 12s4-8 10-8 10 8 10 8-4 8-10 8S2 12 2 12z"/><circle cx="12" cy="12" r="4"/>',
    "Analysis": '<rect x="2" y="3" width="20" height="18"/><path d="M7 17v-5M12 17V8M17 17V5" stroke="#9fb84c"/>',
    "Activity": '<rect x="2" y="3" width="20" height="18"/><path d="m4 15 4-5 4 6 4-8 4 3" stroke="#9fb84c"/>',
    "Pivot": '<circle cx="12" cy="12" r="7"/><path d="M12 1v6M12 17v6M1 12h6M17 12h6"/>',
    "Reset": '<path d="M19 8a8 8 0 1 0 1 7M19 3v5h-5"/><circle cx="12" cy="12" r="3"/>',
    "Effect": '<path d="m12 2-2 7-7-2 6 5-6 5 7-2 2 7 2-7 7 2-6-5 6-5-7 2z" stroke="#a37cdb"/>',
    "Connection": '<path d="M4 7h7v7H4zM13 10h7v7h-7M10 11h4" stroke="#a37cdb"/>',
    "Constraint3D": '<path d="M3 6h7v12H3zM14 6h7v12h-7M7 10l10 4M7 14l10-4" stroke="#a37cdb"/>',
    "Weld": '<path d="M2 5h8v14H2zM14 5h8v14h-8M9 9h6M9 15h6" stroke="#a37cdb"/>',
    "Texture": '<rect x="3" y="3" width="18" height="18"/><path d="m3 14 11-11M3 21 21 3M10 21l11-11"/>',
    "Align": '<path d="M2 21h20M5 5h5v12H5zM14 10h5v7h-5z"/>',
    "Union": '<path d="M2 5h12v14H2zM10 2h12v14H10z"/>',
    "Intersect": '<path d="M2 5h12v14H2zM10 2h12v14H10z"/><path d="M10 5h4v11h-4z" fill="#89909c"/>',
    "Separate": '<path d="M2 5h8v14H2zM14 2h8v14h-8M10 12h4"/>',
    "Subtract": '<path d="M2 5h12v14H2zM10 2h12v14H10z" stroke-dasharray="2 2"/>',
    "Plugins": '<path d="M5 3h4v6h6V3h4v6h3v5h-6v7H8v-7H2V9h3z"/>',
    "Luau": '<path d="M4 2h16v20H4z" fill="#4265a2"/><circle cx="12" cy="12" r="6"/><circle cx="15" cy="8" r="1"/>',
})

for name, drawing in ICONS.items():
    (ROOT / f"Studio{name}.svg").write_text(
        '<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24">'
        '<g fill="none" stroke="#dfe1eb" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round">'
        + drawing + '</g></svg>\n', encoding="utf-8")
print(f"Generated {len(ICONS)} native Studio icons")
