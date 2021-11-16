/// Bounding box
// The value type for this is a 32bit integer and Backed by a juce::Rectangle.
// API is identical to @{el.Rectangle}.
// @classmod el.Bounds
// @pragma nostrip

#include <element/element.h>
#include <element/lua/rectangle.hpp>
#include "lua_helpers.hpp"

#define LKV_TYPE_NAME_BOUNDS "Bounds"

using namespace juce;

extern "C" EL_API
int luaopen_el_Bounds (lua_State* L) {
    using B = Rectangle<int>;
    
    auto M = element::lua::new_rectangle<int> (L, LKV_TYPE_NAME_BOUNDS,
        sol::meta_method::to_string, [](B& self) {
            return element::lua::to_string (self, LKV_TYPE_NAME_BOUNDS);
        }
    );

    sol::stack::push (L, M);
    return 1;
}
