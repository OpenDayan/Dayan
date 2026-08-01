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
      Seq(
        RegInit((params.defaultBootAddress & 0xffffffffL).U((regWidth / 2).W)),
        RegInit((params.defaultBootAddress >> 32).U((regWidth / 2).W))
      )
    }

    node.regmap(
      0 -> RegFieldGroup(
        "boot-address",
        Some("boot-address-reg"),
        bootAddrRegs.zipWithIndex.flatMap { case (r, i) =>
          RegField(
            regWidth / 2,
            r(0),
            RegFieldDesc(
              s"boot-address-$i-l",
              s"Boot Low Address for Hart $i",
              reset = Some(params.defaultBootAddress)
            )
          ) :: RegField(
            regWidth / 2,
            r(1),
            RegFieldDesc(
              s"boot-address-$i-h",
              s"Boot High Address for Hart $i",
              reset = Some(params.defaultBootAddress >> 32)
            )
          ) :: Nil
        }
      )
    )
  }
}
