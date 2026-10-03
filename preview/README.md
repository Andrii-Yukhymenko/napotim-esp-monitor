# Screen design preview

The conversation preview renders a 240x240 Canvas using bitmap glyphs exported
from the actual U8g2 fonts. It mirrors the firmware's baseline coordinates,
RGB565 colors, title/category clipping, priority markers and deadline alignment.
Both compact layouts have six rows. The revised proposal uniformly scales the
original 6x13 title font by 1.15 in both axes using nearest-neighbor rasterization,
with unchanged 6x12 metadata. This preserves the original glyph proportions;
firmware 1.0.3 implements this with a 576-byte monochrome title strip rather than
switching to 7x13.
Checkboxes are always gray and
high-priority bars are red in both proposals. All sample tasks are synthetic.
The revised palette uses a darker saturated purple (#7c2bdf), saturated red
(#ff2424), and stronger category dots, while neutral text remains unchanged.

Regenerate the glyph atlas after changing fonts:

```bash
python3 scripts/make-preview-fonts.py
```

The editable preview fragment is saved in the task's visualization directory:
`C:/Users/Admin/.codex/visualizations/2026/10/03/01a100ab-7a83-7132-b47e-8110cb62da20/napotim-screen.html`.
It embeds the font atlas and needs no server or account key. The standalone
browser wrapper was used for visual checks. The inline host provides variant
selection and optional design controls.

Visual checks covered both layouts, overflow, long Ukrainian titles and 1x/2x
scaling. The chosen larger design has been translated into native drawing
functions, compiled and installed on the LCD. Monitor pixel density and LCD
brightness are not simulated. Category-dot hues come from the account's category
colors; the firmware removes their pastel white component to increase saturation.

After the LCD review, firmware 1.0.6 replaces fractional title scaling with the
native Misc Fixed 7x14 size. The conversation fragment above still records the
older scaling proposal. `native-title-check.png` checks the new native glyphs
using synthetic titles; the vendored BDF and export script are documented in
`fonts/README.md`. Header and metadata keep the original fonts.
