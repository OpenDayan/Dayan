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

class BootAddrReg(params: BootAddrRegParams, beatBytes: Int, nCores: Int)(
    implicit p: Parameters
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
    val regWidth = 64

    val bootAddrRegs = Seq.fill(nCores) {
      RegInit(params.defaultBootAddress.U(regWidth.W))
    }

    node.regmap(
      0 -> RegFieldGroup(
        "boot-address",
        Some("boot-address-reg"),
        bootAddrRegs.zipWithIndex.flatMap { case (r, i) =>
          RegField(
            regWidth,
            r,
            RegFieldDesc(
              s"boot-address-$i",
              s"Boot Address for Hart $i",
              reset = Some(params.defaultBootAddress)
            )
          ) :: Nil
        }
      )
    )
  }
}
