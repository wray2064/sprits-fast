// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 the Sprit's'fast authors
#pragma once

// dither.h — gradients made of pixels, and the pattern that stays put.
//
// A dither here is not a stamp laid over a fill. It is a *value* compared
// against a threshold matrix, choosing between the two ramp stops the value
// falls between. That one mechanism covers both jobs a pixel artist wants:
//
//   Constant density  ->  classic two-tone dithering, a 50% screen
//   Modulated value   ->  a gradient built out of dithered colour
//
// Because the patterns are threshold *matrices* rather than one-bit stamps, the
// same tile works at every density and at every step of a gradient. Twelve are
// built into the engine.
//
// The part with no equivalent in a bitmap editor is anchoring, and it is the
// reason a dithered sprite can move without shimmering:
//
//   Local    the pattern travels and turns with the artwork
//   Global   it travels with the artwork but stays level with the canvas
//   Fixed    it stays where it is, and the artwork moves across it
//
// In an editor that bakes dithering into pixels, rotating a dithered shape
// rotates the screen with it and there is nothing to be done about it. Here the
// choice is a parameter, and switching it recompiles from the same drawing.

#include "app/document.h"
#include "app/paint.h"

#include <string>
#include <vector>

namespace fast {

// Everything a dithered fill is made of, in one place so the interface has one
// thing to edit and one thing to write back.
struct DitherSettings {
    ls::DitherPatternKind pattern = ls::DitherPatternKind::Bayer4;

    // The two ends of the ramp. Two stops is the classic case; the engine takes
    // more, which produces a banded gradient rather than a two-tone one.
    ls::Color from { 40, 40, 60, 255 };
    ls::Color to   { 230, 220, 190, 255 };

    // What drives the value the pattern is compared against.
    ls::DitherModulation modulation = ls::DitherModulation::Linear;

    // Used when the modulation is Constant: a flat density, 0 to 1.
    float density = 0.5f;

    // Linear: the axis. Radial: centre and rim. Angular: centre and reference.
    ls::Vec2f gradientStart { 0.f, 0.f };
    ls::Vec2f gradientEnd   { 16.f, 16.f };

    ls::PatternAnchor anchor = ls::PatternAnchor::Local;
};

// Names for the interface, in enum order, so a combo box is built from the
// engine's own list rather than a hand-kept copy that can drift.
const std::vector<const char*>& ditherPatternNames();
const std::vector<const char*>& ditherModulationNames();
const std::vector<const char*>& patternAnchorNames();

// Turns a layer's fill from solid into dithered, or back, keeping the drawing.
//
// The region is untouched -- only the rule that colours it changes -- so
// switching a finished drawing to a dithered gradient and back again costs
// nothing and loses nothing.
bool setLayerDithered(Document& doc, PaintLayer& layer, const DitherSettings& settings);
bool setLayerSolid(Document& doc, PaintLayer& layer, ls::Color colour);

// True when this layer's fill is a dither.
bool layerIsDithered(Document& doc, const PaintLayer& layer);

// Reads back what a dithered layer is currently set to, so the interface shows
// the layer's own values rather than whatever was last typed.
bool readDitherSettings(Document& doc, const PaintLayer& layer, DitherSettings* out);

// Applies changed settings to a layer that is already dithered. Drives the
// existing operation's parameters rather than replacing it, so dragging a
// density slider leaves one operation and recompiles from the same drawing.
bool applyDitherSettings(Document& doc, const PaintLayer& layer,
                         const DitherSettings& settings);

} // namespace fast
