package rvtsoc.devices

import chisel3._
import org.chipsalliance.cde.config._
import freechips.rocketchip.diplomacy._
import freechips.rocketchip.tilelink._
import freechips.rocketchip.devices.tilelink._
import freechips.rocketchip.regmapper._
import freechips.rocketchip.subsystem._

case class BootAddrRegParams(
    defaultBootAddress: BigInt = 0x80000000L, // This should be DRAM_BASE
    bootRegAddress: BigInt = 0x1000
)
case object BootAddrRegKey extends Field[Option[BootAddrRegParams]](None)

class BootAddrReg(params: BootAddrRegParams, beatBytes: Int)(implicit
    p: Parameters
) extends LazyModule {

  val device = new SimpleDevice("boot-address-reg", Nil)

  val node = TLRegisterNode(
    Seq(AddressSet(params.bootRegAddress, 4096 - 1)),
    device,
    "reg/control",
    beatBytes = beatBytes
  )

  lazy val module = new Impl
  class Impl extends LazyModuleImp(this) {
    val bootAddrReg = RegInit(params.defaultBootAddress.U(64.W))
    node.regmap(0 -> RegField.bytes(bootAddrReg))
  }

}
