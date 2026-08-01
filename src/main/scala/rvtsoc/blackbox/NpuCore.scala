/*
 * Copyright (c) 2026 Institute of Computing Technology, Chinese Academy of Sciences
 * SPDX-License-Identifier: MulanPSL-2.0
 *
 * Dayan is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *
 *     http://license.coscl.org.cn/MulanPSL2
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

package rvtsoc.blackbox

import chisel3._
import chisel3.util._

import org.chipsalliance.cde.config.Parameters

import xs.infra.axi.ExtAxiBundle
import xs.infra.axi.AxiParams
import rvtsoc.subsystem.XbarParamsKey
import rvtsoc.subsystem.{BaseNpuCore, BaseRvtngCore}
import rvtsoc.subsystem.BaseScalarCore

class NpuCore(implicit p: Parameters) extends BaseNpuCore {
  io <> DontCare
}

class ScalarCore(implicit p: Parameters) extends BaseScalarCore {
  io <> DontCare
}

class RvtngCore(implicit p: Parameters) extends BaseRvtngCore {
  io <> DontCare
}