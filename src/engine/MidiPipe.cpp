/*
    This file is part of Element
    Copyright (C) 2019  Kushview, LLC.  All rights reserved.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/// An array of kv.MidiBuffers.
// Designed for real time performance, therefore does virtually no type
// checking in method calls.
// @classmod el.MidiPipe
// @pragma nostrip

#include <sol/sol.hpp>
#include "element/lua/midi_buffer.hpp"
#include "element/lua/factories.hpp"
#include "engine/MidiPipe.h"

namespace Element {

MidiPipe::MidiPipe()
{ 
    size = 0;
    memset(referencedBuffers, 0, sizeof(MidiBuffer*) * maxReferencedBuffers);
}

MidiPipe::MidiPipe (MidiBuffer** buffers, int numBuffers)
{ 
    jassert (numBuffers < maxReferencedBuffers);
    memset (referencedBuffers, 0, sizeof(MidiBuffer*) * maxReferencedBuffers);
    for (int i = 0; i < numBuffers; ++i)
        referencedBuffers[i] = buffers[i];
    size = numBuffers;
}

MidiPipe::MidiPipe (const OwnedArray<MidiBuffer>& buffers, const Array<int>& channels)
{
    jassert(channels.size() < maxReferencedBuffers);
    memset(referencedBuffers, 0, sizeof(MidiBuffer*) * maxReferencedBuffers);
    for (int i = 0; i < channels.size(); ++i)
        referencedBuffers[i] = buffers.getUnchecked (channels.getUnchecked (i));
    size = channels.size();
}

MidiPipe::~MidiPipe() { }

const MidiBuffer* const MidiPipe::getReadBuffer (const int index) const
{
    jassert (isPositiveAndBelow (index, size));
    return referencedBuffers [index];
}

MidiBuffer* const MidiPipe::getWriteBuffer (const int index) const
{
    jassert (isPositiveAndBelow (index, size));
    return referencedBuffers [index];
}

void MidiPipe::clear()
{
    for (int i = 0; i < maxReferencedBuffers; ++i)
    {
        if (auto* rbuffer = referencedBuffers [i])
            rbuffer->clear();
        else
            break;
    }
}

void MidiPipe::clear (int startSample, int numSamples)
{
    for (int i = 0; i < maxReferencedBuffers; ++i)
    {
        if (auto* rbuffer = referencedBuffers [i])
            rbuffer->clear (startSample, numSamples);
        else
            break;
    }
}

void MidiPipe::clear (int channel, int startSample, int numSamples)
{
    jassert (isPositiveAndBelow (channel, size));
    if (auto* buffer = getWriteBuffer (channel))
        buffer->clear (startSample, numSamples);
}

LuaMidiPipe::LuaMidiPipe() {}
LuaMidiPipe::~LuaMidiPipe()
{
    for (int i = refs.size(); --i >= 0;)
    {
        luaL_unref (state, LUA_REGISTRYINDEX, refs [i]);
        refs.remove (i); buffers.remove (i);
    }

    this->state = nullptr;
}

LuaMidiPipe** LuaMidiPipe::create (lua_State* L, int numReserved)
{
    auto** pipe = element::lua::new_userdata <LuaMidiPipe> (L, "el.MidiPipe");
    (*pipe)->state = L;
    (*pipe)->setSize (numReserved);
    (*pipe)->used = 0;
    return pipe;
}

void LuaMidiPipe::setSize (int newsize)
{
    newsize = jmax (0, newsize);

    while (buffers.size() < newsize)
    {
        buffers.add (element::lua::new_midibuffer (state));
        refs.add (luaL_ref (state, LUA_REGISTRYINDEX));
    }

    used = newsize;
}

const MidiBuffer* const LuaMidiPipe::getReadBuffer (int index) const
{
    return &(**buffers.getUnchecked(index)).buffer;
}

MidiBuffer* LuaMidiPipe::getWriteBuffer (int index) const
{
    return &(**buffers.getUnchecked(index)).buffer;
}

void LuaMidiPipe::swapWith (MidiPipe& pipe)
{
    setSize (pipe.getNumBuffers());
    for (int i = pipe.getNumBuffers(); --i >= 0;)
    {
        pipe.getWriteBuffer(i)->swapWith (
            (*buffers.getUnchecked(i))->buffer
        );
    }
}

//==============================================================================
int LuaMidiPipe::get (lua_State* L)
{
    auto* pipe = *(LuaMidiPipe**) lua_touserdata (L, 1);
    lua_rawgeti (L, LUA_REGISTRYINDEX, pipe->refs.getUnchecked (
        static_cast<int> (lua_tointeger (L, 2) - 1)));
    return 1;
}

int LuaMidiPipe::resize (lua_State* L)
{
    auto* pipe = *(LuaMidiPipe**) lua_touserdata (L, 1);
    pipe->setSize (static_cast<int> (lua_tointeger (L, 2)));
    return 0;
}

int LuaMidiPipe::size (lua_State* L)
{
    auto* pipe = *(LuaMidiPipe**) lua_touserdata (L, 1);
    lua_pushinteger (L, pipe->getNumBuffers());
    return 1;
}

int LuaMidiPipe::clear (lua_State* L)
{
    auto* pipe = *(LuaMidiPipe**) lua_touserdata (L, 1);
    for (int i = pipe->buffers.size(); --i >= 0;)
        (**pipe->buffers.getUnchecked(i)).buffer.clear();
    return 0;
}

}

static int midipipe_new (lua_State* L)
{
    lua_Integer nbufs = (lua_gettop (L) > 1 && lua_isinteger (L, 2))
        ? jmax (lua_Integer(), lua_tointeger (L, 2)) : 0;
    Element::LuaMidiPipe::create (L, static_cast<int> (nbufs));
    return 1;
}

static int midipipe_gc (lua_State* L) {
    auto** pipe = (Element::LuaMidiPipe**) lua_touserdata (L, 1);
    if (nullptr != *pipe)
        deleteAndZero (*pipe);
    return 0;
}

/// Create a new MIDI Pipe.
// @int nbuffers Number of buffers
// @return A new MIDI Pipe
// @within Constructors
// @function MidiPipe.new

static const luaL_Reg methods[] = {
    { "__gc",       midipipe_gc },

    /// Get a MidiBuffer from the pipe.
    // @int index Index of the buffer
    // @treturn kv.MidiBuffer A midi buffer
    // @within Methods
    // @function MidiPipe:get
    { "get",        Element::LuaMidiPipe::get },

    /// Get a MidiBuffer from the pipe.
    // @int nbuffers New number of buffers to store
    // @within Methods
    // @function MidiPipe:resize
    { "resize",     Element::LuaMidiPipe::resize },
    
    /// Returns the number of buffers in this pipe.
    // @treturn int The number of buffers
    // @within Methods
    // @function MidiPipe:size
    { "size",       Element::LuaMidiPipe::size },

    /// Clears all buffers in the pipe.
    // Note this doesn't remove buffers, it just clears their contents.
    // @within Methods
    // @function MidiPipe:clear
    { "clear",      Element::LuaMidiPipe::clear },

    { nullptr, nullptr }
};

#include <element/element.h>
extern "C" EL_API
int luaopen_el_MidiPipe (lua_State* L)
{
    if (luaL_newmetatable (L, "el.MidiPipe")) {
        lua_pushvalue (L, -1);               /* duplicate the metatable */
        lua_setfield (L, -2, "__index");     /* mt.__index = mt */
        luaL_setfuncs (L, methods, 0);
        lua_pop (L, 1);
    }

    if (luaL_newmetatable (L, "el.MidiPipeClass")) {
        lua_pushcfunction (L, midipipe_new);   /* push audio_new function */
        lua_setfield (L, -2, "__call");     /* mt.__call = audio_new */
        lua_pop (L, 1);
    }

    lua_newtable (L);
    luaL_setmetatable (L, "el.MidiPipeClass");
    lua_pushcfunction (L, midipipe_new);
    lua_setfield (L, -2, "new");
    return 1;
}
