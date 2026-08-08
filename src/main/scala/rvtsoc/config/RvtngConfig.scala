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

package dayan.rvtsoc.config

import chisel3._
import chisel3.util._

import org.chipsalliance.cde.config._

import dayan.rvtng.vec.VecParams
import dayan.rvtng.neomat.MatParams
import dayan.rvtng.dma.DMAParams
import dayan.rvtng.bus.TLBundleParameters
import dayan.rvtng.vpu._

import dayan.rvtsoc.subsystem.XbarParamsKey

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
