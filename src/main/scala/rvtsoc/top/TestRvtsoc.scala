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

package dayan.rvtsoc.top

import chisel3._
import chisel3.util._

import org.chipsalliance.cde.config.Parameters
import org.chipsalliance.diplomacy.lazymodule.LazyModule

import freechips.rocketchip.devices.debug.SimJTAG
import freechips.rocketchip.amba.axi4._

import xs.infra.axi.ExtAxiBundle

import dayan.rvtsoc.utils.ExtraFiles
import dayan.rvtsoc.devices._
import dayan.rvtsoc.subsystem.XbarParamsKey
import dayan.rvtsoc.subsystem.utils

class TestRvtsoc(implicit p: Parameters) extends Module {
  val frontAxip = p(XbarParamsKey).frontAxip
  val extAxip = p(XbarParamsKey).extAxip
  val soc = Module(new Rvtsoc)
  val io = IO(new Bundle {
    val success = Output(Bool())
  })
  val axiMasterDPI = Module(
    new AXI4MasterDPI(
      AXI4BundleParameters(
        addrBits = frontAxip.addrBits,
        dataBits = frontAxip.dataBits,
        idBits = frontAxip.idBits
      )
    )
  )
  utils.connectAxi(soc.s_axi, axiMasterDPI.axi)
  val axiSlaveDPI = Module(
    new AXI4SlaveDPI(
      AXI4BundleParameters(
        addrBits = extAxip.addrBits,
        dataBits = extAxip.dataBits,
        idBits = extAxip.idBits
      )
    )
  )
  utils.connectAxi(axiSlaveDPI.axi, soc.m_axi)
  soc.int.usr_irq_ack := 0.U
  soc.jtag.foreach { j =>
    val jtag =
      Module(new SimJTAG(tickDelay = 3)).connect(
        j,
        clock,
        reset.asBool,
        ~reset.asBool,
        io.success
      )
  }

  ExtraFiles.add("src/main/csrc/TestRvtsoc/cliUtils.cpp")
  ExtraFiles.add("src/main/csrc/TestRvtsoc/main.cpp")
  ExtraFiles.add("src/main/csrc/devices/uram.cpp")
  ExtraFiles.addIncludePath("src/main/csrc/TestRvtsoc")
}
