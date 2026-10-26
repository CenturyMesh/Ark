#pragma once
// Studio palette: the 064A "Machined Duotone" feature tree applied to every surface.
// Neutral graphite chrome, white overlays for hover/active/lines, one orange accent.
// The ARK wordmark keeps its own blue; viewport and category colours are separate.
namespace ui_theme {
struct Tone{float r,g,b;};
constexpr Tone chrome{.098f,.102f,.110f};   // header, status bar, opaque side panels
constexpr Tone bar{.122f,.125f,.137f};      // module tab strip
constexpr Tone ribbon{.145f,.149f,.161f};   // command ribbon
constexpr Tone panel{.188f,.196f,.212f};    // floating panels (feature tree, sidebar)
constexpr Tone raised{.231f,.239f,.255f};   // buttons, cards and chips on a panel
constexpr Tone field{.071f,.075f,.082f};    // text fields and wells
constexpr Tone text{.953f,.961f,.969f};
constexpr Tone text2{.784f,.816f,.843f};
constexpr Tone muted{.60f,.64f,.68f};
constexpr Tone faint{.43f,.46f,.50f};
constexpr Tone accent{1.f,.635f,.298f};     // tree selection orange
constexpr Tone accentInk{.102f,.110f,.125f};// text drawn on the accent
constexpr Tone danger{.898f,.325f,.294f};
constexpr float panelAlpha=.50f;            // floating panels over the viewport
constexpr float hover=.07f,active=.12f,line=.10f,border=.16f,select=.20f;
constexpr float panelRadius=14,rowRadius=8,controlRadius=6;
// Win32/GDI surfaces use the same tones.
constexpr COLORREF rgb(Tone t){return RGB((int)(t.r*255+.5f),(int)(t.g*255+.5f),(int)(t.b*255+.5f));}
// An opaque equivalent of a white overlay at `alpha` on `base`.
constexpr Tone over(Tone base,float alpha){return {base.r+(1-base.r)*alpha,base.g+(1-base.g)*alpha,base.b+(1-base.b)*alpha};}
constexpr Tone mix(Tone a,Tone b,float t){return {a.r+(b.r-a.r)*t,a.g+(b.g-a.g)*t,a.b+(b.b-a.b)*t};}
}
