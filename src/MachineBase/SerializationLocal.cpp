// -*- Mode: C++; tab-width:2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
// vi:tw=80:et:ts=2:sts=2
//
// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2007 Elliot Glaysher
//  
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//  
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//  
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
//  
// -----------------------------------------------------------------------



// include headers that implement a archive in simple text format
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/serialization/split_free.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/date_time/posix_time/time_serialize.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/fstream.hpp>

#include "Utilities.h"

#include "MachineBase/RLMachine.hpp"
#include "MachineBase/StackFrame.hpp"
#include "MachineBase/Memory.hpp"
#include "MachineBase/SaveGameHeader.hpp"
#include "MachineBase/Serialization.hpp"
#include "algoplus.hpp"

#include "Systems/Base/System.hpp"
#include "Systems/Base/GraphicsSystem.hpp"
#include "Systems/Base/GraphicsStackFrame.hpp"
#include "Systems/Base/EventSystem.hpp"
#include "Systems/Base/TextSystem.hpp"
#include "libReallive/intmemref.h"
#include "libReallive/archive.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <boost/bind.hpp>

using namespace std;
using namespace libReallive;
using namespace boost::archive;
namespace fs = boost::filesystem;

// -----------------------------------------------------------------------

namespace {

/**
 * Pointer to the machine that is having its data
 * serialized. boost::serialization doesn't allow passing something
 * like a closure around, which is frustrating because many pieces of
 * data rely on looking things up on the machine.
 *
 * @warning We're using what is essentially a piece of static data
 *          here; this is a likely location for errors
 */
RLMachine* g_currentMachine = NULL;

}  // close annonymous namespace

// -----------------------------------------------------------------------

namespace boost {
namespace serialization {

// -----------------------------------------------------------------------
// GraphicsStackFrame
// -----------------------------------------------------------------------
template<class Archive>
void serialize(Archive& ar, GraphicsStackFrame& f, unsigned int version)
{
  ar & f.m_commandName & f.m_hasFilename & f.m_fileName & f.m_hasSourceDC 
    & f.m_sourceDC & f.m_hasSourceCoordinates & f.m_sourceX & f.m_sourceY 
    & f.m_sourceX2 & f.m_sourceY2 & f.m_hasTargetDC & f.m_targetDC 
    & f.m_hasTargetCoordinates & f.m_targetX & f.m_targetY & f.m_targetX2 
    & f.m_targetY2 & f.m_hasRGB & f.m_r & f.m_g & f.m_b & f.m_hasOpacity 
    & f.m_opacity & f.m_hasMask & f.m_mask;
}

// -----------------------------------------------------------------------
// System
// -----------------------------------------------------------------------
template<class Archive>
void serialize(Archive& ar, System& sys, unsigned int version)
{
  // For now, does nothing
}

// -----------------------------------------------------------------------
// GraphicsSystem
// -----------------------------------------------------------------------
template<class Archive>
void serialize(Archive& ar, GraphicsSystem& sys, unsigned int version)
{
  ar & sys.graphicsStack();
}

// -----------------------------------------------------------------------
// SaveGameHeader
// -----------------------------------------------------------------------
template<class Archive>
void serialize(Archive& ar, SaveGameHeader& header, unsigned int version)
{
  ar & header.title & header.saveTime;
}

// -----------------------------------------------------------------------
// GlobalMemory
// -----------------------------------------------------------------------
template<class Archive>
inline void serialize(Archive & ar, LocalMemory& memory, unsigned int version)
{
  ar & memory.intA & memory.intB & memory.intC & memory.intD & memory.intE
    & memory.intF & memory.strS & memory.intL & memory.strK;
}

// -----------------------------------------------------------------------
// StackFrame
// -----------------------------------------------------------------------
template<class Archive>
void save(Archive & ar, const StackFrame& frame, unsigned int version)
{
  cerr << "Frame: " << frame << endl;
  int sceneNumber = frame.scenario()->sceneNumber();
  int position = distance(frame.scenario()->begin(), frame.ip);
  ar << sceneNumber << position << frame.frameType;
}

// -----------------------------------------------------------------------

template<class Archive>
void load(Archive & ar, StackFrame& frame, unsigned int version)
{
  int sceneNumber, offset;
  StackFrame::FrameType type;
  ar >> sceneNumber >> offset >> type;

  libReallive::Scenario const* scenario = 
    g_currentMachine->archive().scenario(sceneNumber);
  if(scenario == NULL)
  {
    ostringstream oss;
    oss << "Unknown SEEN #" << sceneNumber << " in save file!";
    throw rlvm::Exception(oss.str());
  }

  if(offset > distance(scenario->begin(), scenario->end()) || offset < 0)
  {
    ostringstream oss;
    oss << offset << " is an illegal bytecode offset for SEEN #" 
        << sceneNumber << " in save file!";
    throw rlvm::Exception(oss.str());
  }

  Scenario::const_iterator positionIt = scenario->begin();
  advance(positionIt, offset);

  frame = StackFrame(scenario, positionIt, type);
}

// -----------------------------------------------------------------------
// RLMachine
// -----------------------------------------------------------------------
template<class Archive>
void save(Archive & ar, const RLMachine& machine, unsigned int version)
{
  int lineNum = machine.lineNumber();
  ar & lineNum;

  // Copy all elements of the stack up to the first LongOperation.
  vector<StackFrame> prunedStack;
  copy_until(machine.callStack.begin(), machine.callStack.end(),
             back_inserter(prunedStack),
             bind(&StackFrame::frameType, _1) == StackFrame::TYPE_LONGOP);
  
  for_each(prunedStack.begin(), prunedStack.end(),
           bind(&StackFrame::setSaveGameAsIP, _1));

  ar & prunedStack;
}

// -----------------------------------------------------------------------

template<class Archive>
void load(Archive & ar, RLMachine& machine, unsigned int version)
{
  cerr << "Loading RLMachine!" << endl;
  ar >> machine.m_line;

  // Just thaw the callStack; all preprocessing was done at freeze
  // time.
  machine.callStack.clear();
  ar >> machine.callStack;
}

// -----------------------------------------------------------------------

} // namespace serialization
} // namespace boost

