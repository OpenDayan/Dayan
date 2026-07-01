package rvtsoc.subsystem

import chisel3._
import chisel3.util._

import org.chipsalliance.cde.config._
import freechips.rocketchip.amba.apb.APBBundleParameters
import freechips.rocketchip.diplomacy.{AddressSet, RegionType}

import xs.infra.axi._

case object XbarParamsKey extends Field[XbarParams]

case class XbarParams(
    addrBits: Int = 40,
    frontIdBits: Int = 6,
    tensorIdBits: Int = 9,
    scalarIdBits: Int = 5,

    frontDataBits: Int = 512,
    scalarDataBits: Int = 64,
    uramDataBits: Int = 256,
    tensorDataBits: Int = 512,
    controlDataBits: Int = 64,
    // 0x00_0000_0000 - 0x00_0040_0000 control
    // 0x00_0080_0000 - 0x00_00c0_0000 uram
    // 0x00_8000_0000 - 0x10_0000_0000 mem

    system_begin: Long = 0x00_0000_0000L,
    system_end: Long = 0x10_0000_0000L,

    control_begin: Long = 0x00_0000_0000L,
    control_end: Long = 0x00_0040_0000L,

    uram_begin: Long = 0x00_0080_0000L,
    uram_end: Long = 0x00_00c0_0000L,
    uram_size_per_core: Long = 1L * 1024 * 1024,

    uram_tlerr_begin: Long = 0x00_00c0_0000L,

    ext_mem_begin: Long = 0x00_8000_0000L,
    ext_mem_end: Long = 0x10_0000_0000L,

    errorBase: Long = 0x3000,

    intrHandlerBase: Long = 0x2000
) {
  val extIdBits = tensorIdBits + log2Ceil(4)
  val uramIdBits = extIdBits
  val controlIdBits = extIdBits

  val frontAxip = AxiParams(addrBits, frontIdBits, dataBits = frontDataBits)
  val frontDbgAxip = frontAxip.copy(idBits = extIdBits)
  val scalarAxip = AxiParams(addrBits, scalarIdBits, dataBits = scalarDataBits)
  val tensorAxip = AxiParams(addrBits, tensorIdBits, dataBits = tensorDataBits)
  val uramAxip = AxiParams(addrBits, uramIdBits, dataBits = uramDataBits)
  val controlAxip =
    AxiParams(addrBits, controlIdBits, dataBits = controlDataBits)
  val extAxip = AxiParams(addrBits, extIdBits, dataBits = tensorDataBits)

  val systemAddrSet = AddressSet(system_begin, system_end - 1)
  val errorAddrSet = AddressSet(errorBase, 0xfff)

  val uramAddrSet = AddressSet(uram_begin, uram_end - uram_begin - 1)
  val uramErrAddrSet = AddressSet(uram_tlerr_begin, 0xfff)

  val controlAddrParams = AddressParams(control_begin, control_end - control_begin)
  val memAddrParams = AddressParams(ext_mem_begin, system_end - ext_mem_begin)
  val uramAddrParams: (Int) => AddressParams = (i: Int) =>
    AddressParams(uram_begin + i * uram_size_per_core, uram_size_per_core)
  val uramAllAddrParams = AddressParams(uram_begin, uram_end - uram_begin)
}
