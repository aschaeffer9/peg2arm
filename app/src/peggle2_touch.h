// Touch controls for the Android build (peggle2_touch.cpp).
//
// In a level:
//   drag            the cannon follows the finger (along the ball's arc, so the shot passes under the finger)
//   tap             fire, along the current aim (a tap does not move the aim)
//   two fingers     hold to speed the ball up (the game's X)
//   back            pause (Start)
// In menus and on the map:
//   tap             presses the button under the finger (the game's own pointer path), otherwise A
//   swipe sideways  the map's previous / next area (left / right)
//   back            B
//
// Pinch-to-zoom: Peggle 2 has no player zoom (its camera zooms by itself near the last orange peg and in Extreme
// Fever), so there is no pinch gesture.
//
// The aim, pointer and tutorial logic follows peggle2-mac's game hooks (MIT, Copyright (c) macprotips,
// https://github.com/macprotips/peggle2-mac, runtime/game_hooks.cpp): the same game functions and offsets, with the
// mouse replaced by touches.

#pragma once

#include <functional>
#include <memory>

namespace rex::system {
class IInputSystem;
}

namespace peggle2::touch {

using InputFactory = std::function<std::unique_ptr<rex::system::IInputSystem>(bool tool_mode)>;

// Returns a factory that builds what `base` builds plus the touch pad (a synthetic pad merged into player 1).
InputFactory Wrap(InputFactory base);

}  // namespace peggle2::touch
