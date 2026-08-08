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

package dayan.rvtsoc.subsystem

import chisel3._
import chisel3.util._

import org.chipsalliance.cde.config
import org.chipsalliance.cde.config.Parameters
import org.chipsalliance.diplomacy.lazymodule._

import dayan.wiredrv.WiredCore
import dayan.wiredrv.CoreParams

import xs.infra.axi.AxiBundle

import dayan.rvtsoc.config.WiredrvParamsKey

class WiredrvCore(implicit p: config.Parameters) extends BaseScalarCore {

  implicit val coreParams: CoreParams = p(WiredrvParamsKey).get
  val wiredCore = Module(new WiredCore)

  wiredCore.io.inst.ready := true.B

  wiredCore.io.hartId := io.hartId
  wiredCore.io.mtip := io.mtip
  wiredCore.io.msip := io.msip
  wiredCore.io.meip := io.meip
  wiredCore.io.debug := io.debug
  wiredCore.io.time := io.time

  wiredCore.io.clusterInterrupt := false.B

  io.tensorInst.valid := wiredCore.io.tensorInst.valid
  io.tensorInst.bits := wiredCore.io.tensorInst.bits
  wiredCore.io.tensorInst.ready := io.tensorInst.ready

  wiredCore.io.tensorResp.valid := io.tensorResp.valid
  io.tensorResp.ready := wiredCore.io.tensorResp.ready
  wiredCore.io.tensorResp.bits := io.tensorResp.bits

  wiredCore.io.ramctl := DontCare
  wiredCore.io.dft <> DontCare

  require(
    io.m_axi.params.idBits >= wiredCore.io.mem.params.idBits,
    s"require io.m_axi.params.idBits >= wiredCore.io.mem.params.idBits, but ${io.m_axi.params.idBits} < ${wiredCore.io.mem.params.idBits}"
  )
  io.m_axi <> wiredCore.io.mem
}
