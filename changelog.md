# v0.9.0
- Learned from 273 levels (second level pack added; 23 million objects), sorted into 11 deco types. Nine Circles uses the AREDL "Circles" tag; AREDL version tags (1.9/2.0/2.1/2.2) are searchable.
- New Options tab: toggle body fill, edge glow, outlines, spike/saw glow, particles, background, beat pulses, color shift;
  adjust glow size, glow opacity, body opacity, hue shift, brightness, pulse spacing, color mode (style / keep mine / mono), theme.
  Options are remembered and the preview updates right away.
- Color shift: the glow changes color mid-structure and back, timed like real levels (fast, ~0.15s).
- Body and glow channel opacity are applied to the color channels.

# v0.8.0
- Existing decoration, triggers and no-touch objects in the selection are skipped (they were being decorated as if they were blocks).
- Clean rules: each block gets one body fill (behind it) + one glow; each open edge one glow or outline hugging the edge; spikes/saws one glow.
- Only real deco pieces are used (glows, outlines, lines, particles, rings, chains; 3D pieces only in 3D styles).
- Colors by role: dark body, bright blending glow, light lines, glow-colored particles.
- At most 16 beat pulses. Background scenery off by default. AI button is round again.

# v0.7.0
- Decoration is placed in order: every open side of the same hitbox gets the same pieces, stacked in the source level's layer order (no more random picks).
- Styles are now community deco types learned from the pack: Glow, Classic, Hell, 3D, Particles, Effect, Retro, Minimal, Nine Circles, Art, Modern.
- Each press of Use/Go moves to the next most typical level of that type (or the next best match for your description).
- Beat Pulse triggers on the glow color when applying, timed like the source level.
- Background pieces are placed in neat grid-snapped rows. Glow opacity setting.

# v0.6.0
- Description box is back: it mixes every level's decorations, weighted by your words (colors, glow, chains, 3d, level names...).
- Decoration is sorted by hitbox (block, platform, slope, spike, ground spike, saw...) instead of object ID.
- Fixed "no rules for the objects you selected".
- Random style button, hitbox summary in the header, remembers your last description and style.

# v0.5.0
- Styles learned from 139 levels, Triggers tab.

# v0.4.0
- Fully offline: removed Gemini/API key options.
- Round AI button, style buttons moved to the bottom.

# v0.3.0
- Offline style decorator: Neon, Lava, Ruins, Ice, Nature, Minimal. No AI or internet needed.
- New themed UI with preview, before/after and theme picker.

# v0.2.0
- Switched from the Claude API to Google Gemini so the mod can be used for free.

# v0.1.0
- First version: AI button in the editor, prompt popup, Claude-generated decoration placed as one undo step.
