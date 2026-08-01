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

package rvtsoc.config

import org.chipsalliance.cde.config._

import freechips.rocketchip.subsystem._
import freechips.rocketchip.devices.tilelink.{
  BootROMLocated,
  BootROMParams,
  CLINTKey,
  CLINTParams,
  PLICKey,
  PLICParams
}
import freechips.rocketchip.devices.debug.{
  DebugModuleKey,
  DefaultDebugModuleParams,
  ExportDebug,
  JTAG
}
import freechips.rocketchip.util.SystemFileName
import freechips.rocketchip.tile.MaxHartIdBits
import freechips.rocketchip.diplomacy.MonitorsEnabled

import xs.utils.perf._

import rvtsoc.devices._
import rvtsoc.subsystem._

class NpuROMConfig
    extends Config((site, here, up) => { case BootROMLocated(InSubsystem) =>
      Seq(
        BootROMParams(
          contentFileName =
            SystemFileName("./src/main/resources/bootrom/bootrom.rv64.img"),
          hang = 0x10000 // wait for MSIP interrupt to start program execution
          // hang = 0x10000000L // sim flash
        )
      )
    })

class XbarConfig
    extends Config((site, here, up) => { case XbarParamsKey =>
      XbarParams()
    })

class ControlBlockConfig
    extends Config((site, here, up) => {
      case CLINTKey => Some(CLINTParams(0x20000))
      // case PLICKey        => Some(PLICParams())
      case BootAddrRegKey => Some(BootAddrRegParams(0x80000000L))
      case IntrParamsKey  => IntrParams()
      case DebugModuleKey =>
        Some(DefaultDebugModuleParams(64).copy(hasBusMaster = true))
      case ExportDebug  => up(ExportDebug, site).copy(protocols = Set(JTAG))
      case RtcPeriodKey => BigInt(1_000)
    })

class NpuCoreConfig
    extends Config(
      new Config((site, here, up) => { case NpuCoreParamsKey =>
        Some(NpuCoreParams())
      })
    )

class BaseConfig
    extends Config(
      new Config((site, here, up) => { case MaxHartIdBits =>
        2
      }) ++
        new WithClockGateModel ++
        new XbarConfig ++
        new ControlBlockConfig ++
        new NpuROMConfig
    )

class VerifRvtsocConfig
    extends Config(
      new NpuCoreConfig ++
        new WiredrvConfig ++
        new RvtngConfig ++
        new BaseConfig
    )

class RvtsocConfig
    extends Config(
      new WithoutTLMonitors ++
        new VerifRvtsocConfig
    )
