// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2006 Elliot Glaysher
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
//
// The parts that were stolen from xclannad are:
//
// Copyright (c) 2004-2006  Kazunori "jagarl" Ueno
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
// 3. The name of the author may not be used to endorse or promote products
//    derived from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
// IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
// OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
// IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
// INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
// NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
// THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// -----------------------------------------------------------------------

#include "Precompiled.hpp"

// -----------------------------------------------------------------------

/**
 * @file   RLMachine.cpp
 * @author Elliot Glaysher
 * @date   Sat Oct  7 10:54:19 2006
 * 
 * @brief  Implementation of the main RLMachine class
 * 
 */

#include "libReallive/archive.h"
#include "libReallive/bytecode.h"
#include "libReallive/scenario.h"
#include "libReallive/expression.h"

#include "MachineBase/RLMachine.hpp"
#include "MachineBase/RLModule.hpp"
#include "MachineBase/RLOperation.hpp"
#include "MachineBase/LongOperation.hpp"

// Some RLMachines will cary around a copy of the Null system.
#include "Systems/Null/NullSystem.hpp"
#include "Systems/Null/NullGraphicsSystem.hpp"
#include "Systems/Base/SystemError.hpp"

#include "Systems/Base/TextSystem.hpp"
#include "Systems/Base/TextPage.hpp"

#include "Modules/cp932toUnicode.hpp"

#include "Modules/TextoutLongOperation.hpp"

#include <sstream>
#include <iostream>

using namespace std;
using namespace libReallive;

// -----------------------------------------------------------------------
// Stack Frame
// -----------------------------------------------------------------------

/**
 * Internally used type that represents a stack frame in RLMachine's
 * call stack.
 */
struct RLMachine::StackFrame {
  /// The scenario in the SEEN file for this stack frame.
  libReallive::Scenario* scenario;
    
  /// The instruction pointer in the stack frame.
  libReallive::Scenario::const_iterator ip;

  /**
   * The function that pushed the current frame onto the
   * stack. Used in error checking.
   */
  enum FrameType {
    TYPE_ROOT,   /**< Added by the Machine's constructor */
    TYPE_GOSUB,  /**< Added by a call by gosub */
    TYPE_FARCALL /**< Added by a call by farcall */
  } frameType;

  /// Default constructor
  StackFrame(libReallive::Scenario* s,
             const libReallive::Scenario::const_iterator& i,
             FrameType t) 
    : scenario(s), ip(i), frameType(t) {}
};

// -----------------------------------------------------------------------
// RLMachine
// -----------------------------------------------------------------------

RLMachine::RLMachine(Archive& inArchive) 
  : m_halted(false), m_haltOnException(true), archive(inArchive), 
    m_ownedSystem(new NullSystem), m_system(*m_ownedSystem)
{
  // Arbitrarily set the scenario to the first one in the archive,
  // which is what we want until we get the Gameexe.ini file parser
  // working
  libReallive::Scenario* scenario = inArchive.scenario(archive.begin()->first);
  if(scenario == 0)
    throw rlvm::Exception("Invalid scenario file");
  callStack.push(StackFrame(scenario, scenario->begin(), StackFrame::TYPE_ROOT));

  // Initialize the big memory block to zero
  memset(intVar, 0, sizeof(intVar));
}

// -----------------------------------------------------------------------

RLMachine::RLMachine(System& inSystem, Archive& inArchive) 
  : m_halted(false), m_haltOnException(true), archive(inArchive), 
    m_ownedSystem(NULL), m_system(inSystem)
{
  // Search in the Gameexe for #SEEN_START and place us there
  Gameexe& gameexe = inSystem.gameexe();
  libReallive::Scenario* scenario = NULL;
  if(gameexe.exists("SEEN_START"))
  {
    int firstSeen = gameexe("SEEN_START").to_int();
    scenario = inArchive.scenario(firstSeen);

    if(scenario == NULL)
      cerr << "WARNING: Invalid #SEEN_START in Gameexe" << endl;
  }

  if(scenario == NULL)
  {
    // if SEEN_START is undefined, then just grab the first SEEN.
    scenario = inArchive.scenario(archive.begin()->first);
  }

  if(scenario == 0)
    throw rlvm::Exception("Invalid scenario file");
  callStack.push(StackFrame(scenario, scenario->begin(), StackFrame::TYPE_ROOT));

  // Initialize the big memory block to zero
  memset(intVar, 0, sizeof(intVar));
}

// -----------------------------------------------------------------------

RLMachine::~RLMachine()
{}

// -----------------------------------------------------------------------

void RLMachine::attachModule(RLModule* module) 
{
  int moduleType = module->moduleType();
  int moduleNumber = module->moduleNumber();
  unsigned int packedModule = packModuleNumber(moduleType, moduleNumber);

  ModuleMap::iterator it = modules.find(packedModule);
  if(it != modules.end())
  {
    RLModule& curMod = *it;
    ostringstream ss;
    ss << "Module identification clash: tyring to overwrite "
       << curMod << " with " << *module << endl;

    throw rlvm::Exception(ss.str());
  }

  modules.insert(packedModule, module);
}