// -----------------------------------------------------------------------

BOOST_SERIALIZATION_SPLIT_FREE(RLMachine);
BOOST_SERIALIZATION_SPLIT_FREE(StackFrame);

// -----------------------------------------------------------------------

namespace {

template<typename TYPE>
void checkInFileOpened(TYPE& file, const fs::path& home)
{
  if(!file)
  {
    ostringstream oss;
    oss << "Could not open save game file " << home.string();
    throw rlvm::Exception(oss.str());
  }
}

}

// -----------------------------------------------------------------------

namespace Serialization {

// -----------------------------------------------------------------------

void saveGameForSlot(RLMachine& machine, int slot)
{
  fs::path path = buildSaveGameFilename(machine, slot);
  fs::ofstream file(path);
  checkInFileOpened(file, path);
  return saveGameTo(file, machine);
}

// -----------------------------------------------------------------------

void saveGameTo(std::ostream& oss, RLMachine& machine)
{
  const SaveGameHeader header(machine.system().graphics().windowSubtitle());

  g_currentMachine = &machine;

  text_oarchive oa(oss);
//  System& sys = machine.system();
  oa << header
     << const_cast<const LocalMemory&>(machine.memory().local())
     << const_cast<const RLMachine&>(machine)
     << const_cast<const System&>(machine.system());

  g_currentMachine = NULL;
}

// -----------------------------------------------------------------------

fs::path buildSaveGameFilename(RLMachine& machine, int slot)
{
  ostringstream oss;
  oss << "save" << setw(3) << setfill('0') << slot << ".sav";

  return machine.system().gameSaveDirectory() / oss.str();
}

// -----------------------------------------------------------------------

SaveGameHeader loadHeaderForSlot(RLMachine& machine, int slot)
{
  fs::path path = buildSaveGameFilename(machine, slot);
  fs::ifstream file(path);
  checkInFileOpened(file, path);
  return loadHeaderFrom(file);
}

// -----------------------------------------------------------------------

SaveGameHeader loadHeaderFrom(std::istream& iss)
{
  SaveGameHeader header;

  // Only load the header
  text_iarchive ia(iss);
  ia >> header;

  return header;
}

// -----------------------------------------------------------------------

void loadLocalMemoryForSlot(RLMachine& machine, int slot, Memory& memory)
{
  fs::path path = buildSaveGameFilename(machine, slot);
  fs::ifstream file(path);
  checkInFileOpened(file, path);
  loadLocalMemoryFrom(file, memory);
} 

// -----------------------------------------------------------------------

void loadLocalMemoryFrom(std::istream& iss, Memory& memory)
{
  SaveGameHeader header;

  // Only load the header
  text_iarchive ia(iss);
  ia >> header
     >> memory.local();
}

// -----------------------------------------------------------------------

void loadGameForSlot(RLMachine& machine, int slot)
{
  fs::path path = buildSaveGameFilename(machine, slot);
  fs::ifstream file(path);
  checkInFileOpened(file, path);
  loadGameFrom(file, machine);
} 

// -----------------------------------------------------------------------

void loadGameFrom(std::istream& iss, RLMachine& machine)
{
  SaveGameHeader header;

  g_currentMachine = &machine;
  {
    machine.system().reset();

    // Only load the header
    text_iarchive ia(iss);
    ia >> header
       >> machine.memory().local()
       >> machine
       >> machine.system();

    machine.system().graphics().replayGraphicsStack(machine);

    machine.system().graphics().markScreenForRefresh();
  }
  g_currentMachine = NULL;
}


}
