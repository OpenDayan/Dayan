package rvtsoc.top

import chisel3._
import chisel3.util._

import org.chipsalliance.cde.config.Parameters
import org.chipsalliance.diplomacy.lazymodule.LazyModule

import freechips.rocketchip.devices.debug.SimJTAG
import freechips.rocketchip.amba.axi4._

import xs.infra.axi.ExtAxiBundle

import rvtsoc.utils.ExtraFiles
import rvtsoc.devices._
import rvtsoc.subsystem.XbarParamsKey
import rvtsoc.subsystem.utils

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
