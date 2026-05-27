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

class RvtsocConfig
    extends Config(
      new NpuCoreConfig ++
        new WiredrvConfig ++
        // new RvtngConfig ++
        new BaseConfig
    )
