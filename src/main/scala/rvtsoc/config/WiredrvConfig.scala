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

import dayan.wiredrv.CoreParams
import freechips.rocketchip.devices.tilelink.BootROMLocated
import freechips.rocketchip.subsystem.InSubsystem
import freechips.rocketchip.tile.MaxHartIdBits

import dayan.rvtsoc.subsystem.XbarParamsKey

case object WiredrvParamsKey extends Field[Option[CoreParams]](None)

class WiredrvConfig
    extends Config((site, here, up) => {
      case WiredrvParamsKey =>
        Some(
          CoreParams(
            maxHartIdBits = up(MaxHartIdBits, site),
            resetPc = up(BootROMLocated(InSubsystem), site).head.hang,
            outerBusWidth = 64
          )
        )
      case XbarParamsKey => up(XbarParamsKey).copy(scalarDataBits = 64)
    })