// -----------------------------------------------------------------------

/// @todo Refactor this. Seriously.
void RLMachine::executeNextInstruction() 
{
  // Do not execute any more instructions if the machine is halted.
  if(halted() == true)
    return;
  // If we are in a long operation, run it, and end it if it returns true.
  else if(m_longOperationStack.size())
  {
    bool retVal = m_longOperationStack.back()(*this);
    if(retVal)
      m_longOperationStack.pop_back();
  }
  else 
  {
    try 
    {
      callStack.top().ip->runOnMachine(*this);
    }
    catch(std::exception& e) {
      if(m_haltOnException) {
        m_halted = true;
      } else {
        // Advance the instruction pointer so as to prevent infinite
        // loops where we throw an exception, and then try again.
        advanceInstructionPointer();
      }

      cout << "(SEEN" << callStack.top().scenario->sceneNumber() 
           << ")(Line " << m_line << "):  " << e.what() << endl;
    }
  }
}

// -----------------------------------------------------------------------

void RLMachine::executeUntilHalted()
{
  while(!halted()) {
    executeNextInstruction();
  }
}

// -----------------------------------------------------------------------

void RLMachine::advanceInstructionPointer()
{
  callStack.top().ip++;
  if(callStack.top().ip == callStack.top().scenario->end())
    m_halted = true;
}

// -----------------------------------------------------------------------

/** 
 * Helper function that throws errors for illegal memory access
 * 
 * @param location The illegal index that was accessed
 * @see RLMachine::getIntValue
 */
static void throwIllegalIndex(int location) 
{
  ostringstream ss;
  ss << "Illegal index location (" << location 
     << ") in RLMachine::getIntVlaue()";
  throw rlvm::Exception(ss.str());
}

// -----------------------------------------------------------------------

/**
 *
 * @note This method was plagarized from xclannad.
 * @todo Does this allow for access like intL4[]? I don't think it does...
 */
int RLMachine::getIntValue(int type, int location) 
{
  int index = type % 26;
  type /= 26;
  if(index == INTZ_LOCATION_IN_BYTECODE) index = INTZ_LOCATION;
  if(index == INTL_LOCATION_IN_BYTECODE) index = INTL_LOCATION;
  if(index > NUMBER_OF_INT_LOCATIONS) 
      throw rlvm::Exception("Illegal index location in RLMachine::getIntValue()");
  if (type == 0) {
    // A[]..G[], Z[] を直に読む
    if (uint(location) >= 2000) 
      throwIllegalIndex(location);
    return intVar[index][location];
  } else {
    // Ab[]..G4b[], Z8b[] などを読む
    int factor = 1 << (type - 1);
    int eltsize = 32 / factor;
    if (uint(location) >= (64000 / factor)) 
      throwIllegalIndex(location);
    return (intVar[index][location / eltsize] >>
            ((location % eltsize) * factor)) & ((1 << factor) - 1);
  }
}

// -----------------------------------------------------------------------

/**
 *
 * @note This method was plagarized from xclannad.
 */
void RLMachine::setIntValue(int rawtype, int location, int value) 
{
  int type = rawtype / 26;
  int index = rawtype % 26;
  if (index == INTZ_LOCATION_IN_BYTECODE) index = INTZ_LOCATION;
  if (index == INTL_LOCATION_IN_BYTECODE) index = INTL_LOCATION;
  if (index < 0 || index > NUMBER_OF_INT_LOCATIONS) {
    fprintf(stderr, "Error: invalid access to Var<%d>[%d]\n",
            type, location);
  }
  if (type == 0) {
    // A[]..G[], Z[] を直に書く
    if (uint(location) >= 2000) 
      throw rlvm::Exception("Illegal index in RLMachine::setIntValue()");
    intVar[index][location] = value;
  } else {
    // Ab[]..G4b[], Z8b[] などを書く
    int factor = 1 << (type - 1);
    int eltsize = 32 / factor;
    int eltmask = (1 << factor) - 1;
    int shift = (location % eltsize) * factor;
    if (uint(location) >= (64000 / factor)) 
      throw rlvm::Exception("Illegal index in RLMachine::setIntValue()");
    intVar[index][location / eltsize] =
      (intVar[index][location / eltsize] & ~(eltmask << shift))
      | (value & eltmask) << shift;
  }
}

// -----------------------------------------------------------------------

const std::string& RLMachine::getStringValue(int type, int location) 
{
  if(location > 1999)
      throw rlvm::Exception("Invalid range access in RLMachine::setStringValue");

  switch(type) {
  case 0x0A:
    if(location > 2)
      throw rlvm::Exception("Invalid range access on strK in RLMachine::setStringValue");
    return strK[location];
  case 0x0C: return strM[location];
  case 0x12: return strS[location];
  default:
    throw rlvm::Exception("Invalid type in RLMachine::getStringValue");
  }
}

// -----------------------------------------------------------------------

