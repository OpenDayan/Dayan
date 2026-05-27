package rvtsoc.config

import chisel3._
import chisel3.util._

import org.chipsalliance.cde.config._

import wiredrv.CoreParams
import freechips.rocketchip.devices.tilelink.BootROMLocated
import freechips.rocketchip.subsystem.InSubsystem
import freechips.rocketchip.tile.MaxHartIdBits

import rvtsoc.subsystem.XbarParamsKey

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
