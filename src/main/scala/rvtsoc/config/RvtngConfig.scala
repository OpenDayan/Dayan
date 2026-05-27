package rvtsoc.config

import chisel3._
import chisel3.util._

import org.chipsalliance.cde.config._

import rvtng.neovec.VecParams
import rvtng.neomat.MatParams
import rvtng.dma.DMAParams
import rvtng.neouram.URAMParms
import rvtng.bus.TLBundleParameters

case class RvtngParams(
    matParams: MatParams = MatParams(cM = 16, cN = 8, cK = 32), // 8T @ 1G
    dmaParams: DMAParams = DMAParams(),
    vecParams: VecParams = VecParams(),
    uramParams: URAMParms = URAMParms()
) {
  val tlParms = TLBundleParameters(
    addressBits = dmaParams.addressBits,
    dataBits = 256,
    sourceBits = dmaParams.sourceBits,
    sinkBits = 1,
    sizeBits = log2Ceil(256 / 8),
    hasBCE = false
  )
}

case object RvtngParamsKey extends Field[Option[RvtngParams]](None)

class RvtngConfig
    extends Config((site, here, up) => { case RvtngParamsKey =>
      Some(RvtngParams())
    })
