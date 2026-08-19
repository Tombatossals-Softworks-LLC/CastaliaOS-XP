# Castalia theme bundle schema (`theme.conf`)

Every theme lives in `themes/<id>/theme.conf` — a **TOML** file, human-editable
with comments (Bible P5), validated in CI by
`tools/castalia_qa/theme_lint.py`. One bundle drives *everything* coherently:
Qt/QSS, GTK, Openbox decorations, icons, the greeter, and sounds (§6.16).

## Sections & keys (all required unless noted)

### `[meta]`
| Key | Type | Meaning |
|-----|------|---------|
| `name` | string | Display name ("Castalia Classic") |
| `id` | string | Must equal the directory name |
| `version` | string | Bundle version |
| `author` | string | Author/credit |
| `high_contrast` | bool (optional, default false) | Raises text-contrast minimum from 4.5:1 to 7:1 |

### `[colors]` — all values `#RRGGBB`
| Key | Role |
|-----|------|
| `accent` | The theme's accent (buttons, focus, links) |
| `titlebar_top` / `titlebar_bottom` | Active titlebar gradient stops |
| `titlebar_text` | Active titlebar text |
| `titlebar_inactive_top` / `titlebar_inactive_bottom` | Inactive gradient stops |
| `titlebar_inactive_text` | Inactive titlebar text |
| `surface` / `surface_alt` | Window background / alternate rows, wells |
| `text` / `text_secondary` | Body text / secondary labels |
| `selection_bg` / `selection_text` | Selection highlight pair |
| `border` | Control and window borders |

### `[metrics]` — integers, on the 4 px design grid (§8.1)
| Key | Range | Meaning |
|-----|-------|---------|
| `base_unit` | must be `4` | The design grid unit |
| `titlebar_height` | 20–32 | Titlebar height (px) |
| `corner_radius` | 0–4 | Control corner radius (px) |
| `panel_height` | 24–36 | Taskbar height at ≥1024×768 |
| `panel_height_800` | 24–32 | Taskbar height at 800×600 |

### `[fonts]`
| Key | Meaning |
|-----|---------|
| `ui` | UI font stack (libre/original only — §3.9) |
| `mono` | Monospace stack |

### `[assets]` — optional
| Key | Meaning |
|-----|---------|
| `wallpaper` | Repo-relative desktop wallpaper source (optional). Themes without it keep the default `branding/wallpapers/azure-bay.svg`. Every referenced asset must have a row in `legal/ASSET_PROVENANCE.csv`. |

## Enforced design rules (fail CI — Bible §8)

1. **16-bit-safe gradients:** each titlebar gradient's two stops differ by
   ≤ **0.12 relative luminance** (band-free on 16-bit display modes).
2. **Contrast:** `text` vs `surface` ≥ **4.5:1** (≥ **7:1** if
   `high_contrast`); `selection_text` vs `selection_bg` likewise;
   `titlebar_text` ≥ **4.5:1** vs the gradient *midpoint* and ≥ **3:1** vs
   each stop; `text_secondary` and inactive titlebar text ≥ **3:1**.
3. **Grid:** metrics within range; `base_unit` fixed at 4.

Rationale for the midpoint rule: title text sits across the whole bar; the
midpoint is the honest reference, with the 3:1 per-stop floor guarding the
extremes.
