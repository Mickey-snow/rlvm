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
//
// -----------------------------------------------------------------------

#include "TestSystem/TestTextWindow.hpp"

#include "Systems/Base/Rect.hpp"
#include "TestSystem/TestSurface.hpp"

#include <boost/shared_ptr.hpp>
#include <sstream>

using std::ostringstream;
using boost::shared_ptr;

// -----------------------------------------------------------------------

TestTextWindow::TestTextWindow(System& system, int x)
    : TextWindow(system, x), text_window_log_("TestTextWindow") {
}

// -----------------------------------------------------------------------

TestTextWindow::~TestTextWindow() {}

// -----------------------------------------------------------------------

shared_ptr<Surface> TestTextWindow::textSurface() {
  // TODO(erg): May need to use a real size?
  return shared_ptr<Surface>(new TestSurface("Text Surface", Size(640, 480)));
}

// -----------------------------------------------------------------------

shared_ptr<Surface> TestTextWindow::nameSurface() {
  return name_surface_;
}

// -----------------------------------------------------------------------

void TestTextWindow::renderNameInBox(const std::string& utf8str) {
  ostringstream oss;
  oss << "Name Surface [" << utf8str << "]";
  name_surface_.reset(new TestSurface(oss.str(), Size(640, 480)));
}

// -----------------------------------------------------------------------

void TestTextWindow::clearWin()
{
  text_window_log_.recordFunction("clearWin");
  TextWindow::clearWin();
  current_contents_ = "";
  name_surface_.reset();
}

// -----------------------------------------------------------------------

void TestTextWindow::setFontColor(const std::vector<int>& colour_data) {
  text_window_log_.recordFunction("setFontColor");
  TextWindow::setFontColor(colour_data);
}

// -----------------------------------------------------------------------

bool TestTextWindow::displayChar(const std::string& current,
                                 const std::string& next)
{
  text_window_log_.recordFunction("displayChar", current, next);

  current_contents_ += current;
  return true;
}

// -----------------------------------------------------------------------

void TestTextWindow::setName(const std::string& utf8name,
                             const std::string& next_char)
{
  text_window_log_.recordFunction("setName", utf8name, next_char);

  current_contents_ += "\\{" + utf8name + "}";

  TextWindow::setName(utf8name, next_char);
}

// -----------------------------------------------------------------------

void TestTextWindow::hardBrake()
{
  text_window_log_.recordFunction("hardBrake");
  current_contents_ += "\n";
  TextWindow::hardBrake();
}

// -----------------------------------------------------------------------

void TestTextWindow::resetIndentation() {
  text_window_log_.recordFunction("resetIndentation");
  TextWindow::resetIndentation();
}

// -----------------------------------------------------------------------

void TestTextWindow::markRubyBegin() {
  text_window_log_.recordFunction("markRubyBegin");
  TextWindow::markRubyBegin();
}

// -----------------------------------------------------------------------

void TestTextWindow::displayRubyText(const std::string& utf8str) {
  text_window_log_.recordFunction("displayRubyText", utf8str);
}