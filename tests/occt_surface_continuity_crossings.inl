// Synthetic public-API regression: one C0 surface corner can cross both
// continuity grids at once. Nearby distinct events must keep separate knots.
// Validate the ordered union of every returned curve, including both endpoints.
template<class Approximation>
void checkContinuityCrossingApproximation(
    Approximation& approximation,
    const occ::handle<Adaptor3d_Surface>& plane,
    const occ::handle<Adaptor3d_Surface>& surface,
    const occ::handle<Geom_BSplineSurface>& originalSurface,
    const gp_Trsf& placement,
    double vBreak) {
    constexpr double requestedTolerance = 1e-6;
    constexpr double roundoff = 256. * std::numeric_limits<double>::epsilon();
    check(approximation.IsDone(), "continuity crossing approximation completes");
    check(approximation.TolReached3d() <= requestedTolerance &&
          approximation.TolReached2d() <= requestedTolerance,
          "continuity crossing retains requested approximation errors");
    const int curveCount = approximation.NbMultiCurves();
    check(curveCount > 0, "continuity crossing exposes its complete result");
    gp_Pnt2d previousEndUV, lastUV, uEvent, vEvent;
    gp_Pnt previousEnd;
    bool foundU = false, foundV = false;
    const auto inverse = placement.Inverted();
    for (int index = 1; index <= curveCount; ++index) {
        const auto& curve = approximation.Value(index);
        const auto& knots = curve.Knots();
        check(std::isfinite(knots.First()) && std::isfinite(knots.Last()) &&
              knots.Last() > knots.First(), "continuity crossing has a finite positive parameter range");
        gp_Pnt2d firstUV;
        gp_Pnt first, last;
        curve.Value(3, knots.First(), firstUV);
        curve.Value(3, knots.Last(), lastUV);
        curve.Value(1, knots.First(), first);
        curve.Value(1, knots.Last(), last);
        check(index == 1 ? firstUV.Distance(gp_Pnt2d(0., 0.)) <= roundoff :
              firstUV.Distance(previousEndUV) <= roundoff,
              "ordered continuity curves retain the first endpoint and join without UV gaps");
        if (index > 1)
            check(first.Distance(previousEnd) <=
                  roundoff * std::max(1., first.XYZ().Modulus() + previousEnd.XYZ().Modulus()),
                  "ordered continuity curves join without spatial gaps");
        check(lastUV.X() > firstUV.X() && lastUV.Y() > firstUV.Y(),
              "continuity crossing curve cannot be empty or reverse the input order");
        for (int knot = knots.Lower(); knot <= knots.Upper(); ++knot) {
            gp_Pnt2d uv;
            curve.Value(3, knots(knot), uv);
            if (uv.Distance(gp_Pnt2d(.5, .5)) <= roundoff) { foundU = true; uEvent = uv; }
            if (uv.Distance(gp_Pnt2d(vBreak, vBreak)) <= roundoff) { foundV = true; vEvent = uv; }
        }
        gp_Pnt2d previousUV = firstUV;
        for (int span = knots.Lower() + 1; span <= knots.Upper(); ++span) {
            const double begin = knots(span - 1), end = knots(span);
            check(end > begin, "continuity crossings cannot create zero parameter sections");
            for (double fraction : {0., .25, .5, .75, 1.}) {
                const double parameter = begin + (end - begin) * fraction;
                gp_Pnt point;
                gp_Pnt2d planeUV, surfaceUV;
                gp_Vec derivative;
                gp_Vec2d uvDerivative;
                curve.D1(1, parameter, point, derivative);
                curve.Value(2, parameter, planeUV);
                curve.D1(3, parameter, surfaceUV, uvDerivative);
                check(std::isfinite(point.X()) && std::isfinite(point.Y()) &&
                      std::isfinite(point.Z()) && std::isfinite(derivative.Magnitude()),
                      "continuity crossing output and D1 remain finite");
                check(point.Distance(plane->Value(planeUV.X(), planeUV.Y())) <= requestedTolerance &&
                      point.Distance(surface->Value(surfaceUV.X(), surfaceUV.Y())) <= requestedTolerance,
                      "continuity crossing stays on both original supports");
                check(surfaceUV.X() >= -roundoff && surfaceUV.X() <= 1. + roundoff &&
                      surfaceUV.Y() >= -roundoff && surfaceUV.Y() <= 1. + roundoff,
                      "continuity crossing UV remains in the original surface range");
                check(surfaceUV.X() >= previousUV.X() - roundoff &&
                      surfaceUV.Y() >= previousUV.Y() - roundoff,
                      "continuity crossing union stays ordered without overlap");
                previousUV = surfaceUV;
                // At knots D1 is side-dependent; check only each open span.
                if (fraction > 0. && fraction < 1.) {
                    gp_Pnt onSurface;
                    gp_Vec du, dv;
                    originalSurface->D1(surfaceUV.X(), surfaceUV.Y(), onSurface, du, dv);
                    const gp_Vec chain = du * uvDerivative.X() + dv * uvDerivative.Y();
                    point.Transform(inverse);
                    derivative.Transform(inverse);
                    check(point.Distance(onSurface) <= requestedTolerance,
                          "rigid placement retains original support positions");
                    check((derivative - chain).Magnitude() <=
                          roundoff * std::max(1., derivative.Magnitude() + chain.Magnitude()),
                          "continuity crossing D1 retains the original Jacobian under rigid placement");
                }
            }
        }
        previousEndUV = lastUV;
        previousEnd = last;
    }
    check(lastUV.Distance(gp_Pnt2d(1., 1.)) <= roundoff,
          "continuity crossing union retains the final original line endpoint");
    check(foundU && foundV, "both continuity events remain explicit knots in the complete curve union");
    check(vBreak == .5 ? uEvent.Distance(vEvent) <= roundoff : uEvent.Distance(vEvent) > roundoff,
          "only exactly coincident continuity events share a knot");
}

