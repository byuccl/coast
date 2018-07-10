//===-- MSP430MCAsmInfo.cpp - MSP430 asm properties -----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the declarations of the MSP430MCAsmInfo properties.
//
//===----------------------------------------------------------------------===//

#include "MSP430MCAsmInfo.h"
using namespace llvm;

void MSP430MCAsmInfo::anchor() { }

MSP430MCAsmInfo::MSP430MCAsmInfo(const Triple &TT) {
  PointerSize = CalleeSaveStackSlotSize = 2;

  CommentString = ";";

  AlignmentIsInBytes = false;
  UsesELFSectionDirectiveForBSS = true;

  //Print globals as .global, not .globl
  GlobalDirective = "\t.global\t";

  //Remove the .type and .size before functions/variables
  HasDotTypeDotSizeDirective = false;

  //Remove the .file at the start of the file
  HasSingleParameterDotFile = false;

  //Remove the .ident at the end of the file
  HasIdentDirective = false;
}
