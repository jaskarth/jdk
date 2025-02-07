/*
 * Copyright (c) 2024, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#include "precompiled.hpp"
#ifdef COMPILER2
#include "opto/node.hpp"
#include "opto/lowerednode.hpp"
#include "opto/phaseX.hpp"

Node* PhaseLowering::lower_node_platform(Node* n) {
  int opc = n->Opcode();
  if (opc == Op_AndI && UseBMI1Instructions) {
    // Look for (AndI (URShiftI ... shift) mask) where mask is a power of two minus one

    const TypeInt* mask_type = type(n->in(2))->isa_int();

    // Check for URShiftI and that 'mask' is a con
    if (n->in(1)->Opcode() == Op_URShiftI && mask_type != nullptr && mask_type->is_con()) {
      const TypeInt* shift_type = type(n->in(1)->in(2))->isa_int();

      // Check that 'shift' is a con
      if (shift_type != nullptr && shift_type->is_con()) {
        // Make sure that 'mask' is a power of two minus 1
        if (is_power_of_2(mask_type->get_con() + 1)) {
          int and_value = exact_log2(mask_type->get_con() + 1);
          int shift_value = shift_type->get_con();

          if (and_value + shift_value < BitsPerJavaInteger) {
            // Encode the constant for the bextr
            int bextr_imm = and_value << 8 | shift_value;

//            Compile::current()->method()->print_name();
//            tty->print_cr(" %d %d", and_value, shift_value);

            return new BextrNode(n->in(1)->in(1), intcon(bextr_imm));
          }
        }
      }
    }
  }

  // TODO: bextr also works for the (less common) pattern (URShiftI (AndI ... mask) shift) where mask == 2^shift

  return nullptr;
}

bool PhaseLowering::should_lower() {
  return true;
}
#endif // COMPILER2
