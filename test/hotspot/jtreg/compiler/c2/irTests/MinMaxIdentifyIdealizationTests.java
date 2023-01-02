/*
 * Copyright (c) 2022, Oracle and/or its affiliates. All rights reserved.
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
 */

package compiler.c2.irTests;

import jdk.test.lib.Asserts;
import compiler.lib.ir_framework.*;
import java.util.Random;
import jdk.test.lib.Utils;

/*
 * @test
 * @library /test/lib /
 * @requires vm.compiler2.enabled
 * @run driver compiler.c2.irTests.MinMaxIdentifyIdealizationTests
 */
public class MinMaxIdentifyIdealizationTests {
    private static final Random RANDOM = Utils.getRandomInstance();

    public static void main(String[] args) {
        TestFramework.runWithFlags(
            "-XX:+DoUnsafeNanIgnorantMath"
        );
    }

    @Test
    @IR(failOn = {IRNode.PHI})
    @IR(counts = {IRNode.MIN, "1" })
    public double testMin1(double v1, double v2) {
        return v1 > v2 ? v2 : v1;
    }

    @Test
    @IR(failOn = {IRNode.PHI})
    @IR(counts = {IRNode.MIN, "1" })
    public double testMin2(double v1, double v2) {
        return v1 >= v2 ? v2 : v1;
    }

    @Test
    @IR(failOn = {IRNode.PHI})
    @IR(counts = {IRNode.MIN, "1" })
    public double testMin3(double v1, double v2) {
        return v1 < v2 ? v1 : v2;
    }

    @Test
    @IR(failOn = {IRNode.PHI})
    @IR(counts = {IRNode.MIN, "1" })
    public double testMin4(double v1, double v2) {
        return v1 <= v2 ? v1 : v2;
    }

    //// max

    @Test
    @IR(failOn = {IRNode.PHI})
    @IR(counts = {IRNode.MAX, "1" })
    public double testMax1(double v1, double v2) {
        return v1 > v2 ? v1 : v2;
    }

    @Test
    @IR(failOn = {IRNode.PHI})
    @IR(counts = {IRNode.MAX, "1" })
    public double testMax2(double v1, double v2) {
        return v1 >= v2 ? v1 : v2;
    }

    @Test
    @IR(failOn = {IRNode.PHI})
    @IR(counts = {IRNode.MAX, "1" })
    public double testMax3(double v1, double v2) {
        return v1 < v2 ? v2 : v1;
    }

    @Test
    @IR(failOn = {IRNode.PHI})
    @IR(counts = {IRNode.MAX, "1" })
    public double testMax4(double v1, double v2) {
        return v1 <= v2 ? v2 : v1;
    }

    @Run(test = {"testMin1", "testMin2", "testMin3", "testMin4", "testMax1", "testMax2", "testMax3", "testMax4"})
    public void runTests() {
        for (int i = 0; i < 100; i++) {
            double v1 = RANDOM.nextDouble();
            double v2 = RANDOM.nextDouble();
            double minRes = Math.min(v1, v2);
            double maxRes = Math.max(v1, v2);

            Asserts.assertEQ(testMin1(v1, v2), minRes);
            Asserts.assertEQ(testMin2(v1, v2), minRes);
            Asserts.assertEQ(testMin3(v1, v2), minRes);
            Asserts.assertEQ(testMin4(v1, v2), minRes);

            Asserts.assertEQ(testMax1(v1, v2), maxRes);
            Asserts.assertEQ(testMax2(v1, v2), maxRes);
            Asserts.assertEQ(testMax3(v1, v2), maxRes);
            Asserts.assertEQ(testMax4(v1, v2), maxRes);
        }
    }
}