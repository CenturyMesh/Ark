# Testing 025B viewport controls

Milestones: A.01/A.03, B.01/B.05. This preview extends 024B; its existing
authoring and agent protocols remain applicable. Drawings remain opt-in.

Run the isolated Windows preview from PowerShell:

```powershell
& 'C:\Sw_templates\Ark-024B\out\test-preview\025B-test1\ark-studio.exe'
```

The bottom-centre viewport dock has a 75% transparent background, with opaque
labels and independently highlighted active icons. Check it over geometry and
over empty viewport space, with the Assistant both open and closed.

1. Click **Planes** to hide all construction planes/datums. Click again to show
   all, including datums previously hidden individually. Geometry is unchanged.
2. Toggle **Sketches**, **Grid** and **Edges** independently. **Fit** frames the
   document. Controls and the gaps between them consume clicks over the model.
3. Select a body and click **2D drawing**. Its existing sheet opens in the same
   viewport. If none exists, the empty state offers **New sheet**; opening this
   view never creates one automatically.
4. Select a container row and repeat. The sheet source is that container.
   Selecting a body or a model face afterwards clears the container context.
5. **Parts** / **Assemblies** opens the complete drawing library. **Design / 3D**
   or Escape returns to modelling. Finish an active edit before using the dock's
   drawing shortcut.

The dock is hidden while viewing a drawing, during busy/tool-gallery overlays,
or when the remaining viewport is too small for the controls. Drawing source
support and exact-measurement validation are unchanged.

Local automated evidence is recorded in [verification-025B.json](verification-025B.json).
The PR remains a draft until user testing; local tests are not manual acceptance.
