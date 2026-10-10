# ARK Studio icon atlases

## Windows application icon

`ark-app-mark.png` is the original transparent blue A/chevron derived from the ARK
wordmark with built-in ImageGen. Its silhouette and proportions are unchanged.
`ark-app.ico` uses the full available width (no added one-pixel border), at 16, 20, 24, 32,
40, 48, 64, 128 and 256 pixels, retaining alpha and its aspect ratio. Regenerate
the ICO with `powershell -File scripts/build-app-icon.ps1`.

Studio embeds resource 101 through `src/view/app_icon.rc` for Explorer,
shortcuts and Task Manager, and loads large/small window icons from that same
resource for the taskbar and Alt+Tab. No opaque tile or background is added.
The source prompt and final transparency instruction are in
`docs/design/ark-app-icon.prompt.txt`.

## Ribbon atlases

The 2026-10-10 audit added `ark-detail-atlas-v1.png`, generated with built-in
ImageGen using the first atlas only as a style reference. Its 4 x 4 cells are:
circle, edit sketch, normal view, sheet base; hem, emboss, hole pattern, drive
relation; motion, select body, select face, select edge; select curve,
parameters, import, redo. The complete prompt is in
`docs/design/ark-detail-atlas.prompt.txt`. The bitmap is preserved unchanged.
Rectangle/circle, spline/edit, flange/hem, cut/emboss/pattern and mate/motion
now have distinct symbols. New document, export, graphics, loads, live MCP,
model tools, tools and More use dedicated code glyphs instead of unrelated
atlas cells. Same-operation aliases (for example creating/editing an emboss)
intentionally share a symbol. CMake copies all three atlases.

Generated on 2026-10-09 using the built-in ImageGen tool. These are original
RGBA raster assets, copied unchanged into this repository. Both actual outputs
are 1254 x 1254 pixels with transparent corners; the tool selected the output
size despite the nominal prompt size. Studio samples a normalized 4 x 4 grid.
The second atlas used the first as a style reference. No external runtime
service or API key is needed to display the assets.

- `ark-tool-atlas-v1.png`: plane, sketch, extrude, cut; loft, sweep, flange,
  tube; assembly, mate, material, center of gravity; save, open, undo, compute.
- `ark-domain-atlas-v1.png`: spline, NURBS, fill, thicken; PCB, cable, frame,
  suspension; constraint, mirror, transform, chamfer; formed, flat, unfold, stock.

CMake copies both files beside Studio under `assets/ui`. The OpenGL ribbon and
native Win32 menu bitmaps use the same atlases. Labels remain live application
text. The original code glyphs remain available for commands outside the atlas.

## Primary atlas prompt

Use case: UI icon asset atlas for ARK Studio, a precise professional native CAD application. Create one production bitmap PNG sprite atlas, square 1024x1024 pixels, transparent background. Exactly 4 columns by 4 rows, equal square cells with icons centered at each cell center, consistent visual size occupying 60 percent of each cell, generous empty padding, no dividers, no labels, no text, no letters, no watermarks. Sixteen distinct coherent engineering toolbar icons, left to right row by row: row 1 construction datum plane with axes; rectangular sketch with four control nodes; extrusion of a rectangle into a solid with upward arrow; subtractive cylindrical cutting tool through a block. Row 2 loft between two different profiles; profile sweeping along curved path; sheet metal right angle flange; bent hollow tube. Row 3 assembled three mechanical blocks; two coaxial mating rings with alignment mark; material sample cube with three finish facets; center of gravity crosshair inside a wireframe solid with downward gravity arrow. Row 4 save disk; open project folder; undo curved arrow; compute processor chip with small parallel lanes. Style: crisp restrained technical icon design, clean geometric silhouettes, luminous pale slate strokes and icy cyan accent surfaces, subtle isometric dimensionality, no photorealism, no excessive detail, extremely legible when drawn at 28 to 36 pixels on dark graphite UI. Keep every cell independent and precisely aligned. Actual transparent alpha outside each icon.

## Domain atlas prompt

Use case: UI icon asset atlas for ARK Studio CAD. Create a NEW companion sprite atlas; the supplied image is STYLE REFERENCE ONLY, do not reproduce its subjects. Square PNG with genuinely transparent background, exactly 4 columns and 4 rows, equal square cells, each icon centered in its own cell, consistent scale occupying 65 percent of cell, generous padding, no dividers, no text, no labels, no letters, no watermark. Match the reference's clean isometric CAD icon style, pale slate edges, icy cyan accent faces, restrained metallic depth, high legibility at 32 pixels; simplify small details. Sixteen distinct subjects left-to-right row-by-row: row 1 a curved Bezier spline with four visible control nodes; a curved NURBS surface with control lattice; a closed wavy boundary filled by one thin surface patch; one curved sheet gaining visible thickness. Row 2 a printed circuit board with a few pads; a routed electrical cable with connector; a triangulated welded tubular structural frame; a double-wishbone suspension with wheel and spring. Row 3 a dimensional geometric constraint between two sketch points; two mirrored solid blocks across a datum plane; a solid with a circular rotation arrow; a cube with one bevelled chamfer highlighted cyan. Row 4 a folded sheet metal bracket; the same bracket as a flat blank; a sheet flange unfolding with curved arrow; a pair of straight hollow tube stock lengths. Keep each icon strictly within its equal grid cell. No drop shadow or glow outside the icon, clean transparent alpha.