void RLMachine::setStringValue(int type, int number, const std::string& value) {
  if(number > 1999)
      throw rlvm::Exception("Invalid range access in RLMachine::setStringValue");

  switch(type) {
  case 0x0A:
    if(number > 2)
      throw rlvm::Exception("Invalid range access on strK in RLMachine::setStringValue");
    strK[number] = value;
    break;
  case 0x0C:
    strM[number] = value;
    break;
  case 0x12: 
    strS[number] = value;
    break;
  default:
    throw rlvm::Exception("Invalid type in RLMachine::setStringValue");
  }     
}

// -----------------------------------------------------------------------

void RLMachine::executeCommand(const CommandElement& f) {
//   cerr << "About to execute opcode<" << f.modtype() << ":" << f.module() << ":" 
//        << f.opcode() << ", " << f.overload() << ">" << endl;
  ModuleMap::iterator it = modules.find(packModuleNumber(f.modtype(), f.module()));
  if(it != modules.end()) {
    it->dispatchFunction(*this, f);
  } else {
    ostringstream ss;
    ss << "Undefined module<" << f.modtype() << ":" << f.module() << ">";
    throw rlvm::Exception(ss.str());
  }
}

// -----------------------------------------------------------------------

void RLMachine::jump(int scenarioNum, int entrypoint) 
{
  // Check to make sure it's a valid scenario
  libReallive::Scenario* scenario = archive.scenario(scenarioNum);
  if(scenario == 0)
    throw rlvm::Exception("Invalid scenario number in jump");

  callStack.top().scenario = scenario;
  callStack.top().ip = scenario->findEntrypoint(entrypoint);
}

// -----------------------------------------------------------------------

void RLMachine::farcall(int scenarioNum, int entrypoint) 
{
  libReallive::Scenario* scenario = archive.scenario(scenarioNum);
  if(scenario == 0)
    throw rlvm::Exception("Invalid scenario number in jump");

  libReallive::Scenario::const_iterator it = scenario->findEntrypoint(entrypoint);

  callStack.push(StackFrame(scenario, it, StackFrame::TYPE_FARCALL));
}

// -----------------------------------------------------------------------

void RLMachine::returnFromFarcall() 
{
  // Check to make sure the types match up.
  if(callStack.top().frameType != StackFrame::TYPE_FARCALL) {
    throw rlvm::Exception("Callstack type mismatch in returnFromFarcall()");
  }

  callStack.pop();
}

// -----------------------------------------------------------------------

void RLMachine::gotoLocation(BytecodeList::iterator newLocation) {
  // Modify the current frame of the call stack so that it's 
  callStack.top().ip = newLocation;
}

// -----------------------------------------------------------------------

void RLMachine::gosub(BytecodeList::iterator newLocation) 
{
  callStack.push(StackFrame(callStack.top().scenario, newLocation, 
                            StackFrame::TYPE_GOSUB));
}

// -----------------------------------------------------------------------

void RLMachine::returnFromGosub()
{
  // Check to make sure the types match up.
  if(callStack.top().frameType != StackFrame::TYPE_GOSUB) {
    throw rlvm::Exception("Callstack type mismatch in returnFromGosub()");
  }

  callStack.pop();
}

// -----------------------------------------------------------------------

void RLMachine::pushLongOperation(LongOperation* longOperation)
{
  m_longOperationStack.push_back(longOperation);
}

// -----------------------------------------------------------------------

int RLMachine::sceneNumber() const
{
  return callStack.top().scenario->sceneNumber();
}

// -----------------------------------------------------------------------

void RLMachine::executeExpression(const ExpressionElement& e) 
{
  int value = e.parsedExpression().integerValue(*this);
  
  advanceInstructionPointer();
}

// -----------------------------------------------------------------------

int RLMachine::getTextEncoding() const
{
  return callStack.top().scenario->encoding();
}

void RLMachine::performTextout(const TextoutElement& e)
{
  std::string utf8str = cp932toUTF8(e.text(), getTextEncoding());

  TextSystem& ts = system().text();

  // Display UTF-8 characters
  auto_ptr<TextoutLongOperation> ptr(new TextoutLongOperation(*this, utf8str));

  if(ts.messageNoWait())
    ptr->setNoWait();
 
  pushLongOperation(ptr.release());
  advanceInstructionPointer();
}

// -----------------------------------------------------------------------

unsigned int RLMachine::packModuleNumber(int modtype, int module)
{
  return (modtype << 8) | module;
}

// -----------------------------------------------------------------------

void RLMachine::unpackModuleNumber(unsigned int packedModuleNumber, int& modtype,  
                                   int& module)
{
  modtype = packedModuleNumber >> 8;
  module = packedModuleNumber && 0xFF;
}

// -----------------------------------------------------------------------

bool RLMachine::halted() const 
{ 
  return m_halted;
}

// -----------------------------------------------------------------------

void RLMachine::halt() 
{
  m_halted = true; 
}

// -----------------------------------------------------------------------

void RLMachine::setHaltOnException(bool haltOnException) 
{
  m_haltOnException = haltOnException;
}
