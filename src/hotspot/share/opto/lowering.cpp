/*
 * Copyright (c) 2025, Oracle and/or its affiliates. All rights reserved.
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

#include "opto/node.hpp"
#include "opto/addnode.hpp"
#include "opto/connode.hpp"
#include "opto/divnode.hpp"
#include "opto/mulnode.hpp"
#include "opto/subnode.hpp"
#include "opto/matcher.hpp"
#include "opto/phaseX.hpp"

Node* handle_div_mod_op(Node* n, BasicType bt, bool is_unsigned) {
  if (!UseDivMod) {
    assert(false, "should not reach");
  }

  // Check if "a % b" and "a / b" both exist
  Node* d = n->find_similar(Op_DivIL(bt, is_unsigned));
  if (d == nullptr) {
    return nullptr;
  }

  // Replace them with a fused divmod if supported
  if (Matcher::has_match_rule(Op_DivModIL(bt, is_unsigned))) {
    DivModNode* divmod = DivModNode::make(n, bt, is_unsigned);
    // If the divisor input for a Div (or Mod etc.) is not zero, then the control input of the Div is set to zero.
    // It could be that the divisor input is found not zero because its type is narrowed down by a CastII in the
    // subgraph for that input. Range check CastIIs are removed during final graph reshape. To preserve the dependency
    // carried by a CastII, precedence edges are added to the Div node. We need to transfer the precedence edges to the
    // DivMod node so the dependency is not lost.
    divmod->add_prec_from(n);
    divmod->add_prec_from(d);
    d->subsume_by(divmod->div_proj(), Compile::current());
    return divmod->mod_proj();
  } else {
    // Replace "a % b" with "a - ((a / b) * b)"
    Node* mult = MulNode::make(d, d->in(2), bt);
    Node* sub = SubNode::make(d->in(1), mult, bt);
    return sub;
  }
}

Node* PhaseLowering::lower_node(Node* n) {
  // Apply shared lowering transforms

  if (UseDivMod) {
    switch (n->Opcode()) {
      case Op_ModI:
        handle_div_mod_op(n, T_INT, false);
        break;

      case Op_ModL:
        handle_div_mod_op(n, T_LONG, false);
        break;

      case Op_UModI:
        handle_div_mod_op(n, T_INT, true);
        break;

      case Op_UModL:
        handle_div_mod_op(n, T_LONG, true);
        break;
    }
  }

  if (n->Opcode() == Op_CmpUL && !Matcher::has_match_rule(Op_CmpUL)) {
    // No support for unsigned long comparisons
    Node* sign_pos = new ConINode(TypeInt::make(BitsPerLong - 1));
    Node* sign_bit_mask = new RShiftLNode(n->in(1), sign_pos);
    Node* orl = new OrLNode(n->in(1), sign_bit_mask);
    Node* remove_sign_mask = new ConLNode(TypeLong::make(max_jlong));
    Node* andl = new AndLNode(orl, remove_sign_mask);
    Node* cmp = new CmpLNode(andl, n->in(2));
    return cmp;
  }

  // Apply backend-specific lowering transforms
  return lower_node_platform(n);
}

Node* PhaseLowering::mask_shifted_count(Node* n) {
  // The cpu's shift instructions don't restrict the count to the
  // lower 5/6 bits. We need to do the masking ourselves.
  Node* in2 = n->in(2);
  juint mask = (n->bottom_type() == TypeInt::INT) ? (BitsPerInt - 1) : (BitsPerLong - 1);
  const TypeInt* t = in2->find_int_type();
  if (t != nullptr && t->is_con()) {
    juint shift = t->get_con();
    if (shift > mask) { // Unsigned cmp
      n->set_req(2, ConNode::make(TypeInt::make(shift & mask)));

      return n;
    }
  } else {
    if (t == nullptr || t->_lo < 0 || t->_hi > (int)mask) {
      Node* shift = new AndINode(in2, ConNode::make(TypeInt::make(mask)));
      n->set_req(2, shift);

      return n;
    }
  }

  return nullptr;
}