void testMandatorySurfaceContinuityCrossings() {
    struct CrossingCase { double vBreak; bool existingEndpoint; };
    const std::array<CrossingCase, 3> cases{{{.5, false}, {.50000001, false}, {.5, true}}};
    for (const auto& crossing : cases) {
        NCollection_Array2<gp_Pnt> poles(1, 3, 1, 3);
        NCollection_Array1<double> uKnots(1, 3), vKnots(1, 3);
        NCollection_Array1<int> multiplicities(1, 3);
        uKnots(1) = vKnots(1) = 0.;
        uKnots(2) = .5; vKnots(2) = crossing.vBreak;
        uKnots(3) = vKnots(3) = 1.;
        multiplicities(1) = multiplicities(3) = 2; multiplicities(2) = 1;
        for (int u = 1; u <= 3; ++u) for (int v = 1; v <= 3; ++v)
            poles(u, v) = gp_Pnt(uKnots(u), vKnots(v),
                std::abs(uKnots(u) - .5) + std::abs(vKnots(v) - crossing.vBreak));
        occ::handle<Geom_BSplineSurface> geometry = new Geom_BSplineSurface(
            poles, uKnots, vKnots, multiplicities, multiplicities, 1, 1);
        for (bool placed : {false, true}) {
            gp_Trsf placement;
            if (placed) {
                placement.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), .37);
                placement.SetTranslationPart(gp_Vec(13, -7, 5));
            }
            const gp_Pln originalPlane(gp_Pnt(0, 0, 0), gp_Dir(1, -1, 0));
            gp_Pln placedPlane = originalPlane;
            placedPlane.Transform(placement);
            IntSurf_Quadric implicit(placedPlane);
            occ::handle<Adaptor3d_Surface> plane = new GeomAdaptor_Surface(new Geom_Plane(placedPlane));
            occ::handle<Adaptor3d_Surface> surface = new GeomAdaptor_TransformedSurface(geometry, placement);
            occ::handle<IntSurf_LineOn2S> points = new IntSurf_LineOn2S();
            for (double parameter : {0., .25, .5, .75, 1.}) {
                if (parameter == .5 && !crossing.existingEndpoint) continue;
                gp_Pnt point = geometry->Value(parameter, parameter);
                point.Transform(placement);
                double u, v; implicit.Parameters(point, u, v);
                IntSurf_PntOn2S sample;
                sample.SetValue(point, u, v, parameter, parameter);
                points->Add(sample);
            }
            GeomInt_WLApprox geometric;
            geometric.SetParameters(1e-6, 1e-6, 3, 8, 4, 30, true);
            occ::handle<IntPatch_WLine> line = new IntPatch_WLine(points, false);
            geometric.Perform(plane, surface, line, true, true, true, 1, line->NbPnts());
            checkContinuityCrossingApproximation(
                geometric, plane, surface, geometry, placement, crossing.vBreak);

            BRep_Builder builder;
            TopoDS_Face surfaceFace, planeFace;
            builder.MakeFace(surfaceFace, geometry, TopLoc_Location(placement), 1e-7);
            builder.MakeFace(planeFace, new Geom_Plane(originalPlane), TopLoc_Location(placement), 1e-7);
            BRepAdaptor_Surface surfaceAdaptor(surfaceFace, false), planeAdaptor(planeFace, false);
            occ::handle<BRepApprox_ApproxLine> brepLine = new BRepApprox_ApproxLine(points, false);
            BRepApprox_Approx brep;
            brep.SetParameters(1e-6, 1e-6, 3, 8, 4, 30, true);
            brep.Perform(planeAdaptor, surfaceAdaptor, brepLine, true, true, true, 1, brepLine->NbPnts());
            checkContinuityCrossingApproximation(
                brep, plane, surface, geometry, placement, crossing.vBreak);
        }
    }
}
