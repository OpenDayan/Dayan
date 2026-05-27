package rvtsoc.subsystem

import chisel3._
import chisel3.util._

import org.chipsalliance.cde.config._

import lmss.LaomaXbar
import lmss.axi.{AxiParams, ExtAxiBundle}
import lmss.param.{AddressParams, LmssParams, PortParams}

case object NpuCoreParamsKey extends Field[Option[NpuCoreParams]](None)

case class NpuCoreParams()
