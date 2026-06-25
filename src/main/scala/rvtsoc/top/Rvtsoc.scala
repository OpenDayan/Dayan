package rvtsoc.top

import chisel3._
import chisel3.experimental.BaseModule

import org.chipsalliance.cde.config.Parameters
import org.chipsalliance.diplomacy.lazymodule.LazyModule

import freechips.rocketchip.util.{AsyncQueueParams, BooleanToAugmentedBoolean}
import freechips.rocketchip.devices.debug.ExportDebug
import freechips.rocketchip.jtag.JTAGIO

import xs.infra.axi._

import PCIeAdapter.XDMAIOBundle

import rvtsoc.subsystem._
import rvtsoc.subsystem.utils.genXbarSlvp
import rvtsoc.blackbox
import rvtsoc.devices._

class RvtsocWrapper(implicit val p: Parameters) extends Module {

  val rvtsoc = Module(new Rvtsoc)

  val s_axi = IO(
    Flipped(
      new ExtAxiBundle(rvtsoc.frontAxip.copy(atomicBits = 0, addrBits = 32))
    )
  )
  val m_axi = IO(new ExtAxiBundle(rvtsoc.extAxip.copy(atomicBits = 0)))
  val int = IO(rvtsoc.int.cloneType)

  rvtsoc.s_axi <> s_axi
  m_axi <> rvtsoc.m_axi
  int <> rvtsoc.int
  rvtsoc.jtag.map { j =>
    j <> DontCare
  }
}

class Rvtsoc(implicit p: Parameters) extends Module {
  val uramAxip = p(XbarParamsKey).uramAxip
  val frontAxip = p(XbarParamsKey).frontAxip
  val frontDbgAxip = p(XbarParamsKey).frontDbgAxip
  val scalarAxip = p(XbarParamsKey).scalarAxip
  val tensorAxip = p(XbarParamsKey).tensorAxip
  val controlAxip = p(XbarParamsKey).controlAxip
  val extAxip = p(XbarParamsKey).extAxip
  val nInterrupts = p(IntrParamsKey).nInterrupts
  val intrGroupSize = p(IntrParamsKey).intrGroupSize

  val s_axi = IO(Flipped(new ExtAxiBundle(frontAxip)))
  val m_axi = IO(new ExtAxiBundle(extAxip))
  val int = IO(new XDMAIOBundle(nInterrupts / intrGroupSize))
  val jtag = p(ExportDebug).jtag.option(IO(Flipped(new JTAGIO)))

  val memAddrParams = p(XbarParamsKey).memAddrParams
  val controlAddrParams = p(XbarParamsKey).controlAddrParams
  val uramAddrParams = p(XbarParamsKey).uramAddrParams
  val uramAllAddrParams = p(XbarParamsKey).uramAllAddrParams

  val nCores = 1

  val lazyControlBlock = LazyModule(new ControlBlock)
  val controlBlock = Module(lazyControlBlock.module)

  val systemXbarSlvp = genXbarSlvp(
    Seq("front", "scalar", "tensor") ++
      controlBlock.io.m_axi.map(_ => "debug"),
    tensorAxip
  )
  val systemXbarMstp = Seq(
    PortParams(
      axip = controlAxip,
      async = Some(AsyncQueueParams()),
      name = "control",
      addr = controlAddrParams
    ),
    PortParams(
      axip = uramAxip,
      async = Some(AsyncQueueParams()),
      name = "tensor",
      addr = uramAddrParams(0)
    ),
    PortParams(
      axip = extAxip,
      async = Some(AsyncQueueParams()),
      name = "ext",
      addr = memAddrParams
    )
  )
  val systemXbar = withReset(reset.asAsyncReset) {
    Module(
      new SystemXbar(
        systemXbarSlvp,
        systemXbarMstp
      )
    )
  }

  val core = Module(
    p(NpuCoreParamsKey).map { _ => new NpuCore }.getOrElse(new blackbox.NpuCore)
  )

  core.io.hartId := 0.U
  core.io.scalar_int_in := controlBlock.io.scalar_ints_out(0)
  core.io.time := controlBlock.io.mtime.get
  controlBlock.io.tensor_ints_in(0) := core.io.tensor_int_out

  val scalarAxiNtoW = Module(
    new AxiNarrowToWide(
      scalarAxip,
      tensorAxip,
      2
    )
  )
  scalarAxiNtoW.io.mst <> core.io.m_axi_scalar
  systemXbar.getSlv("scalar") <> scalarAxiNtoW.io.slv
  systemXbar.getSlv("tensor") <> core.io.m_axi_tensor
  core.io.s_axi_tensor <> systemXbar.getMst("tensor")

  require(frontAxip.dataBits == tensorAxip.dataBits)

  controlBlock.io.m_axi.map { m_axi => systemXbar.getSlv("debug") <> m_axi }
  systemXbar.getSlv("front") <> s_axi

  controlBlock.io.s_axi <> systemXbar.getMst("control")
  m_axi <> systemXbar.getMst("ext")

  int <> lazyControlBlock.module.io.usr_int

  controlBlock.io.jtag.foreach { j =>
    j <> jtag.get
  }
}
