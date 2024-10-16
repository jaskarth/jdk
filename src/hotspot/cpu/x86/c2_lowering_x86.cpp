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

#include "opto/node.hpp"
#include "opto/vectornode.hpp"
#include "opto/movenode.hpp"
#include "opto/phaseX.hpp"

static bool is_vector_unary_bitwise_op(Node* n) {
  return n->Opcode() == Op_XorV &&
         VectorNode::is_vector_bitwise_not_pattern(n);
}

static bool is_vector_binary_bitwise_op(Node* n) {
  switch (n->Opcode()) {
    case Op_AndV:
    case Op_OrV:
      return true;

    case Op_XorV:
      return !is_vector_unary_bitwise_op(n);

    default:
      return false;
  }
}

static bool is_vector_ternary_bitwise_op(Node* n) {
  return n->Opcode() == Op_MacroLogicV;
}

static bool is_vector_bitwise_op(Node* n) {
  return is_vector_unary_bitwise_op(n)  ||
         is_vector_binary_bitwise_op(n) ||
         is_vector_ternary_bitwise_op(n);
}

static bool is_vector_bitwise_cone_root(Node* n) {
  if (n->bottom_type()->isa_vectmask() || !is_vector_bitwise_op(n)) {
    return false;
  }
  for (DUIterator_Fast imax, i = n->fast_outs(imax); i < imax; i++) {
    if (is_vector_bitwise_op(n->fast_out(i))) {
      return false;
    }
  }
  return true;
}

static uint collect_unique_inputs(Node* n, Unique_Node_List& inputs) {
  uint cnt = 0;
  if (is_vector_bitwise_op(n)) {
    uint inp_cnt = n->is_predicated_vector() ? n->req()-1 : n->req();
    if (VectorNode::is_vector_bitwise_not_pattern(n)) {
      for (uint i = 1; i < inp_cnt; i++) {
        Node* in = n->in(i);
        bool skip = VectorNode::is_all_ones_vector(in);
        if (!skip && !inputs.member(in)) {
          inputs.push(in);
          cnt++;
        }
      }
      assert(cnt <= 1, "not unary");
    } else {
      uint last_req = inp_cnt;
      if (is_vector_ternary_bitwise_op(n)) {
        last_req = inp_cnt - 1; // skip last input
      }
      for (uint i = 1; i < last_req; i++) {
        Node* def = n->in(i);
        if (!inputs.member(def)) {
          inputs.push(def);
          cnt++;
        }
      }
    }
  } else { // not a bitwise operations
    if (!inputs.member(n)) {
      inputs.push(n);
      cnt++;
    }
  }
  return cnt;
}

// Criteria under which nodes gets packed into a macro logic node:-
//  1) Parent and both child nodes are all unmasked or masked with
//     same predicates.
//  2) Masked parent can be packed with left child if it is predicated
//     and both have same predicates.
//  3) Masked parent can be packed with right child if its un-predicated
//     or has matching predication condition.
//  4) An unmasked parent can be packed with an unmasked child.
bool compute_logic_cone(Node* n, Unique_Node_List& partition, Unique_Node_List& inputs) {
  assert(partition.size() == 0, "not empty");
  assert(inputs.size() == 0, "not empty");
  if (is_vector_ternary_bitwise_op(n)) {
    return false;
  }

  bool is_unary_op = is_vector_unary_bitwise_op(n);
  if (is_unary_op) {
    assert(collect_unique_inputs(n, inputs) == 1, "not unary");
    return false; // too few inputs
  }

  bool pack_left_child = true;
  bool pack_right_child = true;

  bool left_child_LOP = is_vector_bitwise_op(n->in(1));
  bool right_child_LOP = is_vector_bitwise_op(n->in(2));

  int left_child_input_cnt = 0;
  int right_child_input_cnt = 0;

  bool parent_is_predicated = n->is_predicated_vector();
  bool left_child_predicated = n->in(1)->is_predicated_vector();
  bool right_child_predicated = n->in(2)->is_predicated_vector();

  Node* parent_pred = parent_is_predicated ? n->in(n->req()-1) : nullptr;
  Node* left_child_pred = left_child_predicated ? n->in(1)->in(n->in(1)->req()-1) : nullptr;
  Node* right_child_pred = right_child_predicated ? n->in(1)->in(n->in(1)->req()-1) : nullptr;

  do {
    if (pack_left_child && left_child_LOP &&
        ((!parent_is_predicated && !left_child_predicated) ||
         ((parent_is_predicated && left_child_predicated &&
           parent_pred == left_child_pred)))) {
      partition.push(n->in(1));
      left_child_input_cnt = collect_unique_inputs(n->in(1), inputs);
    } else {
      inputs.push(n->in(1));
      left_child_input_cnt = 1;
    }

    if (pack_right_child && right_child_LOP &&
        (!right_child_predicated ||
         (right_child_predicated && parent_is_predicated &&
          parent_pred == right_child_pred))) {
      partition.push(n->in(2));
      right_child_input_cnt = collect_unique_inputs(n->in(2), inputs);
    } else {
      inputs.push(n->in(2));
      right_child_input_cnt = 1;
    }

    if (inputs.size() > 3) {
      assert(partition.size() > 0, "");
      inputs.clear();
      partition.clear();
      if (left_child_input_cnt > right_child_input_cnt) {
        pack_left_child = false;
      } else {
        pack_right_child = false;
      }
    } else {
      break;
    }
  } while(true);

  if(partition.size()) {
    partition.push(n);
  }

  return (partition.size() == 2 || partition.size() == 3) &&
         (inputs.size()    == 2 || inputs.size()    == 3);
}

