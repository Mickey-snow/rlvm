// -*- Mode: C++; tab-width:2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
// vi:tw=80:et:ts=2:sts=2
//
// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2009 Elliot Glaysher
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
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
// -----------------------------------------------------------------------

#include "Precompiled.hpp"

// -----------------------------------------------------------------------

#include "Systems/Base/TextWaku.hpp"

#include "Systems/Base/GraphicsSystem.hpp"
#include "Systems/Base/Rect.hpp"
#include "Systems/Base/Surface.hpp"
#include "Systems/Base/System.hpp"
#include "Systems/Base/TextSystem.hpp"
#include "Systems/Base/TextWindow.hpp"
#include "Systems/Base/TextWindowButton.hpp"
#include "libReallive/gameexe.h"

#include <sstream>
#include <iomanip>
#include <iostream>
#include <boost/bind.hpp>

using std::endl;
using std::ostringstream;
using std::setfill;
using std::setw;

// -----------------------------------------------------------------------

/**
 * Definitions for the location and Gameexe.ini keys describing various text
 * window buttons.
 *
 * Previously was using a map keyed on strings. In rendering code. With keys
 * that had similar prefixes. WTF was I smoking...
 */
static struct ButtonInfo {
  int index;
  const char* button_name;
  int waku_offset;
} BUTTON_INFO[] = {
  { 0, "CLEAR_BOX", 8},
  { 1, "MSGBKLEFT_BOX", 24},
  { 2, "MSGBKRIGHT_BOX", 32},
  { 3, "EXBTN_000_BOX", 40},
  { 4, "EXBTN_001_BOX", 48},
  { 5, "EXBTN_002_BOX", 56},
  { 6, "EXBTN_003_BOX", 64},
  { 7, "EXBTN_004_BOX", 72},
  { 8, "EXBTN_005_BOX", 80},
  { 9, "EXBTN_006_BOX", 88},
  {10, "READJUMP_BOX", 104},
  {11, "AUTOMODE_BOX", 112},
  {-1, NULL, -1}
};

// -----------------------------------------------------------------------
// TextWaku
// -----------------------------------------------------------------------
TextWaku::TextWaku(System& system, TextWindow& window, int setno, int no)
    : system_(system), window_(window), setno_(setno), no_(no) {
  loadWindowWaku();
}

// -----------------------------------------------------------------------

TextWaku::~TextWaku() {
}
// -----------------------------------------------------------------------

void TextWaku::execute() {
  for (int i = 0; BUTTON_INFO[i].index != -1; ++i) {
    if (button_map_[i]) {
      button_map_[i]->execute();
    }
  }
}

// -----------------------------------------------------------------------

void TextWaku::render(std::ostream* tree, Point box_location) {
  if (tree) {
    *tree << "    Window Waku(" << setno_ << ", " << no_ << "):" << endl;
  }

  if (waku_backing_) {
    Size backing_size = waku_backing_->size();
    // COLOUR
    waku_backing_->renderToScreenAsColorMask(
        Rect(Point(0, 0), backing_size),
        Rect(box_location, backing_size),
        window_.colour(), window_.filter());

    if (tree) {
      *tree << "      Backing Area: " << Rect(box_location, backing_size)
            << endl;
    }
  }

  if (waku_main_) {
    Size main_size = waku_main_->size();
    waku_main_->renderToScreen(
        Rect(Point(0, 0), main_size), Rect(box_location, main_size), 255);

    if (tree) {
      *tree << "      Main Area: " << Rect(box_location, main_size)
            << endl;
    }
  }

  if (waku_button_)
    renderButtons();
}

// -----------------------------------------------------------------------

void TextWaku::renderButtons() {
  for (int i = 0; BUTTON_INFO[i].index != -1; ++i) {
    if (button_map_[i]) {
      button_map_[i]->render(window_, waku_button_, BUTTON_INFO[i].waku_offset);
    }
  }
}

// -----------------------------------------------------------------------

void TextWaku::setMousePosition(const Point& pos) {
  for (int i = 0; BUTTON_INFO[i].index != -1; ++i) {
    if (button_map_[i]) {
      button_map_[i]->setMousePosition(window_, pos);
    }
  }
}

// -----------------------------------------------------------------------

