/* ===========================================================================
 *  ARK-Point :: ark/api.h — stable C ABI over the kernel.
 *  Consumable from Python/ctypes, Rust, C#, Node FFI, and the MCP bridge.
 *  All functions are thread-hostile per-document (single-threaded use).
 * =========================================================================*/
#ifndef ARK_API_H
#define ARK_API_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ArkDoc ArkDoc;

/* lifecycle ------------------------------------------------------------- */
ArkDoc* ark_doc_create(void);
void    ark_doc_destroy(ArkDoc*);

/* parameters (expressions like "width/2") --------------------------------- */
int      ark_param_set(ArkDoc*, const char* name, const char* expr);
double   ark_param_get(ArkDoc*, const char* name);
int      ark_param_remove(ArkDoc*, const char* name);

/* features -----------------------------------------------------------------
 * Each returns new feature id (>0) or 0 on failure.                          */
uint64_t ark_add_box      (ArkDoc*, double dx,double dy,double dz);
uint64_t ark_add_cylinder (ArkDoc*, double r,double h);
uint64_t ark_add_sphere   (ArkDoc*, double r);
uint64_t ark_add_cone     (ArkDoc*, double rb,double rt,double h);
uint64_t ark_add_torus    (ArkDoc*, double R,double r);
uint64_t ark_add_extrude  (ArkDoc*, const char* shape, /*rect,circle,polygon*/
                                    double w, double d, double r,
                                    int    sides,
                                    double height);
uint64_t ark_add_revolve  (ArkDoc*, const char* shape,
                                    double w, double d, double r,
                                    int    sides,
                                    double angleDeg);
uint64_t ark_add_transform(ArkDoc*, uint64_t input,
                                    double tx,double ty,double tz,
                                    double rxDeg,double ryDeg,double rzDeg);
uint64_t ark_add_boolean  (ArkDoc*, uint64_t a, uint64_t b, int op /*0 union,1 cut,2 intersect*/);
uint64_t ark_add_pattern  (ArkDoc*, uint64_t input, double dx,double dy,double dz, int count);

/* Generic feature creation by type name (box|cylinder|sphere|cone|torus|
 * extrude|revolve|transform|boolean|pattern). Set expression params via
 * ark_feature_set_param / inputs via a boolean/pattern input id convention,
 * then call ark_recompute. Returns new id or 0 on unknown type.
 * For boolean features encode inputs as: input1 = (a<<32)|b, op via param "op". */
uint64_t ark_feature_add  (ArkDoc*, const char* typeName);

int      ark_feature_remove(ArkDoc*, uint64_t id);
int      ark_feature_move  (ArkDoc*, uint64_t id, int delta);
/* Edit one expression param of an existing feature (keeps its id/history).
 * Returns 0 ok, -1 bad args, 1 feature not found. */
int      ark_feature_set_param(ArkDoc*, uint64_t id, const char* key, const char* expr);

/* sketches ------------------------------------------------------------------
 * JSON shape: {"entities":[{"kind":"point|line|circle","dof":[..]}...],
 *              "constraints":[{"type":"fixed","a":0,"val":x,"val2":y}...]}
 * ark_sketch_set stores the sketch and recomputes; returns 0 ok.
 * ark_sketch_json returns malloc'd UTF-8 (free via ark_free) or nullptr. */
int      ark_sketch_set (ArkDoc*, const char* name,
                          const char* entitiesJson, const char* constraintsJson /*may be null*/);
char*    ark_sketch_json(ArkDoc*, const char* name);
int      ark_sketch_remove(ArkDoc*, const char* name);

/* evaluation ---------------------------------------------------------------- */
int      ark_recompute(ArkDoc*);            /* 0 ok, !=0 errors present */

/* geometry queries ------------------------------------------------------------ */
int      ark_body_count(ArkDoc*);
uint32_t ark_body_tris (ArkDoc*, int index);

/* export ----------------------------------------------------------------------- */
int      ark_export_stl(ArkDoc*, const char* path, int bodyIndex /* -1=all merged */);
int      ark_export_obj(ArkDoc*, const char* path, int bodyIndex);
int      ark_render_png(ArkDoc*, const char* path, int w,int h);  /* offscreen */

/* persistence -------------------------------------------------------------------- */
/* Full-fidelity .arkjson document (params + sketches + feature history). */
int      ark_doc_save(ArkDoc*, const char* path);               /* 0 ok */
int      ark_doc_load(ArkDoc*, const char* path);               /* 0 ok, replaces content */

/* introspection ------------------------------------------------------------------ */
char*    ark_doc_json(ArkDoc*);             /* malloc'd UTF-8; free via ark_free */
void     ark_free(char*);

/* expression helper ---------------------------------------------------------------- */
double   ark_eval(const char* expr);        /* standalone math eval */

#ifdef __cplusplus
}
#endif
#endif /* ARK_API_H */