static uint extract_bit(uint func, uint pos) {
  return (func & (1 << pos)) >> pos;
}

//
//  A macro logic node represents a truth table. It has 4 inputs,
//  First three inputs corresponds to 3 columns of a truth table
//  and fourth input captures the logic function.
//
//  eg.  fn = (in1 AND in2) OR in3;
//
//      MacroNode(in1,in2,in3,fn)
//
//  -----------------
//  in1 in2 in3  fn
//  -----------------
//  0    0   0    0
//  0    0   1    1
//  0    1   0    0
//  0    1   1    1
//  1    0   0    0
//  1    0   1    1
//  1    1   0    1
//  1    1   1    1
//

uint eval_macro_logic_op(uint func, uint in1 , uint in2, uint in3) {
  int res = 0;
  for (int i = 0; i < 8; i++) {
    int bit1 = extract_bit(in1, i);
    int bit2 = extract_bit(in2, i);
    int bit3 = extract_bit(in3, i);

    int func_bit_pos = (bit1 << 2 | bit2 << 1 | bit3);
    int func_bit = extract_bit(func, func_bit_pos);

    res |= func_bit << i;
  }
  return res;
}

static uint eval_operand(Node* n, ResourceHashtable<Node*,uint>& eval_map) {
  assert(n != nullptr, "");
  assert(eval_map.contains(n), "absent");
  return *(eval_map.get(n));
}

static void eval_operands(Node* n,
                          uint& func1, uint& func2, uint& func3,
                          ResourceHashtable<Node*,uint>& eval_map) {
  assert(is_vector_bitwise_op(n), "");

  if (is_vector_unary_bitwise_op(n)) {
    Node* opnd = n->in(1);
    if (VectorNode::is_vector_bitwise_not_pattern(n) && VectorNode::is_all_ones_vector(opnd)) {
      opnd = n->in(2);
    }
    func1 = eval_operand(opnd, eval_map);
  } else if (is_vector_binary_bitwise_op(n)) {
    func1 = eval_operand(n->in(1), eval_map);
    func2 = eval_operand(n->in(2), eval_map);
  } else {
    assert(is_vector_ternary_bitwise_op(n), "unknown operation");
    func1 = eval_operand(n->in(1), eval_map);
    func2 = eval_operand(n->in(2), eval_map);
    func3 = eval_operand(n->in(3), eval_map);
  }
}

uint compute_truth_table(Unique_Node_List& partition, Unique_Node_List& inputs) {
  assert(inputs.size() <= 3, "sanity");
  ResourceMark rm;
  uint res = 0;
  ResourceHashtable<Node*,uint> eval_map;

  // Populate precomputed functions for inputs.
  // Each input corresponds to one column of 3 input truth-table.
  uint input_funcs[] = { 0xAA,   // (_, _, c) -> c
                         0xCC,   // (_, b, _) -> b
                         0xF0 }; // (a, _, _) -> a
  for (uint i = 0; i < inputs.size(); i++) {
    eval_map.put(inputs.at(i), input_funcs[2-i]);
  }

  for (uint i = 0; i < partition.size(); i++) {
    Node* n = partition.at(i);

    uint func1 = 0, func2 = 0, func3 = 0;
    eval_operands(n, func1, func2, func3, eval_map);

    switch (n->Opcode()) {
      case Op_OrV:
        assert(func3 == 0, "not binary");
        res = func1 | func2;
        break;
      case Op_AndV:
        assert(func3 == 0, "not binary");
        res = func1 & func2;
        break;
      case Op_XorV:
        if (VectorNode::is_vector_bitwise_not_pattern(n)) {
          assert(func2 == 0 && func3 == 0, "not unary");
          res = (~func1) & 0xFF;
        } else {
          assert(func3 == 0, "not binary");
          res = func1 ^ func2;
        }
        break;
      case Op_MacroLogicV:
        // Ordering of inputs may change during evaluation of sub-tree
        // containing MacroLogic node as a child node, thus a re-evaluation
        // makes sure that function is evaluated in context of current
        // inputs.
        res = eval_macro_logic_op(n->in(4)->get_int(), func1, func2, func3);
        break;

      default: assert(false, "not supported: %s", n->Name());
    }
    assert(res <= 0xFF, "invalid");
    eval_map.put(n, res);
  }
  return res;
}