bool TextWaku::handleMouseClick(RLMachine& machine, const Point& pos,
                                bool pressed) {
  for (int i = 0; BUTTON_INFO[i].index != -1; ++i) {
    if (button_map_[i]) {
      if (button_map_[i]->handleMouseClick(machine, window_, pos, pressed))
        return true;
    }
  }

  return false;
}

// -----------------------------------------------------------------------

void TextWaku::loadWindowWaku() {
  using namespace boost;

  GameexeInterpretObject waku(system_.gameexe()("WAKU", setno_, no_));

  setWakuMain(waku("NAME").to_string(""));
  setWakuBacking(waku("BACK").to_string(""));
  setWakuButton(waku("BTN").to_string(""));

  TextSystem& ts = system_.text();
  GraphicsSystem& gs = system_.graphics();

  button_map_[0].reset(
      new ActionTextWindowButton(
          system_,
          ts.windowClearUse(), waku("CLEAR_BOX"),
          bind(&GraphicsSystem::toggleInterfaceHidden, ref(gs))));
  button_map_[1].reset(
    new RepeatActionWhileHoldingWindowButton(
        system_,
        ts.windowMsgbkleftUse(), waku("MSGBKLEFT_BOX"),
        bind(&TextSystem::backPage, ref(ts)),
        250));
  button_map_[2].reset(
      new RepeatActionWhileHoldingWindowButton(
          system_,
          ts.windowMsgbkrightUse(), waku("MSGBKRIGHT_BOX"),
          bind(&TextSystem::forwardPage, ref(ts)),
          250));

  for (int i = 0; i < 7; ++i) {
    GameexeInterpretObject wbcall(system_.gameexe()("WBCALL", i));
    ostringstream oss;
    oss << "EXBTN_" << setw(3) << setfill('0') << i << "_BOX";
    button_map_[3 + i].reset(
      new ExbtnWindowButton(
          system_, ts.windowExbtnUse(), waku(oss.str()), wbcall));
  }

  ActivationTextWindowButton* readjump_box =
      new ActivationTextWindowButton(
          system_,
          ts.windowReadJumpUse(), waku("READJUMP_BOX"),
          bind(&TextSystem::setSkipMode, ref(ts), true),
          bind(&TextSystem::setSkipMode, ref(ts), false));
  button_map_[10].reset(readjump_box);
  ts.skipModeSignal().connect(bind(&ActivationTextWindowButton::setActivated,
                                   readjump_box, _1));
  ts.skipModeEnabledSignal().connect(
    bind(&ActivationTextWindowButton::setEnabled, readjump_box, _1));

  ActivationTextWindowButton* automode_button =
    new ActivationTextWindowButton(
        system_,
        ts.windowAutomodeUse(), waku("AUTOMODE_BOX"),
        bind(&TextSystem::setAutoMode, ref(ts), true),
        bind(&TextSystem::setAutoMode, ref(ts), false));
  button_map_[11].reset(automode_button);
  ts.autoModeSignal().connect(bind(&ActivationTextWindowButton::setActivated,
                                   automode_button, _1));

  /*
   * TODO: I didn't translate these to the new way of doing things. I don't
   * seem to be rendering them. Must deal with this later.
   *
  string key = "MOVE_BOX";
  button_map_.insert(
    key, new TextWindowButton(ts.windowMoveUse(), waku("MOVE_BOX")));

  key = string("MSGBK_BOX");
  button_map_.insert(
    key, new TextWindowButton(ts.windowMsgbkUse(), waku("MSGBK_BOX")));
  */
}

// -----------------------------------------------------------------------

void TextWaku::setWakuMain(const std::string& name) {
  if (name != "")
    waku_main_ = system_.graphics().loadNonCGSurfaceFromFile(name);
  else
    waku_main_.reset();
}

// -----------------------------------------------------------------------

void TextWaku::setWakuBacking(const std::string& name) {
  if (name != "") {
    waku_backing_ = system_.graphics().loadNonCGSurfaceFromFile(name);
    waku_backing_->setIsMask(true);
  } else {
    waku_backing_.reset();
  }
}

// -----------------------------------------------------------------------

void TextWaku::setWakuButton(const std::string& name) {
  if (name != "")
    waku_button_ = system_.graphics().loadNonCGSurfaceFromFile(name);
  else
    waku_button_.reset();
}
