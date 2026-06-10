package rvtsoc.config

import chisel3._
import chisel3.util._

import org.chipsalliance.cde.config._

import rvtng.vec.VecParams
import rvtng.neomat.MatParams
import rvtng.dma.DMAParams
import rvtng.bus.TLBundleParameters
import rvtng.vpu._

import rvtsoc.subsystem.XbarParamsKey

case class RvtngParams(
    // matParams: MatParams = MatParams(cM = 16, cN = 8, cK = 32), // 8T @ 1G
    matParams: MatParams = MatParams(cM = 8, cN = 8, cK = 32), // 4T @ 1G
    vecParams: VecParams = VecParams(),
    dmaParams: DMAParams = DMAParams(),
    vpuParams: VpuParams = VpuParams(),
    tlSourceBits: Int = 9
) {
  val tlParms = TLBundleParameters(
    addressBits = dmaParams.addressBits,
    dataBits = 256,
    sourceBits = tlSourceBits,
    sinkBits = 1,
    sizeBits = log2Ceil(256 / 8),
    hasBCE = false
  )
}

case object RvtngParamsKey extends Field[Option[RvtngParams]](None)

class RvtngConfig
    extends Config((site, here, up) => { case RvtngParamsKey =>
      Some(RvtngParams(tlSourceBits = up(XbarParamsKey).uramAxip.idBits + 1))
    })