Node* xform_to_MacroLogicV(PhaseIterGVN& igvn,
                                    const TypeVect* vt,
                                    Unique_Node_List& partition,
                                    Unique_Node_List& inputs) {
  assert(partition.size() == 2 || partition.size() == 3, "not supported");
  assert(inputs.size()    == 2 || inputs.size()    == 3, "not supported");
  assert(Matcher::match_rule_supported_vector(Op_MacroLogicV, vt->length(), vt->element_basic_type()), "not supported");

  Node* in1 = inputs.at(0);
  Node* in2 = inputs.at(1);
  Node* in3 = (inputs.size() == 3 ? inputs.at(2) : in2);

  uint func = compute_truth_table(partition, inputs);

  Node* pn = partition.at(partition.size() - 1);
  Node* mask = pn->is_predicated_vector() ? pn->in(pn->req()-1) : nullptr;
  return MacroLogicVNode::make(igvn, in1, in2, in3, mask, func, vt);
}

static Node* process_logic_cone_root(PhaseLowering* phase, Node *n, VectorSet &visited) {
  assert(is_vector_bitwise_op(n), "not a root");

  visited.set(n->_idx);

  // 1) Do a DFS walk over the logic cone.
  for (uint i = 1; i < n->req(); i++) {
    Node* in = n->in(i);
    if (!visited.test(in->_idx) && is_vector_bitwise_op(in)) {
      phase->igvn()->replace_node(in, phase->transform(process_logic_cone_root(phase, in, visited)));
    }
  }

  // 2) Bottom up traversal: Merge node[s] with
  // the parent to form macro logic node.
  Unique_Node_List partition;
  Unique_Node_List inputs;
  if (compute_logic_cone(n, partition, inputs)) {
    const TypeVect* vt = n->bottom_type()->is_vect();
    Node* pn = partition.at(partition.size() - 1);
    Node* mask = pn->is_predicated_vector() ? pn->in(pn->req()-1) : nullptr;
    if (mask == nullptr ||
        Matcher::match_rule_supported_vector_masked(Op_MacroLogicV, vt->length(), vt->element_basic_type())) {
      Node* macro_logic = xform_to_MacroLogicV(*phase->igvn(), vt, partition, inputs);
      VectorNode::trace_new_vector(macro_logic, "MacroLogic");
      return macro_logic;
    }
  }

  return nullptr;
}

Node* PhaseLowering::lower_node(Node* in) {
  // Match macro logic (vpternlogd) nodes
  if (Matcher::match_rule_supported(Op_MacroLogicV) && is_vector_bitwise_cone_root(in)) {
    const TypeVect* vt = in->bottom_type()->is_vect();
    bool supported = Matcher::match_rule_supported_vector(Op_MacroLogicV, vt->length(), vt->element_basic_type());
    if (supported) {
      VectorSet visited(C->comp_arena());
      return process_logic_cone_root(this, in, visited);
    }
  }
  if (!UseNewCode) {
    return nullptr;
  }

  switch (in->Opcode()) {
    case Op_AbsI: {
      // abs(i) => i < 0 ? -i : i;
      Node* i = in->in(1);
      Node* zero = transform(_igvn->zerocon(T_INT));
      // 0 - i
      Node* sub = transform(new SubINode(zero, i));
      // i < 0;
      Node* cmp = transform(new CmpINode(sub, zero));
      Node* bol = transform(new BoolNode(cmp, BoolTest::lt));
      return CMoveNode::make(nullptr, bol, i, sub, TypeInt::INT);
    }
    case Op_ReverseBytesS: {
      Node* reverse_i = transform(new ReverseBytesINode(nullptr, in->in(1)));

      return new RShiftINode(reverse_i, transform(_igvn->intcon(16)));
    }
  }

  return nullptr;
}