# CAD and mesh exchange

023B adds general exact STEP/STP and IGES/IGS import through `import_cad` and
Studio file drop. The user chooses **Just import** or **Arkitfy using agents**.
Source bytes and transferred geometry remain embedded, portable and immutable;
reconstruction uses separate editable features and measured comparison. Read
[the import contract](CAD_IMPORT_023B.md) for supported units, mixed geometry,
bounded package capacity and explicit equivalence limits. Specialized tube
`import_step` and sheet `import_sheet_step` contracts remain available.

The visible **Export** control offers STL, OBJ, FBX, STEP and IGES.

`export_mesh{format:"obj"|"fbx"|"stl",path,id?,node?,copy?}` exports the visible
scene or the requested part/instance. Scene placements are baked into a mesh.
OBJ writes normals and a millimetre unit comment; OBJ itself has no standard
unit metadata. FBX is ASCII 7.4 and declares millimetres through its unit scale.
Neither format preserves feature history, exact surfaces, appearance textures
or an editable assembly hierarchy. Keep the `.ark` as the design authority.

`export_iges{id,path,pose?}` writes exact native geometry via OCCT's B-rep IGES
mode. It reads the file back and checks bounds and area, and separately reports
whether solid topology survived. `pose` defaults to `formed`; `flat` is valid
for sheets. [The OCCT writer contract](https://occt3d.com/dev/doc/refman/html/class_i_g_e_s_control___writer.html)
distinguishes face mode from B-rep mode. `export_step` retains its existing exact
STEP and round-trip validation behavior. Exact exchange currently selects a
feature in definition coordinates; it does not export the whole scene as a CAD
assembly.

The integrated vehicle's OBJ and FBX were read by the independent
[ufbx loader](https://github.com/ufbx/ufbx), pinned for local validation at
`5955c5c0b042ac2dc6f32955ab206aaec0620cfc`. Both returned 47,574 vertices,
78,534 triangles and normals; FBX reported 0.001 metres per unit. The loader
was compiled as C++ only for this local readback check and is not a runtime
dependency. OCCT independently read back the exported chassis IGES.
